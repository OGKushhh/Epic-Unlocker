/**
 * Toast system — carbon copy of the mockup's toast queue.
 *  - Max 4 visible at once (oldest is dropped, matching mockup's `while (children > 4)` logic)
 *  - 4500ms auto-dismiss
 *  - New toasts prepend to the top (matching mockup's `container.prepend(item)`)
 */

import { useCallback, useRef, useState } from "react";

export interface Toast {
  id: number;
  title: string;
  body: string;
}

const MAX_TOASTS = 4;
const TOAST_TTL = 4500;

export function useToasts() {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const counter = useRef(0);

  const dismiss = useCallback((id: number) => {
    setToasts((prev) => prev.filter((t) => t.id !== id));
  }, []);

  const show = useCallback(
    (title: string, body: string) => {
      const id = ++counter.current;
      setToasts((prev) => {
        // Prepend new toast; cap at MAX_TOASTS by removing from the end (oldest after prep)
        const next = [{ id, title, body }, ...prev];
        return next.slice(0, MAX_TOASTS);
      });
      window.setTimeout(() => dismiss(id), TOAST_TTL);
    },
    [dismiss]
  );

  return { toasts, show, dismiss };
}
