/**
 * Tooltip — carbon copy of the mockup's hover tooltip.
 * Fixed-position, accent-bordered, smart edge avoidance.
 * Display:none when no content; display:block when `visible` is true.
 */

import { useEffect, useState } from "react";

export interface TooltipState {
  visible: boolean;
  title: string;
  body: string;
  x: number;
  y: number;
}

interface TooltipProps {
  state: TooltipState;
}

export default function Tooltip({ state }: TooltipProps) {
  const [pos, setPos] = useState({ left: 0, top: 0 });

  // Recompute position with viewport-edge avoidance (matches mockup logic)
  useEffect(() => {
    if (!state.visible) return;
    let left = state.x + 14;
    let top = state.y + 14;

    // Estimate tooltip size — mockup used getBoundingClientRect, we approximate.
    const estWidth = 280; // max-width
    const estHeight = 80; // approx for typical 2-line tooltip

    if (left + estWidth > window.innerWidth) {
      left = state.x - estWidth - 14;
    }
    if (top + estHeight > window.innerHeight) {
      top = state.y - estHeight - 14;
    }
    if (left < 0) left = 8;
    if (top < 0) top = 8;

    setPos({ left, top });
  }, [state.visible, state.x, state.y]);

  return (
    <div
      className="tooltip"
      id="tooltip"
      role="tooltip"
      style={{
        display: state.visible ? "block" : "none",
        left: pos.left,
        top: pos.top,
      }}
    >
      <div className="tt-title" id="ttTitle">
        {state.title}
      </div>
      <div className="tt-body" id="ttBody">
        {state.body}
      </div>
    </div>
  );
}
