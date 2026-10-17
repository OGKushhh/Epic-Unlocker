// linux_impl.rs
// Unix domain socket client for EpicGUI on Linux.
//
// This is the Linux counterpart to windows_impl.rs. The ScreamAPI .so (when
// ported in Phase 3) will create a Unix domain socket as a server; we connect
// as a client. The wire protocol is identical (see pipe_protocol.rs) — only
// the transport differs (Unix socket vs Win32 named pipe).
//
// Connection model mirrors the Windows impl:
//   - Single background Tokio task for reading inbound packets.
//   - Outbound commands sent via a separate task that pulls from an mpsc
//     channel, serializing writes so we don't race with the reader.
//
// Unlike the Windows impl, we don't need PeekNamedPipe polling — Tokio's
// async UnixStream is naturally non-blocking, so we can use a real blocking
// `read_exact` via AsyncReadExt without deadlocking writes.
//
// Socket path: see `EPIC_PIPE_NAME` in pipe_protocol.rs (cfg-gated per OS).
// The path follows XDG conventions: $XDG_RUNTIME_DIR/epicgui.sock, falling
// back to /tmp/epicgui.sock if XDG_RUNTIME_DIR is unset (rare on desktop
// sessions, but possible over bare SSH).

use std::io;
use std::path::PathBuf;
use tokio::net::UnixStream;

// IMPORTANT: write_lock MUST be tokio::sync::Mutex, not std::sync::Mutex.
// std::sync::MutexGuard is !Send, so holding it across .await points makes
// the future !Send, which Tauri rejects for command handlers.
// tokio::sync::MutexGuard is Send (when T: Send), so it's safe to hold
// across awaits.
use tokio::sync::Mutex;

