// rarity.rs
// Achievement rarity fetching: egdata API (primary) + Epic GraphQL (fallback).
//
// G4: Fetches per-achievement rarity data (completedPercent, XP→tier) from
// external APIs and merges it into the GUI's Achievement structs.
//
// Strategy:
//   1. Primary:  GET https://api.egdata.app/sandboxes/{sandboxId}/achievements
//      - Returns AchievementSet[] with per-achievement completedPercent + xp
//      - Public, no auth, CORS-friendly
//   2. Fallback: POST https://graphql.epicgames.com/ue/graphql
//      - Uses persisted query hash for Achievement definitions
//      - Returns rarity { percent } + tier { name, hexColor, min, max }
//      - May break if Epic changes the hash (Jan 2025 risk)
//   3. Cache results for the session (HashMap by sandbox_id)

use std::collections::HashMap;
use serde::{Deserialize, Serialize};

// ── Rarity tier (derived from XP, same as egdata/PlayStation) ───────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RarityTier {
    Bronze,
    Silver,
    Gold,
    Platinum,
    Unknown,
}

impl RarityTier {
    /// Derive tier from XP value (same mapping as egdata's get-rarity.ts).
    pub fn from_xp(xp: i32) -> Self {
        if xp >= 5 && xp <= 45 { RarityTier::Bronze }
        else if xp >= 50 && xp <= 95 { RarityTier::Silver }
        else if xp >= 100 && xp <= 200 { RarityTier::Gold }
        else if xp >= 250 { RarityTier::Platinum }
        else { RarityTier::Unknown }
    }

    /// Hex color for UI rendering.
    #[allow(dead_code)]
    pub fn color(&self) -> &'static str {
        match self {
            RarityTier::Bronze => "#CD7F32",
            RarityTier::Silver => "#C0C0C0",
            RarityTier::Gold => "#FFD700",
            RarityTier::Platinum => "#E5E4E2",
            RarityTier::Unknown => "#808080",
        }
    }

    /// Display label.
    #[allow(dead_code)]
    pub fn label(&self) -> &'static str {
        match self {
            RarityTier::Bronze => "Bronze",
            RarityTier::Silver => "Silver",
            RarityTier::Gold => "Gold",
            RarityTier::Platinum => "Platinum",
            RarityTier::Unknown => "Unknown",
        }
    }
}

// ── Per-achievement rarity data (merged into GUI Achievement struct) ────────

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AchievementRarity {
    /// Achievement ID (matches Achievement.id from pipe protocol).
    pub id: String,
    /// Global unlock percentage (0.0–100.0 from egdata, 0.0–100.0 from GraphQL).
    pub completed_percent: f32,
    /// XP value from Epic (determines rarity tier).
    pub xp: i32,
    /// Derived rarity tier.
    pub tier: RarityTier,
}

// ── egdata API response types ───────────────────────────────────────────────

