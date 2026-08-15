/**
 * Statusbar — bottom status bar.
 * Connection state on left (green=Connected · red=Disconnected · yellow=Connecting),
 * with last error shown inline if disconnected.
 * Log line count on right.
 *
 * When disconnected, the last_error from the Rust backend is shown so the user
 * can see WHY (e.g. "CreateFileW failed (GetLastError=2)" means the pipe doesn't
 * exist yet — launch a game with Epic Unlocker injected).
 */

import { t, type Locale } from "../i18n";

interface StatusbarProps {
  connected?: boolean;
  loading?: boolean;
  /** Real byte size of the log file (from get_log_tail return).
   *  When provided, the statusbar formats it as KB/MB. When undefined,
   *  shows "—" (no log connected yet).
   */
  logSizeBytes?: number;
  lastError?: string | null;
  locale?: Locale;
}

/** Format a byte count as "X KB" / "X MB" / "X GB" with 1 decimal place. */
function formatBytes(bytes: number): string {
  if (bytes <= 0) return "0 KB";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
}

export default function Statusbar({
  connected = false,
  loading = false,
  logSizeBytes,
  lastError = null,
  locale = "en",
}: StatusbarProps) {
  const logSize = logSizeBytes != null ? formatBytes(logSizeBytes) : "—";
  const dotColor = loading
    ? "var(--yellow, #ffbd2e)"
    : connected
    ? "var(--green)"
    : "var(--red)";
  const dotGlow = loading
    ? "0 0 6px #ffbd2e"
    : connected
    ? "0 0 6px var(--green)"
    : "0 0 6px var(--red)";

  const label = loading
    ? t("status.connecting", locale)
    : connected
    ? t("status.connected", locale)
    : t("status.disconnected", locale);

  // When disconnected, show the underlying error inline so the user can
  // diagnose (e.g. pipe not found → no game running with Epic Unlocker).
  const errSuffix = !connected && lastError ? ` · ${lastError}` : "";

  return (
    <div className="statusbar">
      <div>
        <span
          className="dot"
          style={{ background: dotColor, boxShadow: dotGlow }}
        />
        {label}
        {errSuffix && (
          <span style={{ color: "var(--text-dim)", marginLeft: "4px" }}>
            {errSuffix}
          </span>
        )}
      </div>
      <div className="right">
        <span>{t("status.logSize", locale)}: {logSize}</span>
      </div>
    </div>
  );
}
