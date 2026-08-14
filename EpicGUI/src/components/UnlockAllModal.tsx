/**
 * Unlock All Modal — confirmation dialog.
 * Includes the Exophase / TrueAchievements ban warning in a red-highlighted
 * block, plus Cancel + danger-red confirm button.
 *
 * Backdrop click closes (matches mockup's `if (e.target === this)` logic).
 */

import { useEffect } from "react";

interface UnlockAllModalProps {
  open: boolean;
  onClose: () => void;
  onConfirm: () => void;
  totalCount: number;
}

export default function UnlockAllModal({
  open,
  onClose,
  onConfirm,
  totalCount,
}: UnlockAllModalProps) {
  // Esc to close
  useEffect(() => {
    if (!open) return;
    const handler = (e: KeyboardEvent) => {
      if (e.key === "Escape") onClose();
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div
      className="modal-overlay show"
      id="unlockAllModal"
      onClick={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div className="modal-box">
        <div className="modal-title">⚠️ Unlock All Achievements</div>
        <div className="modal-body">
          <p>
            This will attempt to unlock{" "}
            <strong>all {totalCount} achievements</strong> for the current game.
          </p>
          <p style={{ marginTop: "8px" }}>
            This includes stat-gated achievements which will be force-ingested
            via the EOS stats interface.
          </p>
          <div className="warning-highlight">
            🚫 <strong>Warning:</strong> Using this on a legitimate Epic Games
            account may flag your profile on achievement tracking sites like{" "}
            <strong>Exophase</strong> or{" "}
            <strong>TrueAchievements</strong>. Your profile could be marked as
            "cheated" or banned from leaderboards.
          </div>
          <p style={{ marginTop: "10px", fontSize: "13px" }}>
            Are you sure you want to proceed?
          </p>
        </div>
        <div className="modal-actions">
          <button className="btn-cancel" onClick={onClose}>
            Cancel
          </button>
          <button
            className="btn-danger"
            onClick={() => {
              onConfirm();
              onClose();
            }}
          >
            Yes, Unlock All
          </button>
        </div>
      </div>
    </div>
  );
}
