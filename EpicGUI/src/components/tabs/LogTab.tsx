/**
 * Log Tab — carbon copy of the mockup's Log tab.
 * Filter toolbar (search + Clear + Auto-scroll + Open File) + monospace log view.
 * Each line is split into [timestamp] + colored level span.
 *
 * Data comes from the useGameData hook (real pipe data in production).
 *
 * Toolbar buttons:
 *   - Clear: wipes ScreamAPI.log on disk via `clear_log`. The next get_log_tail
 *     poll will reflect the empty file. Useful for resetting between game sessions.
 *   - Auto-scroll: when ON, the log view pins to the bottom whenever new lines
 *     arrive. When OFF, the view stays at the user's scroll position. The toggle
 *     auto-disables if the user scrolls up manually (so they can read history
 *     without fighting the autoscroll).
 *   - Open File: launches ScreamAPI.log in the user's default .log editor
 *     (Notepad by default on Windows; VSCode/Notepad++ if the user has
 *     reassigned .log). Implemented via ShellExecuteW on the Rust side.
 */

import { useEffect, useMemo, useRef, useState } from "react";
import type { LogLine } from "../../data/mockupData";
import { t, type Locale } from "../../i18n";

interface LogTabProps {
  active: boolean;
  lines: LogLine[];
  loading?: boolean;
  connected?: boolean;
  logPath?: string;
  locale?: Locale;
  /** Called when the user clicks "Clear" — should invoke the `clear_log` Tauri command. */
  onClear?: () => Promise<void> | void;
  /** Called when the user clicks "Open File" — should invoke `open_log_externally`. */
  onOpenFile?: () => Promise<void> | void;
}

export default function LogTab({
  active,
  lines,
  loading = false,
  connected = true,
  logPath,
  locale = "en",
  onClear,
  onOpenFile,
}: LogTabProps) {
  const [search, setSearch] = useState("");
  const [autoScroll, setAutoScroll] = useState(true);
  // User-feedback state for the toolbar buttons. Shows a brief "(opening…)"
  // or "(failed: …)" caption next to the button so the click is acknowledged
  // even if the launched editor takes a moment to appear.
  const [openStatus, setOpenStatus] = useState<"idle" | "working" | "error">("idle");
  const [openError, setOpenError] = useState<string>("");
  const [clearStatus, setClearStatus] = useState<"idle" | "working" | "done">("idle");
  const viewRef = useRef<HTMLDivElement>(null);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    if (!q) return lines;
    return lines.filter((l) => l.text.toLowerCase().includes(q));
  }, [lines, search]);

  // Auto-scroll to the bottom when new lines arrive AND autoScroll is enabled.
  // We watch `filtered` (not `lines`) so the scroll also pins when the user
  // clears the search filter and the full list suddenly expands.
  useEffect(() => {
    if (!autoScroll) return;
    const el = viewRef.current;
    if (!el) return;
    el.scrollTop = el.scrollHeight;
  }, [filtered, autoScroll]);

  // If the user scrolls up manually, disable auto-scroll so they can read
  // history without the view snapping back to the bottom on the next poll.
  // Re-enables if they scroll back to within ~32px of the bottom.
  const handleScroll = () => {
    const el = viewRef.current;
    if (!el) return;
    const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 32;
    if (!atBottom && autoScroll) {
      setAutoScroll(false);
    } else if (atBottom && !autoScroll) {
      setAutoScroll(true);
    }
  };

  const tsRegex = /^(\[[^\]]+\])/;

  const handleOpenFile = async () => {
    if (!onOpenFile) return;
    setOpenStatus("working");
    setOpenError("");
    try {
      await onOpenFile();
      // Don't flip to "done" — the editor opening IS the success signal.
      // Just reset back to idle after a short delay so the button stops
      // showing the spinner.
      window.setTimeout(() => setOpenStatus("idle"), 800);
    } catch (e) {
      setOpenStatus("error");
      setOpenError(e instanceof Error ? e.message : String(e));
      window.setTimeout(() => setOpenStatus("idle"), 4000);
    }
  };

  const handleClear = async () => {
    if (!onClear) return;
    setClearStatus("working");
    try {
      await onClear();
      setClearStatus("done");
      window.setTimeout(() => setClearStatus("idle"), 1200);
    } catch {
      setClearStatus("idle");
    }
  };

  return (
    <div className={`tab-content${active ? " active" : ""}`} id="tab-log">
      <div className="log-toolbar">
        <input
          className="search"
          placeholder={t("log.filterPlaceholder", locale)}
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        <button
          className="btn"
          title="Wipe ScreamAPI.log on disk"
          onClick={handleClear}
          disabled={clearStatus === "working"}
        >
          {clearStatus === "working" ? t("log.clearing", locale) : clearStatus === "done" ? t("log.cleared", locale) : t("log.clear", locale)}
        </button>
        <button
          className={`btn${autoScroll ? " active" : ""}`}
          onClick={() => setAutoScroll((v) => !v)}
          title="Pin the log view to the bottom when new lines arrive"
        >
          {t("log.autoScroll", locale)}
        </button>
        <button
          className="btn"
          title="Open ScreamAPI.log in your default .log editor"
          onClick={handleOpenFile}
          disabled={openStatus === "working" || !logPath}
        >
          {openStatus === "working" ? t("log.opening", locale) : t("log.openFile", locale)}
        </button>
        {openStatus === "error" && (
          <span
            className="log-toolbar-error"
            title={openError}
            style={{
              fontSize: 11,
              color: "var(--accent-warn, #f0b940)",
              marginLeft: 8,
              maxWidth: 280,
              overflow: "hidden",
              textOverflow: "ellipsis",
              whiteSpace: "nowrap",
            }}
          >
            ⚠ {openError}
          </span>
        )}
        {!logPath && connected && (
          <span
            style={{
              fontSize: 11,
              color: "var(--text-dim)",
              marginLeft: 8,
              fontStyle: "italic",
            }}
          >
            {t("log.noLogPath", locale)}
          </span>
        )}
      </div>

      <div
        className="log-view"
        id="logView"
        ref={viewRef}
        onScroll={handleScroll}
      >
        {loading && (
          <div className="log-line">
            <span className="lvl-info">{t("log.connecting", locale)}</span>
          </div>
        )}

        {!loading && lines.length === 0 && !connected && (
          <div className="log-line">
            <span className="lvl-warn">
              {t("log.notConnected", locale)}
            </span>
          </div>
        )}

        {!loading && lines.length === 0 && connected && logPath && (
          <div className="log-line">
            <span className="lvl-info">
              📄 Log file: {logPath} — {t("log.noLines", locale).toLowerCase()}
            </span>
          </div>
        )}

        {!loading && lines.length === 0 && connected && !logPath && (
          <div className="log-line">
            <span className="lvl-info">{t("log.noPathYet", locale)}</span>
          </div>
        )}

        {filtered.map((line, idx) => {
          const m = line.text.match(tsRegex);
          const ts = m ? m[0] : "";
          const rest = m ? line.text.replace(ts, "") : line.text;
          return (
            <div className="log-line" key={idx}>
              <span className="ts">{ts}</span>{" "}
              <span className={line.cls}>{rest}</span>
            </div>
          );
        })}

        {lines.length > 0 && filtered.length === 0 && (
          <div className="log-line">
            <span className="lvl-info">{t("log.noFilterMatch", locale)}</span>
          </div>
        )}
      </div>
    </div>
  );
}
