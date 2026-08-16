# egdata API Documentation

**Base URL:** `https://api.egdata.app`  
**GraphQL URL:** `https://api.egdata.app/graphql`  
**Tech Stack:** TanStack Start (SSR + React) + Axios + TanStack React Query + gql.tada + PostgreSQL  
**Auth:** `better-auth` + Epic Games OAuth2 (for user profiles); most endpoints are public (no auth)

---

## Achievement System

### Achievement Data Model (REST)

```typescript
interface AchievementSet {
  _id: string;
  productId: string;
  sandboxId: string;
  achievementSetId: string;
  isBase: boolean;
  numProgressed: number;   // players who started this set
  numCompleted: number;    // players who completed all in this set
  achievements: Achievement[];
  lastUpdated: string | undefined;
}

interface Achievement {
  deploymentId: string;
  name: string;                    // achievement ID (e.g. "ach_kill_100_enemies")
  flavorText: string;
  hidden: boolean;                 // secret achievement
  unlockedDisplayName: string;     // name shown when unlocked
  unlockedDescription: string;
  unlockedIconId: string;
  unlockedIconLink: string;        // full URL to unlocked icon image
  lockedDisplayName: string;
  lockedDescription: string;
  lockedIconId: string;
  lockedIconLink: string;          // full URL to locked icon image
  xp: number;                      // XP value → determines rarity tier
  completedPercent: number;        // GLOBAL unlock percentage (e.g. 42.3)
  unlockDate: string;              // ISO date when data was last updated
}
```

### Achievement Rarity (XP-based, client-side)

Source: `src/lib/get-rarity.ts`

| XP Range | Rarity Tier | Color |
|----------|-------------|-------|
| 5–45 | Bronze | `--bronze-start` |
| 50–95 | Silver | `--silver-start` |
| 100–200 | Gold | `--gold-start` |
| 250+ | Platinum | `--platinum-start` |

```typescript
export const getRarity = (xp: number) => {
  if (xp >= 5 && xp <= 45) return "bronze";
  if (xp >= 50 && xp <= 95) return "silver";
  if (xp >= 100 && xp <= 200) return "gold";
  if (xp >= 250) return "platinum";
  return "unknown";
};
```

**Important:** The REST API provides `completedPercent` (global unlock %) per achievement. The GraphQL API provides `rarityPercent` for profile achievements. Both are real server-side percentages, not client-side heuristics.

### Profile Achievement Data (GraphQL `ProfileAchievement` type)

```typescript
interface ProfileAchievement {
  name: string;           // achievement ID
  displayName: string;
  description: string;
  iconUrl: string;
  rarityPercent: number;  // GLOBAL unlock % (e.g. 0.042 for 4.2%)
  xp: number;             // → maps to rarity tier via getRarity()
  sandboxId: string;      // game sandbox ID
  gameTitle: string;
  unlockedAt: DateTime;   // when this user unlocked it
}
```

---

## REST API Endpoints

### Offers

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/offers/{id}` | No | Single offer lookup |
| GET | `/offers/{id}/overview` | No | Full offer detail (price+media+igdb+features+ageRating+polls+genres+technologies) |
| GET | `/offers/{id}/achievements` | No | **Achievement sets for an offer** — returns `AchievementSet[]` |
| GET | `/offers/{id}/items` | No | Items within an offer |
| GET | `/offers/{id}/media` | No | Offer media (screenshots/videos) |
| GET | `/offers/{id}/price-history` | No | Price history chart data |
| GET | `/offers/{id}/price/fairness` | No | Regional pricing fairness score |
| GET | `/offers/{id}/price-stats` | No | Current/lowest/last discount price |
| GET | `/offers/{id}/genres` | No | Offer genres |
| GET | `/offers/{id}/age-rating` | No | Age rating info |
| GET | `/offers/{id}/builds` | No | Builds for an offer |
| GET | `/offers/{id}/assets` | No | Assets for an offer |
| GET | `/offers/{id}/igdb` | No | IGDB external data |
| GET | `/offers/{id}/hltb` | No | HowLongToBeat data |
| GET | `/offers/{id}/polls` | No | Offer polls |
| GET | `/offers/{id}/ratings` | No | Offer ratings |
| GET | `/offers/{id}/giveaways` | No | Giveaway info |
| GET | `/offers/{id}/tops` | No | Position in top collections |
| GET | `/offers/{id}/collections/{collection}` | No | Position in specific collection |
| GET | `/offers/{id}/features` | No | Offer feature flags |
| GET | `/offers/{id}/related` | No | Related offers |
| GET | `/offers/{id}/changelog/stats` | No | Changelog statistics |
| GET | `/offers/{id}/regional-price` | No | Regional pricing data |
| GET | `/offers/{id}/has-regular` | No | Prepurchase check |
| GET | `/offers/{id}/bundle` | No | Bundle composition |
| GET | `/offers/{id}/in-bundle` | No | Bundles containing this offer |
| GET | `/offers/{id}/collection` | No | Collection offers |
| GET | `/offers/{id}/reviews` | No | User reviews list |
| POST | `/offers/{id}/reviews` | Cookie | Submit review |
| PATCH | `/offers/{id}/reviews` | Cookie | Edit review |
| DELETE | `/offers/{id}/reviews` | Cookie | Delete review |
| GET | `/offers/{id}/reviews-summary` | No | Review summary stats |
| GET | `/offers/{id}/reviews/permissions` | Cookie | Review permission check |
| GET | `/offers/latest-achievements` | No | Games with latest achievements |
| GET | `/offers/latest-released` | No | Latest released offers |
| GET | `/offers/upcoming` | No | Upcoming offers |
| GET | `/offers/featured-discounts` | No | Featured discount offers |
| GET | `/offers/genre` | No | All genres |
| GET | `/offers/genres` | No | Genres (navbar) |

### Sandboxes

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/sandboxes/{id}` | No | Sandbox details |
| GET | `/sandboxes/{id}/base-game` | No | Base game for sandbox |
| GET | `/sandboxes/{id}/offers` | No | Offers in sandbox (paginated) |
| GET | `/sandboxes/{id}/items` | No | Items in sandbox (paginated) |
| GET | `/sandboxes/{id}/builds` | No | Builds in sandbox (paginated) |
| GET | `/sandboxes/{id}/assets` | No | Assets in sandbox (paginated) |
| GET | `/sandboxes/{id}/achievements` | No | **Achievements for sandbox** — returns `AchievementSet[]` |

