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

interface StatusbarProps {
  connected?: boolean;
  loading?: boolean;
  logSize?: string;
  lastError?: string | null;
}

export default function Statusbar({
  connected = false,
  loading = false,
  logSize = "0 KB",
  lastError = null,
}: StatusbarProps) {
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
    ? "Connecting…"
    : connected
    ? "Connected"
    : "Disconnected";

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
        <span>Log size: {logSize}</span>
      </div>
    </div>
  );
}
