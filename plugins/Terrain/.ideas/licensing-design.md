# Terrain — Licensing / Copy-Protection Design

**Status:** DESIGN + compiling client skeleton. **Not wired into the audio path.**
To be reviewed before hook-up.
**Revised 2026-09-23:** activation is now **purchase-verified** (Max's spec): the
buyer enters the email on their Shopify order, the server looks the purchase up,
and the buyer pastes one of the **three codes** issued to that email (one code =
one computer). Nothing is emailed from the plugin. Server side:
`licensing-server-spec.md`.
**Scope of this document:** the legitimate, standard offline copy-protection scheme
for Terrain — how the plugin verifies a licence, runs a 14-day demo, and fails
closed — plus what the server must provide and how it snaps onto the existing
registration UI.

**Base:** `feature/terrain-instrument` @ `9dbdab4`.
**Client skeleton:** `plugins/Terrain/Source/License/` (headers only; stubs; **no
real keys, no real endpoints**). Validated in isolation with g++ (see §10).

---

## 0. The one-paragraph summary

Terrain ships with **one embedded Ed25519 public key** and nothing secret. A licence
is a small **signed file** the server issues; the plugin **verifies** it offline
against that public key and checks it is **bound to this machine and to the
registered identity** (name / email / authorization code from Settings → Account).
With no valid licence, Terrain runs a **14-day demo**, then **hard-stops**: it
outputs **silence** and shows the **registration gate**. Every failure mode —
missing, invalid, expired, tampered — resolves to the **same fail-closed outcome**.
The **only** consequence for an unlicensed user is that **Terrain will not run for
them**. Nothing else on their computer is touched.

---

## 1. Threat model & goals

**Goal:** raise the bar against casual copying and casual clock-tampering while
staying **legally clean** and **honest**. This is standard, mainstream DRM (the
same shape used by many commercial plugins), not an arms race.

**In scope (what we defend against):**
- Copying one licence file to many machines → **machine binding**.
- Sharing / forging a licence → **Ed25519 signature over the payload**.
- Running forever without paying → **14-day demo + hard stop**.
- Casual clock rollback to extend the trial → **monotonic high-water timestamp**.
- Casual file editing of the trial marker → **tamper-evident checksum**.

**Explicitly NOT in scope (hard boundary — see §8):** anything that damages,
modifies, or persists on the user's computer beyond Terrain's own files. No hidden
payloads, no machine-harming anti-analysis, no phone-home, no data collection
without opt-in. Enforcement is limited to **disabling Terrain**.

**Honest tradeoff (stated up front):** **no client-side scheme is uncrackable.**
A determined attacker with a debugger can patch the check out of a native binary,
and a determined user can reset the local trial with a clean OS account / VM /
reinstall. This design **raises the bar for the 95%** and keeps everything legally
defensible; it does not pretend to stop the last 5%. We deliberately do **not**
add fragile, user-hostile anti-tamper (rootkits, kernel hooks, aggressive
obfuscation) because the downside (support cost, false positives, reputational and
legal risk) outweighs the marginal protection.

---

## 2. What lives in the binary (and what never does)

| Artifact | In the plugin? | Notes |
|---|---|---|
| Ed25519 **public** key (32 bytes) | **Yes**, embedded | Used only to *verify*. `SignatureVerifier.h → EmbeddedKey`. |
| Ed25519 **private** key | **NEVER** | Lives only in the signing service. |
| Any server secret / HMAC key | **NEVER** | The client has no secret to keep. |
| Machine-fingerprint salt | Yes (compile-time const) | Only salts a one-way hash. |
| Network endpoint (base URL) | **No** — `LicenseServerConfig.baseUrl` is **empty** in every build until configured | `LicenseServerClient.h`; empty ⇒ `NotConfigured`, nothing sent. |

Because the client holds **no secret**, a leak of the binary leaks nothing that
lets anyone mint licences. That is the whole point of asymmetric (public-key)
verification versus a shared serial-number secret.

---

## 3. The licence file

**Location (macOS):** `~/Library/Application Support/Waves Crate/Terrain/Licence.json`
(matches the settings mockup). **Windows:** `%APPDATA%\Waves Crate\Terrain\`.
The file is **signed, not encrypted** — its contents are not secret; integrity and
authenticity are what matter.

**Payload fields** (modeled in `LicenseTypes.h → LicenseFile`):

| Field | Purpose |
|---|---|
| `schemaVersion` | forward-compat |
| `keyId` | which server signing key signed it (key rotation) |
| `product` = `"Terrain"`, `productMajor` | licence scoped to a product + major line |
| `licenseId` | server-side id, for seat management |
| `boundName`, `boundEmail` | the registered identity |
| `boundMachineId` | this machine's fingerprint at activation |
| `activationId` | server id of the (code, machine) pair — used to free the seat on sign-out |
| `maxSeats` | informational (seats enforced server-side) |
| `issuedAt`, `expiresAt` | `expiresAt == 0` ⇒ perpetual |
| `signature` | **detached Ed25519 signature over the canonical bytes of all other fields** |

**Canonical bytes contract (critical):** the server and the client must serialize
the signed fields **identically and unambiguously** (fixed field order, fixed
number formatting, no incidental whitespace) so the client re-derives the exact
byte string the server signed. This is the `canonicalBytes()` seam in
`LicenseManager.h`. Getting this wrong = signatures never verify; getting it
loosely right = signature-malleability bugs. **Now specified** in
`licensing-server-spec.md` §6.4 (fixed-order `key=value` lines + a shared test vector).

**Verification order** (`LicenseManager::evaluate`, all must pass):
1. Parse the file (fail → `StoreCorrupt`).
2. `verify(canonicalBytes, signature)` under the embedded key (fail → `SignatureInvalid`).
3. `product` / `productMajor` match (fail → `ProductMismatch`).
4. `boundMachineId` matches this machine (fail → `MachineMismatch`).
5. If not perpetual, `now ≤ expiresAt` (fail → `Expired`).
6. (At registration/refresh) identity matches the settings registration
   (`IdentityMismatch`).

Any failure → **deny** (audio off + gate). Only all-pass → `Licensed` → audio on.

---

## 4. Machine binding

`MachineBinding.h`. The fingerprint is a **salted, one-way hash** of stable
platform ids:
- **macOS:** `IOPlatformUUID` (IOKit) → `base64(SHA-256(salt ‖ uuid))`.
- **Windows:** registry `MachineGuid` / SMBIOS UUID, same hashing.

**Privacy:** only the **hash** is ever stored in the licence or sent to the server
— never raw serials, MACs, or PII. No other machine data is read or transmitted.
An empty/underivable fingerprint matches nothing (fail-closed).

**Seats:** the client only proves "this licence names *this* machine." The **count**
("This Mac · 1 of 3" in the mockup) is enforced **server-side** at activation —
the server refuses to sign a 4th machine until one is released (matches the
mockup's "already used on 3 computers" error path).

---

## 5. The 14-day demo (anti-rollback)

`TrialClock.h` — implemented as **real, tested logic** (the rest are stubs).

- **Marker:** `~/Library/Application Support/Waves Crate/Terrain/Trial.dat`,
  tamper-evident (checksummed), storing `TrialState` (`firstSeen`,
  `lastSeenHigh`, `trialDays`).
- **Monotonic high-water:** on every observation, `effectiveNow =
  max(observedNow, lastSeenHigh)`. Consumed time = `effectiveNow − firstSeen`.
  - Moving the clock **back** cannot reduce consumed → **remaining never grows,
    never resets**.
  - Moving the clock **forward** only expires sooner (**fail-closed**).
  - Remaining is **clamped ≥ 0** (never negative).
  - A backwards move is **recorded** (`rollbackSeen` → `GateReason::ClockTampered`)
    for the UI, but the decision still fails toward expiry — we never trust the
    rolled-back value.
- **On expiry:** hard stop — `TrialExpired`, audio denied, gate shown.

**Why not cryptographically seal the trial marker?** With only a **public** key on
the client there is **no secret** to MAC the marker against a server. So the trial
file is only **tamper-*evident*** (casual edits are caught by the checksum), not
tamper-*proof*. Consequences of a determined reset (fresh OS user / VM / reinstall)
= a little more free demo time. That is acceptable because the gate is still
fail-closed and the enforcement never leaves Terrain. This is called out here so no
one mistakes the trial for unbreakable.

*(Optional future hardening, still legally clean and non-invasive: on first run
fetch a short **server-signed** trial token bound to the machine — then even the
trial verifies against the public key. Not required for v1.)*

---

## 6. Fail-closed enforcement & audio-path integration

**Contract (`LicenseManager.h`):**
- Default state is **DENY**. `isAudioAllowed()` returns **false** until an
  evaluation *proves* a valid licence or active trial.
- `isAudioAllowed()` is the **only** method the **audio thread** calls: it reads
  **one relaxed `std::atomic<bool>`** — no lock, no allocation, no I/O — so it is
  safe and cheap inside `processBlock`.
- All heavy work (parse, verify, disk, server) runs on the **message thread** via
  `initialiseAtBoot()` / `refresh()` / `register*()` / `applyLicenseFile()`, which
  publish the result into that atomic.

**Where it hooks in (NOT done in this skeleton — for the review):**
- `TerrainAudioProcessor` owns a `LicenseManager` member; call `initialiseAtBoot()`
  in the constructor / `prepareToPlay`.
- In `TerrainAudioProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)`
  (currently at `Source/PluginProcessor.cpp:10485`), **after** the normal render,
  `if (! license.isAudioAllowed()) buffer.clear();` — i.e. produce **silence**.
  (Clearing the output is the whole enforcement: no crash, no noise, no side effect.)
- The **editor** polls `currentDecision()` on its existing UI timer and shows the
  **registration gate** overlay whenever `!audioAllowed`, with `userMessage` /
  `trialDaysRemaining`.
- A short grace/de-bounce so a transient mid-session re-check never clicks the audio
  (fits the project's "no clicks/crackle" law — fade to silence, don't hard-cut).

---

## 7. The activation flow (settings UI ↔ client ↔ server)

UI source of truth: the `ACTIVATION` block of the `#st-overlay` script in
`Source/ui/public/index.html` (Settings → Account → Registration → **Activate Terrain**).

