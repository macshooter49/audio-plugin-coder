# Waves Crate Licensing Server — Spec

**Status:** SPEC, ready to build. Nothing here is deployed.
**Owner:** Max. **Host:** Vercel (serverless functions + Cron) · Postgres (Neon / Vercel Postgres) · Upstash Redis (rate limits) · Resend (transactional email).
**Client side:** `plugins/Terrain/.ideas/licensing-design.md` and `plugins/Terrain/Source/License/`.
**Scope:** every Waves Crate plugin, starting with Terrain. Nothing in the data model is Terrain-specific except one row in `products`.

---

## 1. What it does, in one paragraph

When a Shopify order is paid, Shopify calls our webhook. We verify the HMAC, create a
**purchase** for the buyer's email and generate **three authorization codes** for it
(one per computer), then email the codes to the buyer and show them in their Waves
Crate account. In the plugin, the buyer types that email (`/v1/lookup` tells them we
found the purchase), pastes one code (`/v1/activate`), and gets back a **licence file
signed with Ed25519**. The private key lives only on this server. The plugin verifies
the licence offline with the public key it ships with. Signing out calls
`/v1/deactivate`, which frees that computer's code for another machine. Separately, and
only for users who opted in, the plugin can send anonymous usage events to
`/v1/events`, which a daily job summarises.

```
Shopify ──orders/paid (HMAC)──▶ /api/shopify/webhook ──▶ purchases + 3 codes ──▶ email to buyer
Plugin  ──/v1/lookup {email}────────────────────────────▶ found / not_found (+ seat counts)
Plugin  ──/v1/activate {email, code, machineId}─────────▶ signed Licence.json (Ed25519)
Plugin  ──/v1/deactivate {licenseId, activationId, …}───▶ seat freed
Plugin  ──/v1/events (opt-in only)──────────────────────▶ events ──daily cron──▶ daily_stats + summary email
Browser ──/account/licenses (magic link)────────────────▶ codes + computers, "Free this computer"
Browser ──/admin (allow-listed)─────────────────────────▶ search, grant, revoke, reset seats
```

---

## 2. Environment variables (Vercel → Settings → Environment Variables)

| Name | What it is |
|---|---|
| `DATABASE_URL` | Postgres connection string (pooled). |
| `SHOPIFY_WEBHOOK_SECRET` | The app's webhook signing secret (Shopify admin → Settings → Notifications → Webhooks, or the custom app's API secret). |
| `SHOPIFY_SHOP_DOMAIN` | `your-shop.myshopify.com`; webhooks from any other shop are rejected. |
| `LICENSE_SIGNING_KEY` | Ed25519 **private** key, PKCS#8 PEM. Server only. Never logged, never returned. |
| `LICENSE_KEY_ID` | Short id of the signing key (e.g. `k1`), written into every licence for rotation. |
| `UPSTASH_REDIS_REST_URL` / `UPSTASH_REDIS_REST_TOKEN` | Rate-limit store. |
| `RESEND_API_KEY`, `MAIL_FROM` | Code-delivery and magic-link email. |
| `ADMIN_EMAILS` | Comma-separated allow-list for `/admin`. |
| `SESSION_SECRET` | 32+ random bytes; signs account/admin session cookies. |
| `TELEMETRY_ENABLED` | `true` to accept `/v1/events`. Default `false`. |
| `SUMMARY_TO` | Where the daily summary email goes. |
| `CRON_SECRET` | Vercel sends it as `Authorization: Bearer …` to the cron route; reject anything else. |

Generate the key pair once, offline:

```bash
openssl genpkey -algorithm ed25519 -out terrain-signing.pem          # private -> LICENSE_SIGNING_KEY
openssl pkey -in terrain-signing.pem -pubout -outform DER | tail -c 32 | base64
# ^ 32-byte raw public key -> EmbeddedKey::kEd25519PublicKeyBase64 in SignatureVerifier.h
```

Keep an offline backup of `terrain-signing.pem` (password manager / encrypted USB). Losing
it means shipping a plugin update with a new public key.

---

## 3. Data model (Postgres)