### Items

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/items/{id}` | No | Item detail |
| GET | `/items/{id}/builds` | No | Item builds |
| GET | `/items/{id}/assets` | No | Item assets |
| GET | `/items/{id}/changelog` | No | Item changelog |

### Builds

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/builds` | No | Latest builds list |
| GET | `/builds/{id}` | No | Build detail |
| GET | `/builds/{id}/items` | No | Items in a build |
| GET | `/builds/{id}/history` | No | Build history |
| GET | `/builds/{id}/tree` | No | Build file tree |
| GET | `/builds/{id}/compare/{baseId}` | No | Build comparison |
| GET | `/builds/{id}/install-options` | No | Build install options |

### Search

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| POST | `/search` | No | V1 search |
| POST | `/search/v2/search` | No | V2 search (with historical lows) |
| GET | `/search/{hash}/count` | No | Search facet counts |
| GET | `/search/tags` | No | All searchable tags |
| GET | `/search/changelog` | No | Changelog search |
| POST | `/search/natural-language` | No | AI-powered natural language search |
| GET | `/multisearch/offers` | No | Global search offers |
| GET | `/multisearch/items` | No | Global search items |
| GET | `/multisearch/sellers` | No | Global search sellers |
| GET | `/autocomplete` | No | Autocomplete suggestions |

### Profiles & User Data

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/profiles/{id}/information` | No | User profile info |
| GET | `/profiles/{id}/games` | No | User's game library with achievements |
| GET | `/profiles/{id}/achievements/{sandbox}` | No | **Player achievements for a specific game** |
| PUT | `/profiles/{id}/refresh` | Cookie | Trigger profile data refresh |
| GET | `/profiles/{id}/refresh-status` | No | Check if refresh is available |

### Collections, Franchises, Sellers, Promotions

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/collections/{slug}` | No | Collection offers |
| GET | `/collections/{slug}/{week}` | No | Weekly collection |
| GET | `/franchises/{slug}` | No | Franchise detail |
| GET | `/sellers/{id}` | No | Seller's offers |
| GET | `/sellers/{id}/stats` | No | Seller statistics |
| GET | `/sellers/{id}/cover` | No | Seller cover offers |
| GET | `/promotions/{id}` | No | Promotion offers |
| GET | `/active-sales` | No | Active sales list |