use crate::pipe_protocol::*;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[derive(Debug, thiserror::Error)]
pub enum PipeError {
    #[error("io error: {0}")]
    Io(#[from] io::Error),
    #[error("protocol error: {0}")]
    Protocol(#[from] ProtocolError),
    #[error("pipe closed")]
    Closed,
}

pub struct PipeClient {
    stream: UnixStream,
    /// Mutex so multiple command-sending tasks can't interleave writes.
    write_lock: Mutex<()>,
}

impl PipeClient {
    /// Attempts to connect to the ScreamAPI Unix socket.
    ///
    /// The socket path is resolved by `socket_path()` — it honors
    /// `$XDG_RUNTIME_DIR` and falls back to `/tmp/`.
    ///
    /// Retries on connection failure for up to 5 attempts (200ms apart) to
    /// smooth over the race where ScreamAPI hasn't created the socket yet.
    pub async fn connect() -> Result<Self, PipeError> {
        let path = socket_path();
        log::info!("[linux_impl] connecting to Unix socket at {:?}", path);

        let mut attempts = 0u32;
        loop {
            attempts += 1;
            match UnixStream::connect(&path).await {
                Ok(stream) => {
                    log::info!(
                        "[linux_impl] connected to {:?} on attempt {}",
                        path,
                        attempts
                    );
                    return Ok(PipeClient {
                        stream,
                        write_lock: Mutex::new(()),
                    });
                }
                Err(e) => {
                    if attempts < 5 {
                        log::debug!(
                            "[linux_impl] connect attempt {} failed: {} — retrying in 200ms",
                            attempts,
                            e
                        );
                        tokio::time::sleep(std::time::Duration::from_millis(200)).await;
                        continue;
                    }
                    return Err(PipeError::Io(e));
                }
            }
        }
    }

    /// Attempts to read one full inbound packet (header + payload) from the
    /// socket.
    ///
    /// Returns `Ok(None)` if no complete packet is available yet (less than a
    /// full header, or header but incomplete payload). Returns `Ok(Some(pkt))`
    /// when a complete packet has been read. Returns `Err(Closed)` if the
    /// socket has been closed by the other end.
    ///
    /// The caller (reader loop in `pipe_client.rs`) polls this every ~50ms.
    ///
    /// Implementation note: unlike the Windows impl which uses PeekNamedPipe
    /// to avoid blocking on a non-overlapped handle, we use Tokio's async
    /// `read_exact` with a timeout. If the timeout fires before the full
    /// header arrives, we return `Ok(None)` and let the caller retry. This
    /// keeps the reader task responsive to shutdown.
    pub async fn try_read_packet(
        &self,
    ) -> Result<Option<crate::pipe_client::InboundPkt>, PipeError> {
        // Step 1: Try to read the header (9 bytes) with a short timeout.
        // If no data is available, the timeout fires and we return None.
        let mut header_buf = [0u8; PktHeader::SIZE];
        let n = match tokio::time::timeout(
            std::time::Duration::from_millis(50),
            self.read_exact_or_return_pending(&mut header_buf),
        )
        .await
        {
            Ok(Ok(n)) => n,
            Ok(Err(PipeError::Closed)) => return Err(PipeError::Closed),
            Ok(Err(e)) => return Err(e),
            // Timed out — no full header available yet.
            Err(_) => return Ok(None),
        };
        if n == 0 {
            return Ok(None);
        }

        let (header, _rest) = PktHeader::read_from(&header_buf)?;

        // Step 2: Read the payload (header.payload_size bytes). Use a longer
        // timeout — once the header is here, the payload usually arrives
        // within milliseconds, but allow slack for large AchList dumps.
        let payload = if header.payload_size as usize > 0 {
            let mut payload_buf = vec![0u8; header.payload_size as usize];
            self.read_exact_or_return_closed(&mut payload_buf).await?;
            payload_buf
        } else {
            Vec::new()
        };

        let pkt_type = header
            .pkt_type()
            .ok_or(PipeError::Protocol(ProtocolError::UnknownPktType {
                got: header.pkt_type,
            }))?;

        log::debug!(
            "[linux_impl] read complete: type={:?}, payload_len={}",
            pkt_type,
            payload.len()
        );

        Ok(Some(crate::pipe_client::InboundPkt {
            pkt_type,
            payload,
        }))
    }

    /// Sends an outbound command packet (GUI → DLL).
    pub async fn send_command(
        &self,
        pkt_type: PktType,
        payload: &[u8],
    ) -> Result<(), PipeError> {
        log::info!(
            "[linux_impl] send_command: pkt_type={:?}, payload_len={}",
            pkt_type,
            payload.len()
        );
        let _guard = self.write_lock.lock().await;

        let mut buf = Vec::with_capacity(PktHeader::SIZE + payload.len());
        buf.extend_from_slice(&EPIC_MAGIC.to_le_bytes());
        buf.push(pkt_type as u8);
        buf.extend_from_slice(&(payload.len() as u32).to_le_bytes());
        buf.extend_from_slice(payload);

        // Use write_all on the async stream — UnixStream is naturally
        // non-blocking, so we don't need the spawn_blocking dance the
        // Windows impl uses.
        self.stream.writable().await?;
        self.stream.write_all(&buf).await?;
        log::info!(
            "[linux_impl] write_all succeeded — {} bytes written to socket",
            buf.len()
        );
        Ok(())
    }

    /// Reads exactly `buf.len()` bytes, returning the count.
    /// If the connection closes before any byte is read, returns `Ok(0)`.
    /// If the connection closes mid-read, returns `Err(Closed)`.
    async fn read_exact_or_return_pending(
        &self,
        buf: &mut [u8],
    ) -> Result<usize, PipeError> {
        let mut filled = 0usize;
        while filled < buf.len() {
            match self.stream.read(&mut buf[filled..]).await {
                Ok(0) => {
                    // EOF — socket closed.
                    if filled == 0 {
                        return Ok(0);
                    }
                    return Err(PipeError::Closed);
                }
                Ok(n) => {
                    filled += n;
                }
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                    // Should not happen with async, but be defensive.
                    if filled == 0 {
                        return Ok(0);
                    }
                    return Err(PipeError::Closed);
                }
                Err(e) => return Err(PipeError::Io(e)),
            }
        }
        Ok(filled)
    }

    /// Reads exactly `buf.len()` bytes or returns `Err(Closed)`.
    /// Used for payload reads after we already have the header — at this
    /// point we expect the rest to arrive promptly.
    async fn read_exact_or_return_closed(
        &self,
        buf: &mut [u8],
    ) -> Result<(), PipeError> {
        let mut filled = 0usize;
        while filled < buf.len() {
            match self.stream.read(&mut buf[filled..]).await {
                Ok(0) => return Err(PipeError::Closed),
                Ok(n) => filled += n,
                Err(e) => return Err(PipeError::Io(e)),
            }
        }
        Ok(())
    }
}

/// Resolves the Unix socket path for the ScreamAPI IPC channel.
///
/// Prefers `$XDG_RUNTIME_DIR/epicgui.sock` (per-user, world-writable by
/// default on modern Linux distros). Falls back to `/tmp/epicgui.sock` if
/// `XDG_RUNTIME_DIR` is unset (rare — usually only bare SSH sessions or
/// headless servers).
///
/// The path is parsed from `EPIC_PIPE_NAME` in pipe_protocol.rs, which on
/// Linux is the literal string `/tmp/epicgui.sock`. We honor XDG by
/// checking `$XDG_RUNTIME_DIR` first; if set and the user has write access,
/// we use it. Otherwise we fall back to the literal value in EPIC_PIPE_NAME.
fn socket_path() -> PathBuf {
    if let Ok(xdg) = std::env::var("XDG_RUNTIME_DIR") {
        if !xdg.is_empty() {
            let p = PathBuf::from(xdg).join("epicgui.sock");
            // Verify the parent dir exists and is writable. If not, fall back.
            if p.parent().map(|d| d.exists()).unwrap_or(false) {
                return p;
            }
        }
    }
    // Fallback: use the literal path from EPIC_PIPE_NAME.
    PathBuf::from(EPIC_PIPE_NAME)
}