```sql
create extension if not exists citext;
create extension if not exists pgcrypto;         -- gen_random_uuid()

-- One row per sellable plugin. Shared by every future Waves Crate plugin.
create table products (
  id              text primary key,              -- 'terrain'
  display_name    text not null,                 -- 'Terrain'
  code_prefix     text not null unique,          -- 'TRRN'
  major_version   int  not null default 1,
  seats_per_unit  int  not null default 3,
  shopify_product_ids bigint[] not null default '{}',   -- which Shopify products grant it
  store_url       text
);

-- One row per licence (one unit bought, or one manual grant).
create table purchases (
  id              uuid primary key default gen_random_uuid(),   -- = licenseId in Licence.json
  product_id      text not null references products(id),
  email           citext not null,               -- normalised: trimmed, lower-cased
  buyer_name      text,
  source          text not null check (source in ('shopify','manual')),
  shopify_order_id   bigint,
  shopify_line_item_id bigint,
  granted_by      citext,                        -- admin email for manual grants
  note            text,
  status          text not null default 'active' check (status in ('active','revoked')),
  created_at      timestamptz not null default now(),
  revoked_at      timestamptz
);
create index purchases_email_idx on purchases (email, product_id) where status = 'active';

-- Exactly seats_per_unit rows per purchase. One code = one computer.
create table auth_codes (
  id              uuid primary key default gen_random_uuid(),
  purchase_id     uuid not null references purchases(id) on delete cascade,
  code            text not null unique,          -- 'TRRN-7KQ2-M9XD'
  slot            int  not null,                 -- 1..3, for stable display order
  created_at      timestamptz not null default now(),
  disabled_at     timestamptz,
  unique (purchase_id, slot)
);

-- A code bound to a machine. At most ONE live activation per code.
create table activations (
  id              uuid primary key default gen_random_uuid(),   -- = activationId in Licence.json
  code_id         uuid not null references auth_codes(id) on delete cascade,
  machine_id      text not null,                 -- client's salted SHA-256 fingerprint (base64); never raw ids
  machine_label   text,                          -- optional, user-editable ("Studio Mac")
  activated_at    timestamptz not null default now(),
  last_seen_at    timestamptz not null default now(),
  released_at     timestamptz,
  released_by     text check (released_by in ('plugin','customer','admin'))
);
create unique index one_live_activation_per_code on activations (code_id) where released_at is null;

-- Shopify retries webhooks; this makes processing idempotent.
create table webhook_events (
  webhook_id      text primary key,              -- X-Shopify-Webhook-Id
  topic           text not null,
  received_at     timestamptz not null default now()
);

-- Magic-link logins for /account and /admin.
create table login_tokens (
  token_hash      text primary key,              -- sha256(token); the token itself is only in the email
  email           citext not null,
  expires_at      timestamptz not null,
  used_at         timestamptz
);

-- Opt-in, anonymous telemetry (section 8). No foreign keys to anything above, on purpose.
create table telemetry_events (
  id              bigserial primary key,
  install_id      uuid not null,                 -- random, generated on opt-in, NOT linked to a licence
  product_id      text not null,
  app_version     text not null,
  os              text not null,                 -- 'macOS 15' / 'Windows 11' (major only)
  host_kind       text,                          -- 'standalone' | 'vst3' | 'au'
  name            text not null,                 -- allow-listed event name
  props           jsonb not null default '{}',   -- allow-listed keys only
  occurred_at     timestamptz not null,          -- client time, rounded to the minute
  received_day    date not null default current_date
);
create index telemetry_day_idx on telemetry_events (received_day, product_id);

create table crash_reports (
  id              bigserial primary key,
  install_id      uuid not null,
  product_id      text not null,
  app_version     text not null,
  os              text not null,
  signature       text not null,                 -- hash of the top frames, for grouping
  frames          jsonb not null,                -- symbol names + offsets only; paths scrubbed
  occurred_at     timestamptz not null,
  received_day    date not null default current_date
);

create table admin_log (
  id              bigserial primary key,
  at              timestamptz not null default now(),
  admin_email     citext not null,
  action          text not null,                 -- 'grant' | 'revoke' | 'free' | 'resend' | 'delete_customer'
  target          text not null,                 -- email / purchase id / activation id
  note            text
);

create table daily_stats (
  day             date not null,
  product_id      text not null,
  metric          text not null,                 -- 'active_installs', 'sessions', 'median_session_min', 'event:page_open:osc', ...
  value           numeric not null,
  primary key (day, product_id, metric)
);
```

