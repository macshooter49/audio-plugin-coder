// =============================================================================
//  Terrain — Licensing / Copy-Protection SKELETON
//  LicenseTypes.h — shared value types (no logic, no crypto, no I/O)
// -----------------------------------------------------------------------------
//  STATUS: DESIGN SKELETON ONLY. Not wired into the audio path. Reviewed before
//  hook-up. See plugins/Terrain/.ideas/licensing-design.md for the full design.
//
//  Deliberately FRAMEWORK-FREE (pure C++17, no JUCE) so the licence core can be
//  unit-tested and syntax-checked in isolation with g++. At integration time the
//  string/byte types below map cleanly to juce::String / juce::MemoryBlock at the
//  boundary; the core logic stays framework-free.
//
//  BOUNDARY (enforcement is limited to Terrain itself): nothing in this design
//  touches, modifies, or persists anything on the user's machine beyond Terrain's
//  own settings/licence files. No hidden payloads, no anti-analysis that harms the
//  machine, no data collection without opt-in. The ONLY consequence of an invalid
//  or missing licence is that Terrain refuses to produce audio and shows the gate.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace terrain::license
{
    using Bytes = std::vector<std::uint8_t>;

    // Wall-clock time as seconds since the Unix epoch (UTC). Passed in explicitly
    // everywhere so the logic is deterministic and testable (no hidden clock reads).
    using UnixTime = std::int64_t;

    // -------------------------------------------------------------------------
    //  Overall licence status reported to the UI / gate.
    // -------------------------------------------------------------------------
    enum class LicenseStatus
    {
        Unknown = 0,   // Not yet evaluated. FAIL-CLOSED default — treated as "no audio".
        Licensed,      // Valid, signed, machine- and identity-bound licence present.
        TrialActive,   // No licence, but the 14-day demo window is still open.
        TrialExpired,  // Demo window elapsed; registration required.
        NoLicense,     // No licence and no trial state could be established.
        Invalid,       // A licence exists but failed verification/binding.
        Tampered       // Trial or licence state shows evidence of tampering.
    };

    // -------------------------------------------------------------------------
    //  Why the gate is up (drives the message shown in the registration gate).
    // -------------------------------------------------------------------------
    enum class GateReason
    {
        None = 0,           // Audio permitted.
        NotEvaluated,       // Boot hasn't completed — default deny.
        NoLicenseNoTrial,   // Nothing on disk and trial could not start.
        TrialExpired,       // 14 days used up.
        SignatureInvalid,   // Ed25519 verification failed / key mismatch.
        ProductMismatch,    // Licence is for a different product/major version.
        MachineMismatch,    // Licence not bound to this machine.
        IdentityMismatch,   // Licence identity != registered name/email.
        Expired,            // Time-limited licence past its expiry.
        ClockTampered,      // Wall clock moved backwards vs. monotonic high-water.
        StoreCorrupt        // Licence/trial file unreadable or checksum failed.
    };

    // -------------------------------------------------------------------------
    //  Registration identity — comes from the settings registration flow
    //  (name -> email -> authorization code "TRRN-XXXX-XXXX"). See the settings
    //  mockup: Design/settings-mockup.html (natives registerStart/registerVerify).
    // -------------------------------------------------------------------------
    struct RegistrationIdentity
    {
        std::string name;               // Display / author name; signs presets.
        std::string email;              // Where the authorization code was mailed.
        std::string authorizationCode;  // "TRRN-XXXX-XXXX" the server issued+mailed.
    };

    // -------------------------------------------------------------------------
    //  Opaque, non-reversible machine fingerprint (see MachineBinding.h).
    //  A stable, salted hash — NEVER raw serial numbers or PII on the wire.
    // -------------------------------------------------------------------------
    struct MachineFingerprint
    {
        std::string value;              // e.g. base64(SHA-256(salt || stable-ids)).
        bool isValid() const noexcept { return !value.empty(); }
    };

    // -------------------------------------------------------------------------
    //  The decoded, verified licence payload. The bytes actually signed by the
    //  server are the canonical serialization of these fields (see canonicalBytes
    //  contract in the design doc); `signature` is the detached Ed25519 signature.
    // -------------------------------------------------------------------------
    struct LicenseFile
    {
        std::uint32_t schemaVersion = 0;
        std::string   product;          // "Terrain"
        std::uint32_t productMajor = 0; // licence valid for this major version line
        std::string   licenseId;        // server-side unique id (for seat mgmt)
        std::string   boundName;        // identity the licence was issued to
        std::string   boundEmail;
        std::string   boundMachineId;   // MachineFingerprint.value at issue time
        std::uint32_t maxSeats = 0;     // informational; seats enforced server-side
        UnixTime      issuedAt = 0;
        UnixTime      expiresAt = 0;    // 0 == perpetual (no expiry)
        Bytes         signature;        // detached Ed25519 signature over canonical bytes

        bool isPerpetual() const noexcept { return expiresAt == 0; }
    };

    // -------------------------------------------------------------------------
    //  Persisted trial state (monotonic, anti-rollback — see TrialClock.h).
    //  Stored tamper-evident (checksummed) but NOT server-secret-protected: with
    //  only an embedded PUBLIC key the client cannot MAC this against a secret, so
    //  a determined user can reset it. That tradeoff is documented in the design.
    // -------------------------------------------------------------------------
    struct TrialState
    {
        bool     started       = false;
        UnixTime firstSeen     = 0;   // when the trial clock began
        UnixTime lastSeenHigh  = 0;   // monotonic high-water of observed wall clock
        std::uint32_t trialDays = 14; // demo length
        bool     rollbackSeen  = false; // wall clock was observed moving backwards
    };

    // -------------------------------------------------------------------------
    //  The single decision the rest of the plugin consumes.
    // -------------------------------------------------------------------------
    struct GateDecision
    {
        bool          audioAllowed = false;              // FAIL-CLOSED default.
        LicenseStatus status       = LicenseStatus::Unknown;
        GateReason    reason       = GateReason::NotEvaluated;
        int           trialDaysRemaining = 0;            // 0 when licensed or expired
        std::string   userMessage;                       // shown in the gate UI

        static GateDecision denied(LicenseStatus s, GateReason r, std::string msg)
        {
            GateDecision d;
            d.audioAllowed = false;
            d.status = s;
            d.reason = r;
            d.userMessage = std::move(msg);
            return d;
        }
    };

    // -------------------------------------------------------------------------
    //  Registration server round-trip results (server side is out-of-process;
    //  these model the two natives the settings UI calls). See design doc §Server.
    // -------------------------------------------------------------------------
    struct RegistrationChallenge
    {
        bool        accepted = false;   // server accepted name+email, mailed a code
        std::string requestId;          // opaque handle to correlate the code check
        std::string userMessage;        // "We sent a code to <email>", or an error
    };

    struct RegisterResult
    {
        bool        success = false;    // code verified AND a signed licence stored
        GateReason  failure = GateReason::None;
        std::string userMessage;
    };
}
