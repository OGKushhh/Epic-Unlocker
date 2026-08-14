// stub_impl.rs
// Non-Windows stub — allows the project to compile on Linux/macOS for development,
// but the pipe layer always returns Disconnected. The final .exe must be built
// on Windows (or cross-compiled), which is the target platform anyway.

use std::io;
use std::time::Duration;

use crate::pipe_protocol::*;

#[derive(Debug, thiserror::Error)]
pub enum PipeError {
    #[error("named pipes are only available on Windows (current OS cannot connect to ScreamAPI)")]
    NotWindows,
    #[error("io error: {0}")]
    Io(#[from] io::Error),
    #[error("protocol error: {0}")]
    Protocol(#[from] ProtocolError),
    #[error("pipe closed")]
    Closed,
}

pub struct PipeClient;

impl PipeClient {
    pub async fn connect() -> Result<Self, PipeError> {
        // Simulate retry delay so the connection loop doesn't spin.
        tokio::time::sleep(Duration::from_secs(1)).await;
        Err(PipeError::NotWindows)
    }

    /// Stub for the Windows `try_read_packet`. Always returns `Ok(None)` so
    /// the reader loop in `pipe_client.rs` (which polls this every 50ms) just
    /// sleeps and retries forever on non-Windows. The connect attempt above
    /// already returns `Err(NotWindows)` so this is never actually reached.
    #[allow(unused_variables)]
    pub async fn try_read_packet(&self) -> Result<Option<crate::pipe_client::InboundPkt>, PipeError> {
        Ok(None)
    }

    #[allow(unused_variables)]
    pub async fn send_command(&self, pkt_type: PktType, payload: &[u8]) -> Result<(), PipeError> {
        Err(PipeError::NotWindows)
    }
}
