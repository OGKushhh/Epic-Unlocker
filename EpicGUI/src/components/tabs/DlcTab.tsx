/**
 * DLC Tab — carbon copy of the mockup's DLC tab.
 * Stats line + table with sticky header + zebra rows + colored status.
 *
 * Data comes from the useGameData hook (real pipe data in production):
 *   - `dlc[]` is the merged catalog+stats view (id, title, queried, owned, status)
 *     populated from the DlcCatalog packet AND [DLC] log lines.
 *   - `entitlementCount` is the last-seen `GetEntitlementsCount: N` from the log.
 *
 * Shows empty states when no data is available.
 */

import { useMemo } from "react";
import type { DlcEntry } from "../../data/mockupData";

interface DlcTabProps {
  active: boolean;
  dlc: DlcEntry[];
  /** Last-seen `GetEntitlementsCount: N` from the log. -1 = unknown. */
  entitlementCount?: number;
  loading?: boolean;
  connected?: boolean;
}

export default function DlcTab({
  active,
  dlc,
  entitlementCount,
  loading = false,
  connected = true,
}: DlcTabProps) {
  const data = useMemo(() => dlc, [dlc]);
  const totalQueried = data.length;
  const totalOwned = data.filter((d) => d.status === "Owned").length;
  // -1 means the log hasn't yielded a GetEntitlementsCount line yet.
  // Show "?" so the user knows it's pending, not zero.
  const ecDisplay =
    entitlementCount === undefined || entitlementCount < 0 ? "?" : String(entitlementCount);

  return (
    <div className={`tab-content${active ? " active" : ""}`} id="tab-dlc">
      <div className="dlc-stats">
        <span>
          <strong>{totalQueried}</strong> DLCs queried
        </span>
        <span>
          <strong>{totalOwned}</strong> owned
        </span>
        <span>
          Catalog: <strong>{totalQueried}</strong> titles
        </span>
        <span>
          Entitlements: <strong>{ecDisplay}</strong>
        </span>
      </div>
      <div className="dlc-table-wrap">
        {loading && (
          <EmptyState icon="⏳" title="Connecting…" body="Waiting for the Epic Unlocker pipe to deliver the DLC catalog." />
        )}
        {!loading && data.length === 0 && !connected && (
          <EmptyState
            icon="🔌"
            title="Not connected"
            body="Launch a game with Epic Unlocker injected to establish the pipe connection. The DLC catalog will populate automatically."
          />
        )}
        {!loading && data.length === 0 && connected && (
          <EmptyState icon="📭" title="No DLC" body="The pipe is connected but no DLC catalog has been received yet." />
        )}
        {!loading && data.length > 0 && (
          <table className="dlc-table">
            <thead>
              <tr>
                <th>Item ID</th>
                <th>Title</th>
                <th style={{ textAlign: "center" }}>Times Queried</th>
                <th style={{ textAlign: "center" }}>Times Owned</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody id="dlcBody">
              {data.map((d) => (
                <tr key={d.id}>
                  <td>
                    <code style={{ fontSize: "12px" }}>{d.id}</code>
                  </td>
                  <td>{d.title || "—"}</td>
                  <td style={{ textAlign: "center" }}>{d.queried}</td>
                  <td style={{ textAlign: "center" }}>{d.owned}</td>
                  <td className={d.status === "Owned" ? "status-owned" : "status-not"}>
                    {d.status}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

interface EmptyStateProps {
  icon: string;
  title: string;
  body: string;
}

function EmptyState({ icon, title, body }: EmptyStateProps) {
  return (
    <div style={{ padding: "48px 32px", textAlign: "center", color: "var(--text-dim)" }}>
      <div style={{ fontSize: "48px", marginBottom: "12px" }}>{icon}</div>
      <div style={{ fontSize: "16px", fontWeight: 600, color: "var(--text)", marginBottom: "8px" }}>
        {title}
      </div>
      <div style={{ fontSize: "13px", maxWidth: "420px", margin: "0 auto", lineHeight: "1.6" }}>
        {body}
      </div>
    </div>
  );
}
