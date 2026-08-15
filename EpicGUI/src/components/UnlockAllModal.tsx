/**
 * Unlock All Modal — confirmation dialog.
 * Includes the Exophase / TrueAchievements ban warning in a red-highlighted
 * block, plus Cancel + danger-red confirm button.
 *
 * Backdrop click closes (matches mockup's `if (e.target === this)` logic).
 */

import { useEffect } from "react";
import { t, type Locale } from "../i18n";

interface UnlockAllModalProps {
  open: boolean;
  onClose: () => void;
  onConfirm: () => void;
  totalCount: number;
  locale?: Locale;
}

export default function UnlockAllModal({
  open,
  onClose,
  onConfirm,
  totalCount,
  locale = "en",
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
        <div className="modal-title">⚠️ {t("modal.unlockTitle", locale)}</div>
        <div className="modal-body">
          <p>
            {t("modal.unlockBody1", locale)}{" "}
            <strong>{t("modal.unlockBody2", locale)} {totalCount} {t("modal.unlockBody3", locale)}</strong>
          </p>
          <p style={{ marginTop: "8px" }}>
            {t("modal.statGated", locale)}
          </p>

          {/* Warning Box – uniform border, no top stripe, site names plain bold */}
          <div
            style={{
              backgroundColor: "#fff0f0",
              border: "2px solid #c62828",
              borderRadius: "8px",
              padding: "16px",
              margin: "16px 0",
              boxShadow: "0 2px 8px rgba(198, 40, 40, 0.15)",
            }}
          >
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: "10px",
                marginBottom: "8px",
              }}
            >
              <span style={{ fontSize: "28px", lineHeight: 1 }}>⛔</span>
              <strong
                style={{
                  color: "#b71c1c",
                  fontSize: "18px",
                  letterSpacing: "0.5px",
                }}
              >
                {t("modal.dangerTitle", locale)}
              </strong>
            </div>
            <p style={{ margin: 0, color: "#6d1a1a", fontSize: "15px" }}>
              {t("modal.dangerBody", locale)}{" "}
              <strong style={{ color: "#b71c1c" }}>Exophase</strong> {t("modal.dangerOr", locale)} <strong style={{ color: "#b71c1c" }}>TrueAchievements</strong>.
              {t("modal.dangerMarked", locale)}{" "}
              {t("modal.dangerBanned", locale)}
            </p>
          </div>

          <p style={{ marginTop: "10px", fontSize: "13px" }}>
            {t("modal.areYouSure", locale)}
          </p>
        </div>
        <div className="modal-actions">
          <button className="btn-cancel" onClick={onClose}>
            {t("modal.cancel", locale)}
          </button>
          <button
            className="btn-danger"
            onClick={() => {
              onConfirm();
              onClose();
            }}
          >
            {t("modal.confirm", locale)}
          </button>
        </div>
      </div>
    </div>
  );
}