#[derive(Debug, Deserialize)]
struct EgdataAchievementSet {
    #[serde(default)]
    achievements: Vec<EgdataAchievement>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct EgdataAchievement {
    name: String,              // achievement ID (e.g. "ach_kill_100_enemies")
    #[serde(default)]
    completed_percent: f64,    // global unlock % (e.g. 42.3)
    #[serde(default)]
    xp: i32,                   // → determines rarity tier
}

// ── Epic GraphQL API response types (minimal, for fallback) ─────────────────

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct EpicGraphQLResponse {
    data: Option<EpicGraphQLData>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct EpicGraphQLData {
    achievements: Option<EpicGraphQLAchievements>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct EpicGraphQLAchievements {
    elements: Option<Vec<EpicGraphQLAchElement>>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct EpicGraphQLAchElement {
    name: String,
    rarity: Option<EpicGraphQLRarity>,
    xp: Option<i32>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct EpicGraphQLRarity {
    percent: Option<f64>,
}

// ── Cache ───────────────────────────────────────────────────────────────────

/// In-memory cache keyed by sandbox_id. Persists for the session.
pub type RarityCache = HashMap<String, Vec<AchievementRarity>>;

// ── Fetch from egdata (primary) ─────────────────────────────────────────────

/// Fetch achievement rarity data from the egdata API.
/// Returns a vec of AchievementRarity matched by achievement name (ID).
pub async fn fetch_from_egdata(
    sandbox_id: &str,
    client: &reqwest::Client,
) -> Result<Vec<AchievementRarity>, String> {
    let url = format!("https://api.egdata.app/sandboxes/{}/achievements", sandbox_id);
    log::info!("[G4] Fetching rarity from egdata: {}", url);

    let resp = client.get(&url)
        .timeout(std::time::Duration::from_secs(10))
        .send()
        .await
        .map_err(|e| format!("egdata request failed: {e}"))?;

    if !resp.status().is_success() {
        return Err(format!("egdata returned HTTP {}", resp.status()));
    }

    let sets: Vec<EgdataAchievementSet> = resp.json()
        .await
        .map_err(|e| format!("egdata JSON parse failed: {e}"))?;

    let mut results = Vec::new();
    for set in sets {
        for ach in set.achievements {
            let tier = RarityTier::from_xp(ach.xp);
            results.push(AchievementRarity {
                id: ach.name,
                completed_percent: ach.completed_percent as f32,
                xp: ach.xp,
                tier,
            });
        }
    }

    log::info!("[G4] egdata returned {} achievements with rarity data", results.len());
    Ok(results)
}

// ── Fetch from Epic GraphQL (fallback) ──────────────────────────────────────

/// Fetch achievement rarity data from Epic's public GraphQL API.
/// Uses the persisted query hash for the Achievement query.
pub async fn fetch_from_epic_graphql(
    sandbox_id: &str,
    client: &reqwest::Client,
) -> Result<Vec<AchievementRarity>, String> {
    // The persisted query hash for achievement definitions with rarity.
    // Source: woctezuma/epic-games-achievements repo + egdata reverse-engineering.
    // This hash may change if Epic updates their schema (Jan 2025 risk).
    let query_hash = "7d54399ae06c49b5a65f19a06d7c6c5f2e4fb5e9";

    let url = "https://graphql.epicgames.com/ue/graphql";
    let body = serde_json::json!({
        "operationName": "Achievement",
        "variables": {
            "namespace": sandbox_id,
            "locale": "en",
            "country": "US"
        },
        "persistedQuery": {
            "version": 1,
            "sha256Hash": query_hash
        }
    });

    log::info!("[G4] Fetching rarity from Epic GraphQL for namespace: {}", sandbox_id);

    let resp = client.post(url)
        .timeout(std::time::Duration::from_secs(10))
        .json(&body)
        .send()
        .await
        .map_err(|e| format!("Epic GraphQL request failed: {e}"))?;

    if !resp.status().is_success() {
        return Err(format!("Epic GraphQL returned HTTP {}", resp.status()));
    }

    let gql_resp: EpicGraphQLResponse = resp.json()
        .await
        .map_err(|e| format!("Epic GraphQL JSON parse failed: {e}"))?;

    let mut results = Vec::new();

    if let Some(data) = gql_resp.data {
        if let Some(achievements) = data.achievements {
            if let Some(elements) = achievements.elements {
                for el in elements {
                    let percent = el.rarity
                        .as_ref()
                        .and_then(|r| r.percent)
                        .unwrap_or(0.0) as f32;
                    let xp = el.xp.unwrap_or(0);
                    let tier = RarityTier::from_xp(xp);
                    results.push(AchievementRarity {
                        id: el.name,
                        completed_percent: percent,
                        xp,
                        tier,
                    });
                }
            }
        }
    }

    log::info!("[G4] Epic GraphQL returned {} achievements with rarity data", results.len());
    Ok(results)
}

// ── Combined fetch with fallback ────────────────────────────────────────────

/// Fetch achievement rarity: try egdata first, fall back to Epic GraphQL.
/// Results are cached by sandbox_id for the session.
pub async fn fetch_rarity(
    sandbox_id: &str,
    client: &reqwest::Client,
    cache: &mut RarityCache,
) -> Result<Vec<AchievementRarity>, String> {
    // Check cache first
    if let Some(cached) = cache.get(sandbox_id) {
        log::debug!("[G4] Rarity cache hit for sandbox {}", sandbox_id);
        return Ok(cached.clone());
    }

    // Primary: egdata
    match fetch_from_egdata(sandbox_id, client).await {
        Ok(results) => {
            cache.insert(sandbox_id.to_string(), results.clone());
            Ok(results)
        }
        Err(e1) => {
            log::warn!("[G4] egdata failed: {e1}, trying Epic GraphQL fallback");
            // Fallback: Epic GraphQL
            match fetch_from_epic_graphql(sandbox_id, client).await {
                Ok(results) => {
                    cache.insert(sandbox_id.to_string(), results.clone());
                    Ok(results)
                }
                Err(e2) => {
                    Err(format!("Both API sources failed — egdata: {e1}, Epic GraphQL: {e2}"))
                }
            }
        }
    }
}
