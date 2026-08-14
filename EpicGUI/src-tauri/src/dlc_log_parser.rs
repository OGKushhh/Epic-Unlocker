// dlc_log_parser.rs
// Port of the C++ EpicGUI's ParseDlcLine() and SplitIdTitle() helpers.
//
// ScreamAPI.log emits DLC-related lines tagged with [DLC]. This module parses
// them and mutates a `HashMap<String, DlcStat>` (keyed by DLC id) plus an
// `entitlement_count` counter. The C++ GUI did this in `ParseDlcLine` and
// stored results in `g_dlcItems` / `g_entitlementCount`; we do the same here
// in Rust, holding the same logical state inside `AppState::dlc_stats` and
// `AppState::entitlement_count`.
//
// Supported log line formats (mirrors ScreamAPI's ecom_hooks logging):
//   1. `... [DLC] Item ID: <id>`                          → times_queried += 1
//   2. `... [DLC] Item ID: <id> ("Title")`                → times_queried += 1, fallback title
//   3. `... [DLC] [Owned] <id>`                           → current_owned = true,  times_owned += 1
//   4. `... [DLC] [Owned] <id> ("Title")`                 → same + fallback title
//   5. `... [DLC] [Not Owned] <id>`                       → current_owned = false
//   6. `... [DLC] [Not Owned] <id> ("Title")`             → same + fallback title
//   7. `... [DLC] GetEntitlementsCount: <N>`              → entitlement_count = N
//
// Title parsing: a token of the form `<id> ("Title")` extracts both. Bare
// `<id>` extracts just the id. Trailing whitespace/CR/LF is stripped.

use std::collections::HashMap;

use crate::state::DlcStat;

/// Result of parsing one log line. The caller uses this to decide whether
/// to emit a `dlc-stats-updated` Tauri event (only emit when `changed`).
#[derive(Debug, Default)]
pub struct ParseOutcome {
    pub changed: bool,
    /// True if a `GetEntitlementsCount:` line was seen and the value differed.
    pub entitlement_changed: bool,
}

/// Splits a token of the form `<id> ("Title")` into (id, Some(title)).
/// A bare `<id>` returns (id, None). Trailing whitespace on `id` is stripped.
fn split_id_title(token: &str) -> (String, Option<String>) {
    let token = token.trim_end_matches(['\r', '\n', ' ', '\t']);
    if let Some(paren) = token.find(" (\"") {
        let id = token[..paren].trim_end().to_string();
        let rest = &token[paren + 3..];
        let title = match rest.rfind("\")") {
            Some(end) if end > 0 => Some(rest[..end].to_string()),
            _ => None,
        };
        (id, title)
    } else {
        (token.to_string(), None)
    }
}

