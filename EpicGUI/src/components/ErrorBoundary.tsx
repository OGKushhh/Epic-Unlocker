/**
 * ErrorBoundary — catches render errors in a subtree so the rest of the app
 * (titlebar, music, etc.) keeps running. Shows a minimal inline fallback
 * with the error message and a retry button.
 */

import { Component, type ErrorInfo, type ReactNode } from "react";

interface Props {
  children: ReactNode;
}

interface State {
  hasError: boolean;
  error: Error | null;
}

export default class ErrorBoundary extends Component<Props, State> {
  constructor(props: Props) {
    super(props);
    this.state = { hasError: false, error: null };
  }

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error("[ErrorBoundary] Render error:", error, info);
  }

  render() {
    if (this.state.hasError) {
      return (
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            padding: "24px",
            background: "var(--panel, #1a1a1a)",
            color: "var(--text, #e0e0e0)",
            fontFamily: "system-ui, sans-serif",
            borderRadius: "8px",
            margin: "12px",
            flex: "1 1 auto",
          }}
        >
          <div style={{ fontSize: "32px", marginBottom: "8px" }}>💥</div>
          <div style={{ fontWeight: 600, color: "var(--red, #f44336)", marginBottom: "4px" }}>Render Error</div>
          <pre
            style={{
              background: "var(--log-bg, #0d0d0d)",
              padding: "12px",
              borderRadius: "4px",
              maxWidth: "500px",
              overflow: "auto",
              fontSize: "11px",
              color: "var(--text-dim, #aaa)",
              whiteSpace: "pre-wrap",
              margin: "0 0 8px 0",
            }}
          >
            {this.state.error?.message ?? "Unknown error"}
          </pre>
          <button
            onClick={() => this.setState({ hasError: false, error: null })}
            style={{
              padding: "6px 16px",
              backgroundColor: "var(--accent, #f67014)",
              color: "#fff",
              border: "none",
              borderRadius: "4px",
              cursor: "pointer",
              fontSize: "13px",
              fontWeight: 600,
            }}
          >
            Retry
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}
