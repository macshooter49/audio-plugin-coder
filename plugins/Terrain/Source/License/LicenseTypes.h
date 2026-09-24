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
    //  Seat model: one purchase (keyed by the buyer's order email) carries
    //  kSeatsPerPurchase authorization codes; each code activates ONE computer.
    //  Seats are enforced server-side; the client only displays the counts.
    // -------------------------------------------------------------------------
    inline constexpr std::uint32_t kSeatsPerPurchase = 3;

    // -------------------------------------------------------------------------
    //  Activation identity — comes from the settings activation flow
    //  (name -> purchase email [lookup] -> authorization code [activate]).
    //  Nothing is emailed by the plugin: codes are issued when the Shopify order
    //  is paid, delivered with the order, and listed in the customer's Waves
    //  Crate account ("Licenses"). JS natives: licenseLookup / licenseActivate /
    //  licenseDeactivate (see index.html #st-overlay, "ACTIVATION").
    // -------------------------------------------------------------------------
    struct RegistrationIdentity
    {
        std::string name;               // Display / author name; signs presets.
        std::string email;              // The email the purchase was made with.
        std::string authorizationCode;  // "TRRN-XXXX-XXXX", one of that purchase's codes.
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
        std::string   keyId;            // which server signing key (rotation), e.g. "k1"
        std::string   product;          // "Terrain"
        std::uint32_t productMajor = 0; // licence valid for this major version line
        std::string   licenseId;        // server-side unique id (for seat mgmt)
        std::string   boundName;        // identity the licence was issued to
        std::string   boundEmail;
        std::string   boundMachineId;   // MachineFingerprint.value at issue time
        std::string   activationId;     // server id of this (code, machine) activation
        std::uint32_t maxSeats = 0;     // informational (3); seats enforced server-side
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
    //  Licensing-server round trips. The server is Max's own small HTTPS
    //  service (spec: plugins/Terrain/.ideas/licensing-server-spec.md). These
    //  model the three natives the settings UI calls.
    // -------------------------------------------------------------------------

    // Transport / availability outcomes shared by every call.
    enum class ServerOutcome
    {
        Ok = 0,         // the server answered; read the call-specific status
        NotConfigured,  // no endpoint configured in this build -> nothing sent
        Offline,        // DNS / TLS / timeout / no network
        RateLimited,    // HTTP 429
        ServerError     // 5xx, malformed JSON, unexpected shape
    };

    // POST /v1/lookup {email, product}
    enum class LookupStatus { Found, NotFound };
    struct PurchaseLookupResult
    {
        ServerOutcome outcome    = ServerOutcome::NotConfigured;
        LookupStatus  status     = LookupStatus::NotFound;
        std::uint32_t seatsTotal = kSeatsPerPurchase;
        std::uint32_t seatsUsed  = 0;
        std::string   userMessage;
    };

    // POST /v1/activate {email, code, machineId, name, product, productMajor}
    enum class ActivationStatus
    {
        Activated,      // signed licence received, verified and stored
        BadCode,        // code unknown, or not issued to this email
        CodeInUse,      // code already bound to a DIFFERENT machine
        SeatsFull,      // every code on the purchase is in use
        LicenseRejected // server said OK but the licence failed local verification
    };
    struct ActivationResult
    {
        ServerOutcome    outcome    = ServerOutcome::NotConfigured;
        ActivationStatus status     = ActivationStatus::BadCode;
        std::uint32_t    seatsTotal = kSeatsPerPurchase;
        std::uint32_t    seatsUsed  = 0;
        Bytes            licenseBlob;                     // signed Licence.json (Activated only)
        GateReason       failure    = GateReason::None;   // set for LicenseRejected
        std::string      userMessage;

        bool success() const noexcept
        {
            return outcome == ServerOutcome::Ok && status == ActivationStatus::Activated;
        }
    };

    // POST /v1/deactivate {licenseId, activationId, machineId}
    struct DeactivationResult
    {
        ServerOutcome outcome = ServerOutcome::NotConfigured;
        bool          freed   = false;   // server released the seat
    };

    // -------------------------------------------------------------------------
    //  The JSON "status" string the natives hand back to the settings UI.
    //  Keep in lock-step with regNet() in index.html.
    // -------------------------------------------------------------------------
    inline const char* outcomeStatus(ServerOutcome o) noexcept
    {
        switch (o)
        {
            case ServerOutcome::NotConfigured: return "not_configured";
            case ServerOutcome::Offline:       return "offline";
            case ServerOutcome::RateLimited:   return "rate_limited";
            case ServerOutcome::ServerError:   return "error";
            case ServerOutcome::Ok:            break;
        }
        return "error";
    }

    inline const char* jsStatus(const PurchaseLookupResult& r) noexcept
    {
        if (r.outcome != ServerOutcome::Ok) return outcomeStatus(r.outcome);
        return r.status == LookupStatus::Found ? "found" : "not_found";
    }

    inline const char* jsStatus(const ActivationResult& r) noexcept
    {
        if (r.outcome != ServerOutcome::Ok) return outcomeStatus(r.outcome);
        switch (r.status)
        {
            case ActivationStatus::Activated:       return "activated";
            case ActivationStatus::BadCode:         return "bad_code";
            case ActivationStatus::CodeInUse:       return "code_in_use";
            case ActivationStatus::SeatsFull:       return "seats_full";
            case ActivationStatus::LicenseRejected: return "error";
        }
        return "error";
    }

    inline const char* jsStatus(const DeactivationResult& r) noexcept
    {
        if (r.outcome != ServerOutcome::Ok) return outcomeStatus(r.outcome);
        return r.freed ? "deactivated" : "error";
    }
}