**Seed Terrain:**

```sql
insert into products (id, display_name, code_prefix, major_version, seats_per_unit, shopify_product_ids)
values ('terrain', 'Terrain', 'TRRN', 1, 3, '{<shopify product id>}');
```

**Email normalisation (everywhere):** `trim`, Unicode NFC, lower-case. Do **not** strip
Gmail dots or `+tags` — the buyer types whatever they typed at checkout, and guessing
creates support cases.

---

## 4. Authorization codes

- Format: `<PREFIX>-XXXX-XXXX`, e.g. `TRRN-7KQ2-M9XD`.
- Alphabet for the 8 random characters: `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (32 symbols, no
  `0/O/1/I`), from `crypto.randomInt`. 8 × 5 bits = 40 bits per code. A code is only valid
  together with the matching email, and activation is rate-limited (section 7), so guessing
  is not practical.
- Before matching, the server upper-cases the input, removes spaces and dashes, and maps
  `O→0`, `I→1`, `L→1`, then re-formats. (The plugin already upper-cases and formats.)
- Codes are stored in plain text because the customer's account page must show them. They
  are not secrets on their own: without the purchase email they do nothing, and the licence
  that matters is signed.

---

## 5. Shopify webhook → purchase + codes

`POST /api/shopify/webhook` — subscribe to **`orders/paid`**, **`refunds/create`**,
**`orders/cancelled`** (JSON format).

1. **Read the raw body** (disable body parsing: in a Next.js route handler use
   `await req.text()`; never re-serialise JSON before verifying).
2. **Verify:** `X-Shopify-Hmac-Sha256` must equal
   `base64(HMAC_SHA256(SHOPIFY_WEBHOOK_SECRET, rawBody))`. Compare with
   `crypto.timingSafeEqual` on equal-length buffers. Reject with `401` otherwise.
3. **Check the shop:** `X-Shopify-Shop-Domain === SHOPIFY_SHOP_DOMAIN`, else `401`.
4. **Deduplicate:** `insert into webhook_events (webhook_id, topic) … on conflict do nothing`;
   if nothing was inserted, return `200` immediately (Shopify retry).
5. **`orders/paid`:** for each `line_items[]` whose `product_id` maps to a row in `products`,
   create `quantity` purchases, each with `seats_per_unit` codes, in **one transaction**.
   `email` = `order.email` (fall back to `order.customer.email`), `buyer_name` =
   `order.customer.first_name + last_name`.
   Then send the code email (below) and return `200`. Do the work quickly: Shopify
   times out after 5 s and retries. If email sending fails, still return `200` (codes are
   safe in the DB and on the account page); log and retry from a queue/cron.
6. **`refunds/create` / `orders/cancelled`:** if the refund covers a licensed line item,
   set that purchase `status='revoked'`, `revoked_at=now()`, and disable its codes.
   Activations already issued keep working offline (perpetual signed licences cannot be
   recalled). New activations and re-activations are refused. If that ever matters, add an
   optional online re-check to the plugin — not needed for v1.

**Code email** (Resend, plain and short):

> Subject: Your Terrain authorization codes
>
> Thanks for buying Terrain. To activate it, open Terrain → Settings → Account → Activate
> Terrain, enter this email address, then paste one of these codes. Each code activates
> one computer.
>
> TRRN-7KQ2-M9XD · TRRN-… · TRRN-…
>
> You can always find these codes, and see which computers use them, at
> {account url}/licenses.

---

## 6. Plugin API (`/v1/*`)

All endpoints: `POST`, `Content-Type: application/json`, HTTPS only, JSON responses of the
shape `{ "status": "...", ... }`. Body limit 4 KB (except `/v1/events`). CORS: none (the
plugin is not a browser). Unknown fields are ignored. Every response carries
`Cache-Control: no-store`.

The `status` strings are exactly the ones the plugin UI understands
(`LicenseTypes.h → jsStatus`, `index.html → regNet`). HTTP codes map as in
`LicenseServerClient.h`.

### 6.1 `POST /v1/lookup`

```json
{ "email": "max@example.com", "product": "terrain" }
```

| Case | HTTP | Body |
|---|---|---|
| At least one active purchase for this email + product | 200 | `{"status":"found","seatsTotal":3,"seatsUsed":1}` |
| None | 404 | `{"status":"not_found"}` |
| Malformed email | 400 | `{"status":"error"}` |
| Rate-limited | 429 | `{"status":"rate_limited"}` |

With several purchases for the same email, `seatsTotal` / `seatsUsed` are summed.
Never return codes, names, order numbers or dates here.

*Privacy trade-off, stated plainly:* this endpoint confirms whether an email bought Terrain.
That is the requested UX ("no purchase for that email"). It is limited to a yes/no plus seat
counts and is rate-limited per IP and per email so it cannot be used to scan lists.

### 6.2 `POST /v1/activate`

```json
{ "email": "max@example.com", "code": "TRRN-7KQ2-M9XD",
  "machineId": "b64-sha256-fingerprint", "name": "Max Hart",
  "product": "terrain", "productMajor": 1 }
```

Server logic, in one transaction (`select … for update` on the code row):

1. Normalise email and code. Find the code **joined to an active purchase with that email
   and product**. None → `422 {"status":"bad_code"}` (same answer for a wrong code and a
   code that belongs to someone else — don't reveal which).
2. Code disabled / purchase revoked → `422 {"status":"bad_code"}`.
3. Live activation on this code:
   - same `machineId` → **re-issue** (reinstall / new OS): update `last_seen_at`, sign a fresh
     licence, return `200`. This is idempotent.
   - different `machineId` → if all codes of the purchase are in use,
     `409 {"status":"seats_full","seatsTotal":3,"seatsUsed":3}`; otherwise
     `409 {"status":"code_in_use","seatsTotal":3,"seatsUsed":N}`.
4. No live activation → insert one, sign, return:

```json
{ "status": "activated", "seatsTotal": 3, "seatsUsed": 2,
  "licence": { ...Licence.json, see 6.4... } }
```

`name` is cleaned before signing: NFC, control characters → space, collapse spaces, max 60
characters. It is informational (it shows as "Registered to …").

### 6.3 `POST /v1/deactivate`

```json
{ "licenseId": "uuid", "activationId": "uuid", "machineId": "b64-sha256-fingerprint" }
```

Frees the seat if the activation exists, is live, belongs to that licence, **and** its
`machine_id` equals the one sent → `200 {"status":"deactivated"}`, `released_by='plugin'`.
Already released → also `200 {"status":"deactivated"}` (idempotent). Mismatch →
`404 {"status":"error"}`. The plugin removes its local licence regardless of the answer.

### 6.4 The licence file (`Licence.json`)

```json
{
  "schemaVersion": 1,
  "keyId": "k1",
  "product": "Terrain",
  "productMajor": 1,
  "licenseId": "4f1c…",          // purchases.id
  "activationId": "9a0e…",       // activations.id
  "boundName": "Max Hart",
  "boundEmail": "max@example.com",
  "boundMachineId": "b64-sha256-fingerprint",
  "maxSeats": 3,
  "issuedAt": 1758614400,
  "expiresAt": 0,
  "signature": "base64(64-byte Ed25519 signature)"
}
```

**Canonical bytes** (what is signed, and what the client rebuilds in
`LicenseManager::canonicalBytes`): UTF-8, one `key=value` per line, `\n` line endings,
this exact order, a trailing `\n` after the last line, integers in base 10 with no sign,
padding or leading zeros:

```
WAVESCRATE-LICENCE-1
schemaVersion=1
keyId=k1
product=Terrain
productMajor=1
licenseId=4f1c…
activationId=9a0e…
boundName=Max Hart
boundEmail=max@example.com
boundMachineId=b64-sha256-fingerprint
maxSeats=3
issuedAt=1758614400
expiresAt=0
```

No value may contain `\n` or `\r` (the server guarantees this by cleaning `name`; emails
and ids cannot contain them). Signing in Node:

```js
import { createPrivateKey, sign } from 'node:crypto';
const key = createPrivateKey(process.env.LICENSE_SIGNING_KEY);
const signature = sign(null, Buffer.from(canonical, 'utf8'), key).toString('base64');
```

Ship one test vector (a canonical string, the public key and the signature) in the repo
so the C++ verifier is tested against exactly what the server produces.

**Key rotation:** add `k2`, sign new licences with it, and ship a plugin update that embeds
both public keys (looked up by `keyId`). Old licences keep verifying under `k1`.

---

## 7. Rate limiting & abuse

Upstash `@upstash/ratelimit`, sliding window. Keys are hashed (`sha256(ip)`,
`sha256(email)`) and expire with the window, so no raw IP is stored.

| Endpoint | Limit |
|---|---|
| `/v1/lookup` | 10 / min per IP · 20 / hour per email |
| `/v1/activate` | 10 / min per IP · 10 / hour per email · 5 wrong codes / hour per email, then 1 hour lockout |
| `/v1/deactivate` | 20 / hour per IP |
| `/v1/events` | 30 / hour per `install_id` · 120 / hour per IP |
| magic-link request | 3 / 15 min per email · 20 / hour per IP |

Every limited response is `429 {"status":"rate_limited"}` with `Retry-After`.
Log activation failures (email hash, outcome, time) for 30 days to spot code sharing.

---

## 8. Opt-in telemetry

The plugin sends nothing unless the user chose **Share anonymous usage** (onboarding
step 4, or Settings → Account → Privacy). Turning it off stops sending immediately and
deletes the local queue. The server additionally ignores all events while
`TELEMETRY_ENABLED` is not `true`.

### 8.1 `POST /v1/events`

```json
{
  "installId": "random-uuid-v4",
  "product": "terrain", "appVersion": "1.0.3", "os": "macOS 15", "host": "vst3",
  "events": [
    { "name": "session_start",  "at": "2026-09-23T14:05Z" },
    { "name": "page_open",      "at": "2026-09-23T14:05Z", "props": { "page": "mod" } },
    { "name": "knob_used",      "at": "2026-09-23T14:07Z", "props": { "param": "SYN_FILTER1_CUTOFF" } },
    { "name": "preset_load",    "at": "2026-09-23T14:09Z", "props": { "preset": "factory:Glass Pad", "factory": true } },
    { "name": "session_end",    "at": "2026-09-23T14:51Z", "props": { "minutes": 46 } }
  ]
}
```

- Batched by the plugin (at most every 15 minutes, and at session end); ≤ 100 events and
  ≤ 32 KB per request.
- `installId` is a random UUID created when the user opts in and **replaced** each time they
  opt in again. It is never sent to `/v1/lookup|activate|deactivate` and never stored next
  to a licence, email or machine id.
- **Allow-list** (anything else is dropped): events `session_start`, `session_end`,
  `page_open`, `feature_used`, `knob_used`, `preset_load`, `crash`; props `page`,
  `feature`, `param` (must be a known parameter id), `preset` (factory presets only —
  user preset names are replaced by `"user"`), `factory`, `minutes`.
- Timestamps are rounded to the minute by the client; the server rejects any older than
  7 days or in the future.
- Response: `202 {"status":"ok"}`. The server does not store the IP.

### 8.2 Crash reports

`{ "name": "crash", "props": { "signature": "...", "frames": [...] } }` in the same batch,
moved into `crash_reports`. The client strips file paths down to the module name (no home
directory or user name can appear) and never includes audio buffers or memory contents.

### 8.3 Daily summary

Vercel Cron, `0 6 * * *` → `GET /api/cron/daily` (protected by `CRON_SECRET`):

1. Aggregate yesterday into `daily_stats`: active installs (distinct `install_id`),
   sessions, median session length, app-version split, OS split, top 20 pages, features,
   knobs and factory presets, crash count grouped by `signature`.
2. Email `SUMMARY_TO` a plain-text digest of the above (plus new purchases and activations
   from the licensing tables).
3. Delete `telemetry_events` and `crash_reports` older than **90 days**. `daily_stats` are
   kept; they contain no per-install data.

---

## 9. Customer account — "Licenses" (shared by every Waves Crate plugin)

`/account/licenses`. Login is a **magic link** sent to the email address (15-minute,
single-use token; `login_tokens` stores only its hash). The session cookie is HttpOnly,
Secure, SameSite=Lax, 30 days.

The page lists every active purchase for that email, grouped by product:

```
Terrain                                   purchased 23 Sep 2026 · order #1042
  TRRN-7KQ2-M9XD   Studio Mac · activated 23 Sep 2026        [Free this computer]
  TRRN-4HNP-2WQA   Not used yet                                [Copy]
  TRRN-X8RT-6YCB   Not used yet                                [Copy]
```

- **Free this computer** releases the activation (`released_by='customer'`). The old machine
  keeps its offline licence until it is signed out, so limit this to **3 releases per
  purchase per 30 days** to stop rotation abuse (admin can override).
- Machine labels are editable; the default is "Computer 1/2/3". The plugin never sends a
  computer name.
- Future plugins appear on this page automatically once they have a `products` row.

*Alternative:* if Max prefers the account area inside Shopify, build it as a Shopify
Customer Account UI extension that calls `GET /v1/account/licenses` with the Shopify
session token (verify it with the app secret). The data model does not change.

---

## 10. Admin (`/admin`)

Magic-link login restricted to `ADMIN_EMAILS`. Every action is written to an
`admin_log(at, admin_email, action, target, note)` table.

- **Search** by email, code, or Shopify order number.
- **Grant a licence** to any email: choose product, number of units (default 1), optional
  note ("reviewer", "beta tester", "replacement"). Creates `source='manual'` purchases with
  codes and optionally sends the code email.
- **Revoke** a purchase (refund handled outside Shopify, chargeback, abuse).
- **Free a computer** / **Free all computers** on a purchase, without limits.
- **Resend** the code email.
- **Delete customer**: removes every purchase, code, activation and login token for an
  email (see section 11).
- **Stats**: the last 30 days of `daily_stats`, and purchases/activations per day.

---

## 11. Privacy notes (for the privacy policy)

- **Licensing** stores the buyer's email, name, Shopify order reference, the three codes,
  and for each activated computer a **salted one-way fingerprint** (no serial numbers, MAC
  addresses, hostnames or user names) plus activation and release dates. Purpose:
  delivering and enforcing the licence. Kept for the life of the licence.
- **Usage data** is **opt-in**, off by default, and can be switched off any time in
  Settings → Account → Privacy. It contains crash reports, which pages, features, knobs and
  factory presets are used, the app version, the OS version, the plugin format and session length. It never
  contains audio, files, project or sample names, user preset names, names, emails, IP
  addresses or licence ids. It is keyed by a random install id that cannot be linked to a
  purchase. Raw events are deleted after 90 days.
- **No third-party analytics or ad SDKs**, in the plugin or on these pages.
- **Deletion requests:** the admin page's "Delete customer" removes the purchases, codes,
  activations and login tokens for an email (licences already on disk keep working
  offline). Telemetry has nothing to delete by email, by design.
- All traffic is HTTPS. Secrets live only in Vercel environment variables.

---

## 12. Suggested layout (Next.js App Router on Vercel)

```
app/
  api/shopify/webhook/route.ts   // 5
  v1/lookup/route.ts             // 6.1
  v1/activate/route.ts           // 6.2
  v1/deactivate/route.ts         // 6.3
  v1/events/route.ts             // 8.1
  api/cron/daily/route.ts        // 8.3
  account/licenses/page.tsx      // 9
  admin/…                        // 10
lib/
  db.ts  codes.ts  licence.ts (canonical + sign)  ratelimit.ts  email.ts  auth.ts
test/
  licence-vector.json            // shared with the C++ verifier test
```

Set the function region next to the database. All `/v1/*` handlers run on the Node.js
runtime (Ed25519 signing uses `node:crypto`).

---

## 13. Build checklist

1. Create the Postgres database and run section 3; seed `products`.
2. Generate the Ed25519 key pair; put the private key in Vercel; put the public key in
   `SignatureVerifier.h` (plugin) and commit the test vector.
3. Implement `/v1/lookup`, `/v1/activate`, `/v1/deactivate` with the rate limits.
4. Implement the webhook; test with Shopify's "Send test notification" and a real $0 test
   order; confirm a retry does not create duplicate codes.
5. Code email + `/account/licenses` + magic link.
6. `/admin` (grant, revoke, free, resend, search).
7. In the plugin: implement `HttpsServerClient`, register the natives, and set
   `LicenseServerConfig.baseUrl` from the build configuration. Until that is set, the
   plugin keeps saying "Activation server not configured yet".
8. Telemetry last: `/v1/events`, cron, summary email, then flip `TELEMETRY_ENABLED`.