**The flow (4 steps, dots in the sheet header):**
1. **Name** — local only; becomes the preset author immediately.
2. **Purchase email** — "Which email address did you use to buy Terrain?" →
   `licenseLookup`. *Not found* → "We couldn't find a Terrain purchase for …" with
   **Try again** / **Buy Terrain**. *Found* → step 3 opens with "Found your purchase."
   and the free-seat count ("2 of 3 computers free").
3. **Authorization code** `TRRN-XXXX-XXXX` — one of the codes issued to that email
   (in the order email and in the customer's Waves Crate account → Licenses) →
   `licenseActivate`. Errors: `bad_code`, `code_in_use` (bound to another computer),
   `seats_full`. Success → "Terrain is activated" (name, email, author, "N of 3 in use").
4. **Anonymous usage opt-in** — shown once, the last step before first use.
   Two equal-weight buttons (**Not now** / **Share anonymous usage**), nothing
   pre-selected; Escape / click-outside = Not now. Stored as `S.usage` +
   `S.usageAsked`; switchable later in Settings → Account → Privacy.

Transport states shown on steps 2 and 3: `not_configured` ("**Activation server not
configured yet.** … Nothing was sent."), `offline`, `rate_limited`, `error`. The UI
never fakes success: a missing native is treated exactly like `not_configured`.

**Native ↔ skeleton mapping** (to register with `withNativeFunction` in
`PluginEditor.cpp` at hook-up — not done here). Each native takes one JSON string,
runs the blocking call on a background thread, and resolves with a JSON string
`{status, seatsTotal?, seatsUsed?}` where `status = jsStatus(result)`:

| JS native | `LicenseManager` method | `status` values |
|---|---|---|
| `licenseLookup({email})` | `lookupPurchase(email)` | `found`, `not_found` + transport |
| `licenseActivate({name,email,code})` | `activate(identity, now)` | `activated`, `bad_code`, `code_in_use`, `seats_full`, `error` + transport |
| `licenseDeactivate({})` | `deactivate(now)` | `deactivated` + transport (local licence removed regardless) |
| `licenseOpenStore()` *(optional)* | opens `LicenseServerConfig.storeUrl` | — (UI falls back to a toast) |
| (boot / UI timer) | `initialiseAtBoot` / `currentDecision` | drives "Registered to …", trial days left |

Transport statuses: `not_configured`, `offline`, `rate_limited`, `error`
(`ServerOutcome` → `outcomeStatus()` in `LicenseTypes.h`).

**`activate()` only reports success after the returned licence verifies locally**
(signature, product, machine). A server that says "ok" with a bad licence yields
`LicenseRejected` → `error`, and the bad file is removed. The server can never unlock
audio by assertion alone.

**Naming note:** C++ symbols are American (`License…`); the on-disk file stays
`Licence.json` to match the mockup.

---

## 8. Out of scope — the hard boundary (legitimacy)

This design **does not** include, and Terrain must **never** ship, any of:
- Anything that **damages, modifies, deletes, or disables** anything on the user's
  computer beyond Terrain's own files.
- **Hidden payloads**, backdoors, or logic that does anything other than gate
  Terrain's audio.
- **Machine-harming anti-analysis** (kernel drivers, rootkits, boot-persistence,
  destructive anti-debug).
- **Persistence** outside Terrain's own app-support directory.
- **Data collection without opt-in.** The only licensing network calls are the
  ones the user initiates (lookup, activate, sign-out). Anonymous usage data is
  **off by default**, asked once as the last onboarding step with no pre-selected
  answer, and switchable off any time in Settings → Account → Privacy. Its
  contents and ingest endpoint are specified in `licensing-server-spec.md` §8.

**The entire enforcement surface is:** on failure, `buffer.clear()` (silence) +
show the gate. That is the maximum consequence, by design.

---

## 9. What the SERVER must provide

Fully specified in **`licensing-server-spec.md`** (Max's own small Vercel service):
Shopify `orders/paid` webhook (HMAC-verified) → purchase + 3 codes for the buyer
email; `POST /v1/lookup`, `/v1/activate` (→ Ed25519-signed licence, private key only
on the server), `/v1/deactivate`; admin grant page; customer "Licenses" page shared by
future Waves Crate plugins; opt-in telemetry ingest `POST /v1/events` + daily summary.

The client trusts the server's **signature**, not the transport — HTTPS is still
required (it protects the email address and the code in flight).

---

## 10. The client skeleton (delivered here)

Header-only, framework-free C++17 under `plugins/Terrain/Source/License/`:

| File | Role |
|---|---|
| `LicenseTypes.h` | value types: statuses, reasons, `LicenseFile`, `TrialState`, `GateDecision`, identity. |
| `TrialClock.h` | **real** monotonic anti-rollback trial logic (tested). |
| `SignatureVerifier.h` | Ed25519 verify **interface** + **empty** embedded-key placeholder + fail-closed stub. |
| `MachineBinding.h` | fingerprint interface + fail-closed stub. |
| `LicenseStore.h` | persistence interface + in-memory store (test only). |
| `LicenseServerClient.h` | `LicenseServerConfig` (**empty** endpoint = disabled), `ILicenseServerClient` (lookup / activate / deactivate), `DisabledServerClient` default. |
| `LicenseManager.h` | the façade: fail-closed, lock-free `isAudioAllowed()`, `lookupPurchase` / `activate` / `deactivate`. |

**Deliberately framework-free** so the licence core is unit-testable and builds in
isolation; at hook-up the string/byte types map to `juce::String` / `juce::MemoryBlock`
only at the boundary. **No real crypto keys. No real endpoints.** The stubs make
the whole thing compile while keeping every path **fail-closed**.

**Validation (isolated, not the full plugin build):**
```
g++ -std=c++17 -Wall -Wextra -o license_selftest license_selftest.cpp
```
A tiny stub `main` (in the session scratchpad, not committed) drives the manager.
Result: **compiles clean (no warnings), all 13 behaviour checks PASS** — fresh
trial = 14 days, day-5 → 9 days, **rollback to day 2 stays 9 (no reset) and flags
clock tamper**, day-30 → audio **denied** / `TrialExpired`, unverifiable licence
stays denied, and a pre-evaluation manager defaults to **DENY**.

---

## 11. Open items for the review (before hook-up)
- Lock the **canonical serialization** of `LicenseFile` (client == server, byte-exact).
- Implement `HttpsServerClient` (juce::URL, background thread) and register the
  `licenseLookup` / `licenseActivate` / `licenseDeactivate` natives; source
  `LicenseServerConfig` from a build setting, never a literal in source.
- Choose the crypto dependency (**libsodium** preferred, or a small vendored
  ed25519) and wire `Ed25519Verifier`.
- Implement `MachineBinding` per-OS (IOKit / registry) + pick the salt.
- Implement the real `LicenseStore` (`juce::File` + JSON) at the mockup's paths;
  keep the file name `Licence.json`.
- Decide the mid-session re-check cadence + fade-to-silence grace (no-clicks law).
- Confirm the demo length (14 days) and whether to add the optional server-signed
  trial token (§5).
- Settle the `License`/`Licence` spelling for the C++ symbols (on-disk name stays
  `Licence.json`).
