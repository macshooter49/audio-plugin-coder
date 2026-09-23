# Terrain — Licensing / Copy-Protection Design

**Status:** DESIGN + compiling client skeleton. **Not wired into the audio path.**
To be reviewed before hook-up.
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
| Network endpoints | Only at release; **placeholders in skeleton** | `beginRegistration` / `completeRegistration`. |

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
| `product` = `"Terrain"`, `productMajor` | licence scoped to a product + major line |
| `licenseId` | server-side id, for seat management |
| `boundName`, `boundEmail` | the registered identity |
| `boundMachineId` | this machine's fingerprint at activation |
| `maxSeats` | informational (seats enforced server-side) |
| `issuedAt`, `expiresAt` | `expiresAt == 0` ⇒ perpetual |
| `signature` | **detached Ed25519 signature over the canonical bytes of all other fields** |

**Canonical bytes contract (critical):** the server and the client must serialize
the signed fields **identically and unambiguously** (fixed field order, fixed
number formatting, no incidental whitespace) so the client re-derives the exact
byte string the server signed. This is the `canonicalBytes()` seam in
`LicenseManager.h`. Getting this wrong = signatures never verify; getting it
loosely right = signature-malleability bugs. Specify it once, test it both sides.

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

## 7. Integrating with the existing registration UI

The settings mockup already designs the whole flow — this scheme just supplies its
backend. Source of truth: `Design/settings-mockup.html` (Account → Registration,
About → Licence).

**The flow (unchanged):** **name → email → authorization code `TRRN-XXXX-XXXX` →
welcome.** The mockup's own note already describes the backing:
> "A LicenceManager on the processor: load/verify a signed licence file at boot;
> natives `registerStart(name,email)`, `registerVerify(code)`, `signOut()`; a server
> endpoint to mail + check codes."

**Native ↔ skeleton mapping** (natives are registered with `withNativeFunction` in
`Source/PluginEditor.cpp`, alongside `loadPreset`, `savePatchFile`, … — same pattern):

| JS native (settings UI) | `LicenseManager` method | Effect |
|---|---|---|
| `registerStart(name, email)` | `beginRegistration({name,email})` → `RegistrationChallenge` | Server mails a `TRRN-XXXX-XXXX` code. |
| `registerVerify(code)` | `completeRegistration(code)` → `RegisterResult` | Server checks code, returns a **signed** `Licence.json`; client verifies + stores; re-evaluates. |
| `signOut()` | `signOut(now)` | Removes `Licence.json` from **this machine only**; trial untouched. |
| (boot / UI timer) | `initialiseAtBoot` / `currentDecision` | Drives "Registered to …", "1 of 3", trial days left. |

The UI already renders every state this design produces: registered
(name/email/since), not-registered, the 3-machine limit, the "code already used on
3 computers" error, and the beta "Ad-hoc · Developer ID at 1.0" signing line — no
UI changes are required by this document (and none are made).

**Naming note:** the mockup uses British `Licence` / `LicenceManager` and the file
`Licence.json`; the skeleton uses American `License` (matching the requested
`Source/License/` path and `LicenseManager.h`). This is a one-word reconciliation
to settle at hook-up; the **on-disk file name should stay `Licence.json`** to match
the mockup, exposed via a single constant. Nothing load-bearing.

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
- **Data collection without opt-in.** The only network calls are the two the user
  initiates by registering (send/verify a code). The mockup's "Anonymous usage
  data" toggle is **off by default** and explicitly opt-in; this scheme adds no
  telemetry of its own.

**The entire enforcement surface is:** on failure, `buffer.clear()` (silence) +
show the gate. That is the maximum consequence, by design.

---

## 9. What the SERVER must provide

The server side is **out of process** and out of scope to build here, but the
client depends on it doing exactly this:

1. **Issue + email authorization codes.** On `registerStart(name,email)`: create a
   pending registration, generate a one-time `TRRN-XXXX-XXXX` code, email it to the
   address. Rate-limit; expire codes.
2. **Verify codes and enforce seats.** On `registerVerify(code)`: validate the code,
   check the machine fingerprint against the licence's seat allowance (e.g. 3),
   refuse if exhausted (drives the mockup's "used on 3 computers" message), allow a
   released seat to be reused.
3. **Sign licence files.** With the **private** Ed25519 key (held only here, e.g.
   in an HSM / KMS), produce the `canonicalBytes` of the payload and sign; return
   `Licence.json`. The private key **never** ships in the plugin.
4. **Manage seats / sign-out.** Track `licenseId → machines`; free a seat on
   `signOut`.
5. **Key management.** Keep the signing key offline/HSM; have a key-rotation plan
   (bump `schemaVersion`, ship a new embedded public key in a plugin update).

Client trusts the server's signature, not the transport — so even plain HTTPS
issues are non-fatal to authenticity (though HTTPS is still required for the code
exchange and to protect the email address).

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
| `LicenseManager.h` | the façade: fail-closed, lock-free `isAudioAllowed()`, registration seams. |

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
