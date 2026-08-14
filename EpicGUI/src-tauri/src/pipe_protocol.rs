// pipe_protocol.rs
// Direct port of EpicGUI/src/pipe_protocol.h
// Shared wire format between ScreamAPI DLL (C++) and EpicGUI (Rust).
// Named pipe: \\.\pipe\EpicGUI

#![allow(dead_code)]

use serde::{Deserialize, Serialize};

// ── Constants ────────────────────────────────────────────────────────────────
pub const EPIC_PIPE_NAME: &str = r"\\.\pipe\EpicGUI";
pub const EPIC_MAGIC: u32 = 0xABD04E21;
pub const EPIC_MAX_PAYLOAD: usize = 8 * 1024 * 1024; // 8 MB sanity cap

// ── Packet types ─────────────────────────────────────────────────────────────
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PktType {
    // DLL → GUI
    AchList = 0x01,
    AchUpdate = 0x02,
    LogPath = 0x03,
    DlcCatalog = 0x04,
    // GUI → DLL
    CmdUnlock = 0x10,
    CmdUnlockAll = 0x11,
    CmdRefresh = 0x12,
}

impl PktType {
    pub fn from_u8(v: u8) -> Option<Self> {
        match v {
            0x01 => Some(PktType::AchList),
            0x02 => Some(PktType::AchUpdate),
            0x03 => Some(PktType::LogPath),
            0x04 => Some(PktType::DlcCatalog),
            0x10 => Some(PktType::CmdUnlock),
            0x11 => Some(PktType::CmdUnlockAll),
            0x12 => Some(PktType::CmdRefresh),
            _ => None,
        }
    }
}

// ── Wire unlock state ────────────────────────────────────────────────────────
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum WireUnlockState {
    Locked = 0,
    Unlocked = 1,
    Unlocking = 2,
}

impl WireUnlockState {
    pub fn from_u8(v: u8) -> Option<Self> {
        match v {
            0 => Some(WireUnlockState::Locked),
            1 => Some(WireUnlockState::Unlocked),
            2 => Some(WireUnlockState::Unlocking),
            _ => None,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            WireUnlockState::Locked => "Locked",
            WireUnlockState::Unlocked => "Unlocked",
            WireUnlockState::Unlocking => "Unlocking",
        }
    }
}

// ── Wire structs (packed, matching #pragma pack(push, 1)) ────────────────────

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct PktHeader {
    pub magic: u32,
    pub pkt_type: u8,
    pub payload_size: u32,
}

impl PktHeader {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    /// Parses a 9-byte packet header from `buf`.
    ///
    /// IMPORTANT: this only parses the header bytes — it does NOT validate that
    /// the payload is present in `buf`. The caller is responsible for reading
    /// `payload_size` bytes separately after the header (this is how named-pipe
    /// reads work: we read the 9-byte header first to learn the payload size,
    /// then issue a second read for the payload).
    ///
    /// The returned `&[u8]` slice is the portion of `buf` AFTER the header
    /// (possibly empty if `buf.len() == SIZE`). It is provided for callers that
    /// happen to have header+payload in one buffer; `read_packet` ignores it.
    pub fn read_from(buf: &[u8]) -> Result<(Self, &[u8]), ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::HeaderTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        let magic = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
        if magic != EPIC_MAGIC {
            return Err(ProtocolError::BadMagic { got: magic });
        }
        let pkt_type = PktType::from_u8(buf[4])
            .ok_or(ProtocolError::UnknownPktType { got: buf[4] })?;
        let payload_size = u32::from_le_bytes([buf[5], buf[6], buf[7], buf[8]]) as usize;
        if payload_size > EPIC_MAX_PAYLOAD {
            return Err(ProtocolError::PayloadTooLarge { size: payload_size });
        }
        // Return whatever bytes follow the header. Callers that only passed a
        // 9-byte header buffer will get an empty slice here, which is fine —
        // they read the payload separately. Callers that passed header+payload
        // in one buffer get the payload slice for convenience.
        let rest = &buf[Self::SIZE..];
        Ok((
            PktHeader {
                magic,
                pkt_type: pkt_type as u8,
                payload_size: payload_size as u32,
            },
            rest,
        ))
    }

    pub fn pkt_type(&self) -> Option<PktType> {
        PktType::from_u8(self.pkt_type)
    }
}

// ── AchList payload ──────────────────────────────────────────────────────────

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct AchListHeader {
    pub count: u32,
    pub blob_size: u32,
}

impl AchListHeader {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn read_from(buf: &[u8]) -> Result<Self, ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::PayloadTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        Ok(AchListHeader {
            count: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            blob_size: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
        })
    }
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct AchEntry {
    pub id_off: u32,
    pub name_off: u32,
    pub desc_off: u32,
    /// Offset of the UnlockedIconURL string in the blob. 0 means no URL
    /// (older DLL builds that don't send icon URLs will zero this field).
    pub icon_url_off: u32,
    pub is_hidden: u8,
    pub state: u8,
    /// A3: player progress as fixed-point 0..1000 (0 = 0%, 1000 = 100%).
    /// Divide by 1000.0 to recover the 0..1 float. Older DLL builds that
    /// don't send progress will zero this field (which correctly reads as 0%).
    pub progress: u16,
    /// A3: offset of the StatThresholdLabel string in the blob (0 = no label).
    /// e.g. "12/50 kills" — rendered next to the progress bar in the GUI.
    pub stat_threshold_off: u32,
}

