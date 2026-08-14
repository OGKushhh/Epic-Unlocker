// windows_impl.rs
// Named-pipe client for \\.\pipe\EpicGUI on Windows.
// We use raw Win32 API via the `windows` crate (or inline extern if needed).
//
// Connection model: ScreamAPI DLL creates the pipe as a server (CreateNamedPipe).
// We connect as a client (CreateFileW). The DLL is single-threaded on its end
// and expects to read commands synchronously, so we:
//   - Use a single background Tokio task for reading inbound packets.
//   - Outbound commands (CmdUnlock / CmdUnlockAll / CmdRefresh) are sent on a
//     separate Tokio task that pulls from an mpsc channel, serializing writes
//     so we don't race with the reader.
//
// IMPORTANT: We use PeekNamedPipe polling instead of a permanent blocking
// ReadFile. In non-overlapped mode, a blocking ReadFile would prevent
// WriteFile on the same handle from completing (the I/O Manager serializes
// operations on non-overlapped handles). This caused CmdUnlock to deadlock
// behind the reader's ReadFile — the DLL never received the unlock command.
// With peek-polling, the handle is only held briefly during actual reads,
// so writes can always proceed.
//
// For now we use a synchronous implementation wrapped in spawn_blocking.

use std::ffi::OsStr;
use std::io;
use std::os::windows::ffi::OsStrExt;
use std::os::windows::io::RawHandle;
use std::time::Duration;

// IMPORTANT: write_lock MUST be tokio::sync::Mutex, not std::sync::Mutex.
// std::sync::MutexGuard is !Send, so holding it across the spawn_blocking().await
// in send_command() makes the future !Send, which Tauri rejects for command
// handlers. tokio::sync::MutexGuard is Send (when T: Send), so it's safe to
// hold across awaits.
use tokio::sync::Mutex;

use crate::pipe_protocol::*;

const GENERIC_READ: u32 = 0x80000000;
const GENERIC_WRITE: u32 = 0x40000000;
const OPEN_EXISTING: u32 = 3;
const INVALID_HANDLE_VALUE: RawHandle = -1isize as RawHandle;
const ERROR_PIPE_BUSY: u32 = 231;

#[link(name = "kernel32")]
extern "system" {
    fn CreateFileW(
        lpFileName: *const u16,
        dwDesiredAccess: u32,
        dwShareMode: u32,
        lpSecurityAttributes: *const u8,
        dwCreationDisposition: u32,
        dwFlagsAndAttributes: u32,
        hTemplateFile: *const u8,
    ) -> RawHandle;

    fn CloseHandle(hObject: RawHandle) -> i32;

    fn GetLastError() -> u32;

    fn ReadFile(
        hFile: RawHandle,
        lpBuffer: *mut u8,
        nNumberOfBytesToRead: u32,
        lpNumberOfBytesRead: *mut u32,
        lpOverlapped: *const u8,
    ) -> i32;

    fn WriteFile(
        hFile: RawHandle,
        lpBuffer: *const u8,
        nNumberOfBytesToWrite: u32,
        lpNumberOfBytesWritten: *mut u32,
        lpOverlapped: *const u8,
    ) -> i32;

    fn PeekNamedPipe(
        hNamedPipe: RawHandle,
        lpBuffer: *mut u8,
        nBufferSize: u32,
        lpBytesRead: *mut u32,
        lpTotalBytesAvail: *mut u32,
        lpBytesLeftThisMessage: *mut u32,
    ) -> i32;
}

fn to_wide(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(std::iter::once(0)).collect()
}

/// Wrapper newtype around `RawHandle` (`*mut c_void`) that is `Send`.
/// Raw pointers are `!Send` by default, which prevents `spawn_blocking`
/// closures that capture the handle directly from being `Send`. We wrap the
/// handle in this newtype every time we need to move it into a `spawn_blocking`
/// task.
///
/// SAFETY: a Win32 HANDLE is an opaque kernel object identifier. It is safe to
/// reference from any thread — the underlying kernel object is thread-safe.
/// The only requirement is that callers serialize actual I/O on the handle,
/// which `PipeClient` does via `write_lock` for writes and a single-reader
/// invariant for reads.
#[derive(Copy, Clone)]
struct SendRawHandle(RawHandle);
unsafe impl Send for SendRawHandle {}

pub struct PipeClient {
    handle: RawHandle,
    /// Mutex so multiple command-sending tasks can't interleave writes.
    write_lock: Mutex<()>,
}