### Stats, Free Games, Misc

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/stats/homepage` | No | Homepage stats |
| GET | `/stats/releases/yearly` | No | Yearly release chart |
| GET | `/stats/releases/monthly` | No | Monthly release chart |
| GET | `/stats/creations/yearly` | No | Yearly creation chart |
| GET | `/stats/creations/monthly` | No | Monthly creation chart |
| GET | `/free-games/mobile` | No | Mobile free games |
| GET | `/free-games/og` | No | OG free game offer ID |
| GET | `/free-games/stats` | No | Giveaway statistics |
| GET | `/countries` | No | Supported country codes |
| GET | `/regions` | No | Region data |
| GET | `/game-awards` | No | Game awards |
| GET | `/changelist` | No | Recent changelog |
| GET | `/base-game/{namespace}` | No | Base game by namespace |

---

## GraphQL API

All queries POST to `/graphql` on `api.egdata.app`.

### 1. `ProfilePage` Query

**Variables:**
```typescript
{
  id: ID!,                              // Epic account ID
  featuredAchievementLimit: Int!,       // e.g. 8
  featuredGameLimit: Int!,             // e.g. 6
  recentActivityLimit: Int!,           // e.g. 12
  recentActivityPage: Int!,            // e.g. 1
  gameLimit: Int!,                     // e.g. 12
  gamePage: Int!,                      // e.g. 1
  gameFilter: ProfileGameFilter!,      // ALL | COMPLETED | NEAR_PLATINUM | IN_PROGRESS | PLATINUM
  gameSort: ProfileGameSort!,          // COMPLETION | ALPHABETICAL | XP | ACHIEVEMENTS
  achievementLimit: Int!,              // e.g. 25
  achievementPage: Int!                // e.g. 1
}
```

**Key fields returned:**
```graphql
profile {
  accountId, displayName, avatar { small, medium, large }
  linkedAccounts, creationDate, reviewsCount
  highlights { level, totalXP, totalGames, totalAchievements, totalPlatinums }
  heroGame { sandboxId, title, imageUrl, completionPercent }
  featuredAchievements { name, displayName, description, iconUrl, rarityPercent, xp, sandboxId, gameTitle, unlockedAt }
  featuredGames { sandboxId, title, imageUrl, completionPercent, unlocked, total, earnedXP, totalXP, hasPlatinum,
    rarestAchievements { name, displayName, description, iconUrl, rarityPercent, xp, sandboxId, gameTitle, unlockedAt }
  }
  recentActivity { type, sandboxId, gameTitle, achievementName, achievementIconUrl, occurredAt }
  games { elements { ... same as featuredGames }, total, page, limit }
  achievements { elements { ... same as featuredAchievements }, total, page, limit }
}
```

### 2. `OfferPage` Query
**Variables:** `{ id: ID!, country: String!, locale: String }`  
Full offer detail including price(country), franchises, giveaways, items, builds, technologies.

### 3. `SandboxHub` Query
**Variables:** `{ id: ID!, country: String!, offerLimit: Int, updateLimit: Int }`  
Full sandbox: namespace, title, developer, publisher, platforms, ageRating, price, keyImages, genres, stats, achievements (sets/total/baseTotal/xp), primaryItem, primaryOffer, featuredOffers, recentBuilds, recentChanges.

### 4. `OfferOnly` Query
**Variables:** `{ id: ID!, locale: String }`  
Same as OfferPage but without country-dependent data (no price/franchises).

---

## External Epic Games APIs

### Epic OAuth2 Flow

| Step | URL | Method | Description |
|------|-----|--------|-------------|
| 1 | `https://www.epicgames.com/id/authorize` | Redirect | Initiate OAuth (params: `client_id`, `response_type=code`, `scope=basic_profile`, `redirect_uri`) |
| 2 | `https://api.epicgames.dev/epic/oauth/v2/token` | POST | Exchange code for tokens (Basic auth, body: `grant_type=authorization_code`) |
| 3 | `https://api.epicgames.dev/epic/oauth/v2/userInfo` | GET | Fetch user info (Bearer token) |
| 4 | `https://api.epicgames.dev/epic/id/v2/accounts?accountId=...` | GET | Account lookup (Bearer token) |

### OIDC Discovery
- Issuer: `https://api.epicgames.dev/epic/oauth/v2`
- JWKS: `https://api.epicgames.dev/epic/oauth/v2/.well-known/jwks.json`

---

## Key Findings for Epic Unlocker (G4 Rarity Feature)

### What egdata proves about Epic's achievement APIs:

1. **`completedPercent` IS available** — egdata's REST API `/offers/{id}/achievements` and `/sandboxes/{id}/achievements` both return per-achievement `completedPercent` (global unlock %). This is the **real server-side rarity data** we need.

2. **`rarityPercent` IS available via GraphQL** — The `ProfileAchievement` GraphQL type has `rarityPercent: Float`. This is per-achievement global rarity as a float.

3. **`numProgressed` and `numCompleted` are on `AchievementSet`** — These are per-set counts (not per-achievement). The woctezuma repo noted these were "plugged" on Jan 22, 2025, but egdata still has them in its type definitions — they may be partially available through the egdata backend's own scraping pipeline.

4. **`xp` → rarity tier mapping is standard** — Epic assigns XP values 5-45 (Bronze), 50-95 (Silver), 100-200 (Gold), 250+ (Platinum). This is the same system PlayStation uses. egdata derives rarity purely from XP client-side.

5. **The data source** — egdata's backend scrapes the Epic GraphQL API (`graphql.epicgames.com/ue/graphql`) using the `Achievement` persisted query, then serves it through its own REST/GraphQL API. The `completedPercent` and `rarityPercent` values come from Epic's servers.

### Two approaches for Epic Unlocker:

| Approach | Source | Pros | Cons |
|----------|--------|------|------|
| **A. Call egdata API** | `GET /sandboxes/{id}/achievements` | No need to deal with Epic GraphQL directly; `completedPercent` already computed; free, public, CORS-friendly | Depends on third-party service; egdata may rate-limit; sandboxId must be known |
| **B. Call Epic GraphQL directly** | `POST https://graphql.epicgames.com/ue/graphql` | Direct from source; no third-party dependency; `rarity { percent }` + `tier { name, hexColor, min, max }` available | Must maintain persisted query hash; Epic could change/break it; the Jan 2025 shutdown risk |

**Recommendation:** Use **egdata API as primary, Epic GraphQL as fallback**. The egdata API is specifically built for this use case, is publicly accessible, and already transforms the data into a clean format. If egdata is down, fall back to the direct Epic GraphQL call.