impl AchEntry {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn read_from(buf: &[u8]) -> Result<Self, ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::PayloadTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        Ok(AchEntry {
            id_off: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            name_off: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
            desc_off: u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]),
            icon_url_off: u32::from_le_bytes([buf[12], buf[13], buf[14], buf[15]]),
            is_hidden: buf[16],
            state: buf[17],
            // A3: progress (u16 LE) + stat_threshold_off (u32 LE)
            progress: u16::from_le_bytes([buf[18], buf[19]]),
            stat_threshold_off: u32::from_le_bytes([buf[20], buf[21], buf[22], buf[23]]),
        })
    }
}

// ── AchUpdate payload ────────────────────────────────────────────────────────

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct AchUpdatePkt {
    pub id: [u8; 128],
    pub state: u8,
}

impl AchUpdatePkt {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn read_from(buf: &[u8]) -> Result<Self, ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::PayloadTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        let mut id = [0u8; 128];
        id.copy_from_slice(&buf[..128]);
        Ok(AchUpdatePkt {
            id,
            state: buf[128],
        })
    }

    pub fn id_str(&self) -> String {
        let nul = self.id.iter().position(|&b| b == 0).unwrap_or(128);
        String::from_utf8_lossy(&self.id[..nul]).into_owned()
    }
}

// ── CmdUnlock payload ────────────────────────────────────────────────────────

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct CmdUnlockPkt {
    pub id: [u8; 128],
}

impl CmdUnlockPkt {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn new(id: &str) -> Self {
        let mut buf = [0u8; 128];
        let bytes = id.as_bytes();
        let n = bytes.len().min(127);
        buf[..n].copy_from_slice(&bytes[..n]);
        CmdUnlockPkt { id: buf }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        self.id.to_vec()
    }
}

// ── LogPath payload ──────────────────────────────────────────────────────────

pub const LOG_PATH_MAX: usize = 260; // MAX_PATH on Windows

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct LogPathPkt {
    pub path: [u8; LOG_PATH_MAX],
}

impl LogPathPkt {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn read_from(buf: &[u8]) -> Result<Self, ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::PayloadTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        let mut path = [0u8; LOG_PATH_MAX];
        path.copy_from_slice(&buf[..LOG_PATH_MAX]);
        Ok(LogPathPkt { path })
    }

    pub fn path_str(&self) -> String {
        let nul = self.path.iter().position(|&b| b == 0).unwrap_or(LOG_PATH_MAX);
        String::from_utf8_lossy(&self.path[..nul]).into_owned()
    }
}

// ── DlcCatalog payload ───────────────────────────────────────────────────────

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct DlcCatalogHeader {
    pub count: u32,
    pub blob_size: u32,
}

impl DlcCatalogHeader {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn read_from(buf: &[u8]) -> Result<Self, ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::PayloadTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        Ok(DlcCatalogHeader {
            count: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            blob_size: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
        })
    }
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct DlcCatalogEntry {
    pub id_off: u32,
    pub title_off: u32,
}

impl DlcCatalogEntry {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn read_from(buf: &[u8]) -> Result<Self, ProtocolError> {
        if buf.len() < Self::SIZE {
            return Err(ProtocolError::PayloadTooShort {
                got: buf.len(),
                want: Self::SIZE,
            });
        }
        Ok(DlcCatalogEntry {
            id_off: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            title_off: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
        })
    }
}

// ── Helpers for reading null-terminated strings from the blob ────────────────

pub fn read_string(blob: &[u8], offset: u32) -> Option<String> {
    let off = offset as usize;
    if off >= blob.len() {
        return None;
    }
    let end = blob[off..]
        .iter()
        .position(|&b| b == 0)
        .map(|e| off + e)
        .unwrap_or(blob.len());
    Some(String::from_utf8_lossy(&blob[off..end]).into_owned())
}

// ── Errors ───────────────────────────────────────────────────────────────────

#[derive(Debug, thiserror::Error)]
pub enum ProtocolError {
    #[error("header too short: got {got} bytes, want {want}")]
    HeaderTooShort { got: usize, want: usize },
    #[error("bad magic: 0x{got:08X}")]
    BadMagic { got: u32 },
    #[error("unknown packet type: 0x{got:02X}")]
    UnknownPktType { got: u8 },
    #[error("payload too large: {size} bytes")]
    PayloadTooLarge { size: usize },
    #[error("payload too short: got {got}, want {want}")]
    PayloadTooShort { got: usize, want: usize },
    #[error("string offset out of range: {0}")]
    StringOffsetOutOfRange(u32),
    #[error("utf-8 decode error: {0}")]
    Utf8(#[from] std::string::FromUtf8Error),
}