// SAFETY: PipeClient owns a single Win32 named-pipe handle. We serialize all
// writes via `write_lock` (Mutex<()>), and only one reader task is ever spawned
// for a given client (in pipe_client::spawn_pipe_loop). The handle is closed
// exactly once in Drop. Win32 handles are kernel objects and are safe to share
// across threads as long as access is serialized, which we enforce.
// Without these impls, `spawn_blocking` rejects the closure because
// `*mut c_void` (RawHandle) is `!Send` by default.
unsafe impl Send for PipeClient {}
unsafe impl Sync for PipeClient {}

#[derive(Debug, thiserror::Error)]
pub enum PipeError {
    #[error("CreateFileW failed (GetLastError={0})")]
    CreateFailed(u32),
    #[error("io error: {0}")]
    Io(#[from] io::Error),
    #[error("protocol error: {0}")]
    Protocol(#[from] ProtocolError),
    #[error("pipe closed")]
    Closed,
}

impl Drop for PipeClient {
    fn drop(&mut self) {
        if self.handle != INVALID_HANDLE_VALUE && !self.handle.is_null() {
            unsafe {
                CloseHandle(self.handle);
            }
        }
    }
}

impl PipeClient {
    /// Attempts to connect to the ScreamAPI named pipe.
    /// Retries on ERROR_PIPE_BUSY for up to 5 attempts (200ms apart).
    pub async fn connect() -> Result<Self, PipeError> {
        tokio::task::spawn_blocking(|| {
            let wide = to_wide(EPIC_PIPE_NAME);
            let mut attempts = 0;
            loop {
                attempts += 1;
                unsafe {
                    let h = CreateFileW(
                        wide.as_ptr(),
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        std::ptr::null(),
                        OPEN_EXISTING,
                        0,
                        std::ptr::null(),
                    );
                    if h != INVALID_HANDLE_VALUE && !h.is_null() {
                        return Ok(PipeClient {
                            handle: h,
                            write_lock: Mutex::new(()),
                        });
                    }
                    let err = GetLastError();
                    if err == ERROR_PIPE_BUSY && attempts < 5 {
                        std::thread::sleep(Duration::from_millis(200));
                        continue;
                    }
                    return Err(PipeError::CreateFailed(err));
                }
            }
        })
        .await
        .map_err(|e| PipeError::Io(io::Error::new(io::ErrorKind::Other, e.to_string())))?
    }

    /// Attempts to read one full inbound packet (header + payload) from the pipe.
    /// Returns `Ok(None)` if not enough data is available yet (less than a full
    /// header, or header but incomplete payload). Returns `Ok(Some(pkt))` when a
    /// complete packet has been read. Returns `Err(Closed)` if the pipe has been
    /// closed by the other end.
    ///
    /// This method uses `PeekNamedPipe` to check data availability BEFORE issuing
    /// a blocking `ReadFile`. This is CRITICAL: in non-overlapped mode, a blocking
    /// `ReadFile` would hold the handle's I/O slot and prevent `WriteFile` (from
    /// `send_command`) from completing. By peeking first, we only issue `ReadFile`
    /// when we know it will complete immediately (data is already in the kernel
    /// buffer), keeping the handle free for writes between packets.
    ///
    /// The caller (reader loop in `pipe_client.rs`) polls this method every ~50ms.
    pub async fn try_read_packet(&self) -> Result<Option<crate::pipe_client::InboundPkt>, PipeError> {
        let handle = SendRawHandle(self.handle);
        tokio::task::spawn_blocking(move || -> Result<Option<crate::pipe_client::InboundPkt>, PipeError> {
            // Step 1: Peek to see how many bytes are available in the pipe buffer.
            let mut available: u32 = 0;
            let rc = unsafe {
                PeekNamedPipe(
                    handle.0,
                    std::ptr::null_mut(),
                    0,
                    std::ptr::null_mut(),
                    &mut available,
                    std::ptr::null_mut(),
                )
            };
            if rc == 0 {
                let err = unsafe { GetLastError() };
                log::warn!("[try_read_packet] PeekNamedPipe failed (GetLastError={})", err);
                return Err(PipeError::Closed);
            }
            // Not enough data for even a header — return None so the caller can sleep.
            if (available as usize) < PktHeader::SIZE {
                return Ok(None);
            }
            // Step 2: Read the header (9 bytes). This will complete immediately
            // because we just confirmed >= 9 bytes are available.
            let header_buf = read_exact(handle, PktHeader::SIZE)?;
            let (header, _rest) = PktHeader::read_from(&header_buf)?;
            // Step 3: Check if the full payload is available. If not, we still
            // try to read it — ReadFile will block briefly until the rest arrives.
            // This is acceptable because the DLL sends header+payload back-to-back,
            // so the wait is at most a few milliseconds. The handle is released
            // as soon as the read completes.
            let payload = if header.payload_size as usize > 0 {
                read_exact(handle, header.payload_size as usize)?
            } else {
                Vec::new()
            };
            let pkt_type = header
                .pkt_type()
                .ok_or(PipeError::Protocol(ProtocolError::UnknownPktType {
                    got: header.pkt_type,
                }))?;
            log::debug!(
                "[try_read_packet] read complete: type={:?}, payload_len={}",
                pkt_type,
                payload.len()
            );
            Ok(Some(crate::pipe_client::InboundPkt {
                pkt_type,
                payload,
            }))
        })
        .await
        .map_err(|e| PipeError::Io(io::Error::new(io::ErrorKind::Other, e.to_string())))?
    }

    /// Sends an outbound command packet (GUI → DLL).
    pub async fn send_command(&self, pkt_type: PktType, payload: &[u8]) -> Result<(), PipeError> {
        log::info!(
            "[send_command] pkt_type={:?}, payload_len={}, handle={:p}",
            pkt_type,
            payload.len(),
            self.handle
        );
        let _guard = self.write_lock.lock().await;
        let handle = SendRawHandle(self.handle);
        let payload_owned = payload.to_vec();
        let total_len = PktHeader::SIZE + payload_owned.len();
        log::info!("[send_command] write_lock acquired, spawning blocking write of {} bytes", total_len);
        tokio::task::spawn_blocking(move || -> Result<(), PipeError> {
            let mut buf = Vec::with_capacity(total_len);
            buf.extend_from_slice(&EPIC_MAGIC.to_le_bytes());
            buf.push(pkt_type as u8);
            buf.extend_from_slice(&(payload_owned.len() as u32).to_le_bytes());
            buf.extend_from_slice(&payload_owned);
            log::info!(
                "[send_command] buf ready: {} bytes [magic={:#010x} type={:#04x} payload_size={}]",
                buf.len(),
                EPIC_MAGIC,
                pkt_type as u8,
                payload_owned.len()
            );
            write_all(handle, &buf)?;
            log::info!("[send_command] write_all succeeded — {} bytes written to pipe", buf.len());
            Ok(())
        })
        .await
        .map_err(|e| PipeError::Io(io::Error::new(io::ErrorKind::Other, e.to_string())))??;
        Ok(())
    }
}

fn read_exact(handle: SendRawHandle, n: usize) -> Result<Vec<u8>, PipeError> {
    let h = handle.0;
    let mut buf = vec![0u8; n];
    let mut filled = 0usize;
    while filled < n {
        let mut got: u32 = 0;
        let rc = unsafe {
            ReadFile(
                h,
                buf[filled..].as_mut_ptr(),
                (n - filled) as u32,
                &mut got,
                std::ptr::null(),
            )
        };
        if rc == 0 {
            return Err(PipeError::Closed);
        }
        if got == 0 {
            return Err(PipeError::Closed);
        }
        filled += got as usize;
    }
    Ok(buf)
}

fn write_all(handle: SendRawHandle, data: &[u8]) -> Result<(), PipeError> {
    let h = handle.0;
    let mut written = 0usize;
    log::info!("[write_all] starting WriteFile loop, data.len()={}, handle={:p}", data.len(), h);
    while written < data.len() {
        let mut put: u32 = 0;
        let rc = unsafe {
            WriteFile(
                h,
                data[written..].as_ptr(),
                (data.len() - written) as u32,
                &mut put,
                std::ptr::null(),
            )
        };
        let err = if rc == 0 { unsafe { GetLastError() } } else { 0 };
        log::info!(
            "[write_all] WriteFile returned rc={}, put={}, GetLastError={}, total_written={}",
            rc,
            put,
            err,
            written + put as usize
        );
        if rc == 0 || put == 0 {
            log::error!("[write_all] WriteFile FAILED — rc={}, GetLastError={}", rc, err);
            return Err(PipeError::Closed);
        }
        written += put as usize;
    }
    log::info!("[write_all] all {} bytes written successfully", data.len());
    Ok(())
}
