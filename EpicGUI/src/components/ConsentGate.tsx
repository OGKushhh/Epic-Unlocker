/**
 * ConsentGate — one-time first-launch modal for manifest sharing consent.
 *
 * Opt-out model: uploads are enabled by default. This modal informs the user
 * and lets them disable it.
 *
 * On "OK, got it" → consent=true, dismissed=true (never shows again).
 * On "Disable" → consent=false, dismissed=false on disk (gate re-shows every launch),
 *   but dismissed=true in local state so the modal closes for the current session.
 */

import { useEffect, useRef } from "react";
import { t, type Locale } from "../i18n";

interface ConsentGateProps {
  open: boolean;
  onAccept: () => void;
  onDecline: () => void;
  locale?: Locale;
}

export default function ConsentGate({
  open,
  onAccept,
  onDecline,
  locale = "en",
}: ConsentGateProps) {
  const boxRef = useRef<HTMLDivElement>(null);

  // Esc to decline + focus trap
  useEffect(() => {
    if (!open) return;
    boxRef.current?.focus();

    const handler = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        onDecline();
        return;
      }
      if (e.key !== "Tab" || !boxRef.current) return;
      const focusable = boxRef.current.querySelectorAll<HTMLElement>(
        'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
      );
      if (focusable.length === 0) return;
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [open, onDecline]);

  if (!open) return null;

  return (
    <div
      className="modal-overlay show"
      id="consentGateModal"
      onClick={() => {
        // Don't close on backdrop click — user must make a choice
      }}
    >
      <div className="modal-box" ref={boxRef} tabIndex={-1} style={{ outline: "none" }}>
        <div className="modal-title">📋 {t("consent.title", locale)}</div>
        <div className="modal-body">
          <p style={{ fontSize: "15px", lineHeight: "1.6" }}>
            {t("consent.body", locale)}
          </p>
        </div>
        <div className="modal-actions" style={{ justifyContent: "flex-end", gap: "10px" }}>
          <button
            className="btn-cancel"
            onClick={onDecline}
            style={{ opacity: 0.7 }}
          >
            {t("consent.decline", locale)}
          </button>
          <button
            className="btn-primary"
            onClick={onAccept}
            style={{
              backgroundColor: "var(--accent, #f67014)",
              color: "#fff",
              border: "none",
              borderRadius: "6px",
              padding: "8px 20px",
              cursor: "pointer",
              fontSize: "14px",
              fontWeight: 600,
            }}
          >
            {t("consent.accept", locale)}
          </button>
        </div>
      </div>
    </div>
  );
}
