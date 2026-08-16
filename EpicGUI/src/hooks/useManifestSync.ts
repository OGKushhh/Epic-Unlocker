/**
 * useManifestSync — manages manifest scanning and uploading.
 *
 * Flow:
 *   1. Check consent state from backend
 *   2. If consent=true, scan manifests on disk
 *   3. Filter out already-uploaded hashes
 *   4. Upload new manifests (smart uploader: batch/sequential/chunked)
 *   5. Listen to progress events from Rust
 *   6. Toast feedback for each step
 */

import { useCallback, useEffect, useRef, useState } from "react";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import {
  getManifestConsent,
  setManifestConsent,
  scanManifests,
  uploadManifests,
  getUploadedManifestHashes,
} from "../lib/api";
import type { ManifestConsentState, ScannedManifest } from "../types";
import { t, type Locale } from "../i18n";

// ── Progress type (mirrors Rust UploadProgress) ──────────────────────────────

export interface UploadProgressEvent {
  totalFiles: number;
  completedFiles: number;
  currentFile: string;
  currentFilePercent: number; // 0-100 for current file
  overallPercent: number;     // 0-100 overall
  status: string;             // "uploading" | "chunking" | "done" | "error"
}

// ── Hook options ─────────────────────────────────────────────────────────────

interface ManifestSyncOptions {
  locale?: Locale;
  showToast: (title: string, body: string) => void;
}

export function useManifestSync({ locale = "en", showToast }: ManifestSyncOptions) {
  const [consentState, setConsentState] = useState<ManifestConsentState>({
    consent: true,
    dismissed: false,
  });
  const [syncing, setSyncing] = useState(false);
  const [scannedManifests, setScannedManifests] = useState<ScannedManifest[]>([]);
  const [uploadProgress, setUploadProgress] = useState<UploadProgressEvent | null>(null);
  const consentLoaded = useRef(false);

  // ── Listen to progress events from Rust ────────────────────────────────
  useEffect(() => {
    let unlisten: UnlistenFn | null = null;

    (async () => {
      unlisten = await listen<UploadProgressEvent>("manifest-upload-progress", (event) => {
        setUploadProgress(event.payload);
        console.log("📊 Upload progress:", event.payload);
      });
    })();

    return () => {
      unlisten?.();
    };
  }, []);

  // Load consent state on mount
  useEffect(() => {
    getManifestConsent()
      .then((state) => {
        setConsentState(state);
        consentLoaded.current = true;
      })
      .catch((e) => console.error("[ManifestSync] Failed to load consent:", e));
  }, []);

  // Auto-sync on first consent load if consent is true
  useEffect(() => {
    if (!consentLoaded.current) return;
    if (!consentState.consent) return;
    // Run once on mount when consent is enabled
    syncManifests();
  }, [consentLoaded.current, consentState.consent]);

  const syncManifests = useCallback(async () => {
    console.log("🔍 syncManifests called");
    if (syncing) return;
    setSyncing(true);
    setUploadProgress(null);

    try {
      // Check consent
      const consent = await getManifestConsent();
      console.log("📋 Consent:", consent);
      if (!consent.consent) {
        showToast(`🔒 ${t("toast.manifestNoConsent", locale)}`, "");
        setSyncing(false);
        return;
      }

      // Scan
      showToast(`🔍 ${t("toast.manifestScanning", locale)}`, "");
      const manifests = await scanManifests();
      console.log("📁 Scanned manifests:", manifests);
      setScannedManifests(manifests);

      if (manifests.length === 0) {
        showToast(`📭 ${t("toast.manifestScanFound", locale).replace("{count}", "0")}`, "");
        setSyncing(false);
        return;
      }

      showToast(
        `📋 ${t("toast.manifestScanFound", locale).replace("{count}", String(manifests.length))}`,
        ""
      );

      // Check which are already uploaded
      const uploadedHashes = new Set(await getUploadedManifestHashes());
      console.log("📋 Already uploaded:", uploadedHashes);
      const newManifests = manifests.filter((m) => !uploadedHashes.has(m.sha256));
      console.log("📤 New manifests to upload:", newManifests);

      if (newManifests.length === 0) {
        showToast(`✅ ${t("toast.manifestUploadSkipped", locale)}`, "");
        setSyncing(false);
        return;
      }

      // Upload — the Rust smart uploader emits progress events
      const total = newManifests.length;
      const filePaths = newManifests.map((m) => m.filePath);
      console.log("📤 Sending to Rust:", filePaths);
      showToast(
        `📤 ${t("toast.manifestUploading", locale)}`,
        `0/${total}`
      );
      const results = await uploadManifests(filePaths);
      console.log("📊 Upload results:", results);

      const ok = results.filter((r) => r.status === "ok").length;
      const skipped = results.filter((r) => r.status === "skipped").length;
      const failed = results.filter((r) => r.status === "error").length;

      if (ok > 0 && failed === 0) {
        showToast(
          `✅ ${t("toast.manifestUploadDone", locale).replace("{count}", String(ok))}`,
          skipped > 0 ? `${skipped} skipped` : ""
        );
      } else if (ok > 0 && failed > 0) {
        showToast(
          `⚠️ ${t("toast.manifestUploadDone", locale).replace("{count}", String(ok))}`,
          `${failed} failed, ${skipped} skipped`
        );
      } else if (failed > 0) {
        showToast(
          `❌ ${t("toast.manifestUploadFailed", locale)}`,
          `${failed} of ${total} failed (will retry next sync)`
        );
      } else {
        showToast(`✅ ${t("toast.manifestUploadSkipped", locale)}`, "");
      }
    } catch (e) {
      console.error("❌ syncManifests error:", e);
      const msg = e instanceof Error ? e.message : String(e);
      showToast(`❌ ${t("toast.manifestUploadFailed", locale)}`, msg);
    } finally {
      setSyncing(false);
    }
  }, [locale, showToast, syncing]);

  const acceptConsent = useCallback(async () => {
    await setManifestConsent(true, true);
    setConsentState({ consent: true, dismissed: true });
  }, []);

  const declineConsent = useCallback(async () => {
    // Persist dismissed=false on disk so the gate re-shows on every launch.
    // Set dismissed=true locally so the modal closes for THIS session.
    await setManifestConsent(false, false);
    setConsentState({ consent: false, dismissed: true });
  }, []);

  const toggleConsent = useCallback(async (enabled: boolean) => {
    // When disabling: reset dismissed so modal shows again on next launch
    // When enabling: keep dismissed true (they already know)
    const dismissed = enabled ? true : false;
    await setManifestConsent(enabled, dismissed);
    setConsentState({ consent: enabled, dismissed });
  }, []);

  // Refresh consent state (e.g. after settings change)
  const refreshConsent = useCallback(async () => {
    const state = await getManifestConsent();
    setConsentState(state);
    return state;
  }, []);

  return {
    consentState,
    syncing,
    scannedManifests,
    uploadProgress,
    syncManifests,
    acceptConsent,
    declineConsent,
    toggleConsent,
    refreshConsent,
  };
}