/// Parses one log line and, if it's a [DLC] line, mutates `dlc_stats` and
/// `entitlement_count` accordingly. Returns a `ParseOutcome` indicating
/// whether any state changed (so the caller can throttle event emission).
///
/// This is a faithful port of the C++ `ParseDlcLine` — including the quirk
/// that we only set the fallback `title` on a `DlcStat` if it's currently
/// `None` (so a later catalog packet takes precedence over the log fallback).
pub fn parse_dlc_line(
    line: &str,
    dlc_stats: &mut HashMap<String, DlcStat>,
    entitlement_count: &mut i32,
) -> ParseOutcome {
    // Fast path: only [DLC]-tagged lines are interesting. The C++ version
    // checks `wl.find(L"[DLC]") != npos` before calling ParseDlcLine.
    if !line.contains("[DLC]") {
        return ParseOutcome::default();
    }

    let mut out = ParseOutcome::default();

    // ── "Item ID: <id>" or "Item ID: <id> ("Title")" ──────────────────────
    if let Some(pos) = line.find("Item ID: ") {
        let token = &line[pos + "Item ID: ".len()..];
        let (id, title) = split_id_title(token);
        if !id.is_empty() {
            let entry = dlc_stats.entry(id.clone()).or_insert_with(|| DlcStat {
                id,
                title: None,
                times_queried: 0,
                times_owned: 0,
                current_owned: false,
            });
            entry.times_queried += 1;
            if entry.title.is_none() {
                entry.title = title;
            }
            out.changed = true;
        }
        return out;
    }

    // ── "[Owned] <id>" or "[Not Owned] <id>" ─────────────────────────────
    let owned_pos = line.find("[Owned] ");
    let not_owned_pos = line.find("[Not Owned] ");
    if let Some(start) = owned_pos {
        let token = &line[start + "[Owned] ".len()..];
        let (id, title) = split_id_title(token);
        if let Some(entry) = dlc_stats.get_mut(&id) {
            entry.current_owned = true;
            entry.times_owned += 1;
            if entry.title.is_none() {
                entry.title = title;
            }
            out.changed = true;
        }
        return out;
    }
    if let Some(start) = not_owned_pos {
        let token = &line[start + "[Not Owned] ".len()..];
        let (id, title) = split_id_title(token);
        if let Some(entry) = dlc_stats.get_mut(&id) {
            entry.current_owned = false;
            if entry.title.is_none() {
                entry.title = title;
            }
            out.changed = true;
        }
        return out;
    }

    // ── "GetEntitlementsCount: N" ─────────────────────────────────────────
    if let Some(pos) = line.find("GetEntitlementsCount:") {
        let rest = &line[pos + "GetEntitlementsCount:".len()..];
        let trimmed = rest.trim_start();
        // Parse leading digits (handles "8", "8\r\n", "8 [extra info]" etc.)
        let num_str: String = trimmed.chars().take_while(|c| c.is_ascii_digit()).collect();
        if let Ok(n) = num_str.parse::<i32>() {
            if *entitlement_count != n {
                *entitlement_count = n;
                out.changed = true;
                out.entitlement_changed = true;
            }
        }
    }

    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_item_id_bare() {
        let mut stats = HashMap::new();
        let mut ec = -1i32;
        let out = parse_dlc_line("[03:57:50.861] [INFO] [DLC] Item ID: dlc_season_pass_01", &mut stats, &mut ec);
        assert!(out.changed);
        assert_eq!(stats.get("dlc_season_pass_01").unwrap().times_queried, 1);
        assert!(stats.get("dlc_season_pass_01").unwrap().title.is_none());
    }

    #[test]
    fn parses_item_id_with_title() {
        let mut stats = HashMap::new();
        let mut ec = -1i32;
        let out = parse_dlc_line(
            "[DLC] Item ID: dlc_season_pass_01 (\"Season Pass\")",
            &mut stats,
            &mut ec,
        );
        assert!(out.changed);
        let e = stats.get("dlc_season_pass_01").unwrap();
        assert_eq!(e.times_queried, 1);
        assert_eq!(e.title.as_deref(), Some("Season Pass"));
    }

    #[test]
    fn parses_owned_and_increments_times_owned() {
        let mut stats = HashMap::new();
        let mut ec = -1i32;
        // First a query to create the entry
        parse_dlc_line("[DLC] Item ID: dlc_01", &mut stats, &mut ec);
        // Then two [Owned] responses
        parse_dlc_line("[DLC] [Owned] dlc_01", &mut stats, &mut ec);
        parse_dlc_line("[DLC] [Owned] dlc_01", &mut stats, &mut ec);
        let e = stats.get("dlc_01").unwrap();
        assert!(e.current_owned);
        assert_eq!(e.times_owned, 2);
        assert_eq!(e.times_queried, 1);
    }

    #[test]
    fn parses_not_owned_resets_current_owned() {
        let mut stats = HashMap::new();
        let mut ec = -1i32;
        parse_dlc_line("[DLC] Item ID: dlc_01", &mut stats, &mut ec);
        parse_dlc_line("[DLC] [Owned] dlc_01", &mut stats, &mut ec);
        parse_dlc_line("[DLC] [Not Owned] dlc_01", &mut stats, &mut ec);
        let e = stats.get("dlc_01").unwrap();
        assert!(!e.current_owned);
        assert_eq!(e.times_owned, 1); // not incremented by [Not Owned]
    }

    #[test]
    fn parses_get_entitlements_count() {
        let mut stats = HashMap::new();
        let mut ec = -1i32;
        let out = parse_dlc_line("[DLC] GetEntitlementsCount: 8", &mut stats, &mut ec);
        assert!(out.changed);
        assert!(out.entitlement_changed);
        assert_eq!(ec, 8);
    }

    #[test]
    fn ignores_non_dlc_lines() {
        let mut stats = HashMap::new();
        let mut ec = -1i32;
        let out = parse_dlc_line("[INFO] Some random line", &mut stats, &mut ec);
        assert!(!out.changed);
        assert!(stats.is_empty());
    }

    #[test]
    fn split_id_title_handles_trailing_whitespace() {
        let (id, title) = split_id_title("abc123 \r\n");
        assert_eq!(id, "abc123");
        assert!(title.is_none());
    }
}
