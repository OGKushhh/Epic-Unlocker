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
import { t, type Locale } from "../../i18n";

interface DlcTabProps {
  active: boolean;
  dlc: DlcEntry[];
  /** Last-seen `GetEntitlementsCount: N` from the log. -1 = unknown. */
  entitlementCount?: number;
  loading?: boolean;
  connected?: boolean;
  locale?: Locale;
}

export default function DlcTab({
  active,
  dlc,
  entitlementCount,
  loading = false,
  connected = true,
  locale = "en",
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
          <strong>{totalQueried}</strong> {t("dlc.dlcsQueried", locale)}
        </span>
        <span>
          <strong>{totalOwned}</strong> {t("dlc.owned", locale)}
        </span>
        <span>
          {t("dlc.catalog", locale)}: <strong>{totalQueried}</strong> {t("dlc.titles", locale)}
        </span>
        <span>
          {t("dlc.entitlements", locale)}: <strong>{ecDisplay}</strong>
        </span>
      </div>
      <div className="dlc-table-wrap">
        {loading && (
          <EmptyState icon="⏳" title={t("dlc.emptyConnecting", locale)} body={t("dlc.emptyConnectingBody", locale)} />
        )}
        {!loading && data.length === 0 && !connected && (
          <EmptyState
            icon="🔌"
            title={t("dlc.emptyNotConnected", locale)}
            body={t("dlc.emptyNotConnectedBody", locale)}
          />
        )}
        {!loading && data.length === 0 && connected && (
          <EmptyState icon="📭" title={t("dlc.emptyNoDlc", locale)} body={t("dlc.emptyNoDlcBody", locale)} />
        )}
        {!loading && data.length > 0 && (
          <table className="dlc-table">
            <thead>
              <tr>
                <th>{t("dlc.itemId", locale)}</th>
                <th>{t("dlc.tableTitle", locale)}</th>
                <th style={{ textAlign: "center" }}>{t("dlc.timesQueried", locale)}</th>
                <th style={{ textAlign: "center" }}>{t("dlc.timesOwned", locale)}</th>
                <th>{t("dlc.status", locale)}</th>
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
