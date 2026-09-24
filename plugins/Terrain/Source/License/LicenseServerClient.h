// =============================================================================
//  Terrain — Licensing / Copy-Protection SKELETON
//  LicenseServerClient.h — the ONLY place Terrain talks to the licensing server.
// -----------------------------------------------------------------------------
//  STATUS: SKELETON. Interface + a disabled default. No URL and no secret are
//  compiled in: the endpoint comes from LicenseServerConfig, which is EMPTY in
//  every build until the owner configures it. Empty (or non-HTTPS) config means
//  every call returns ServerOutcome::NotConfigured WITHOUT touching the network,
//  and the settings UI says "Activation server not configured yet".
//
//  Server contract: plugins/Terrain/.ideas/licensing-server-spec.md
//    POST {baseUrl}/v1/lookup      {email, product}
//    POST {baseUrl}/v1/activate    {email, code, machineId, name, product, productMajor}
//    POST {baseUrl}/v1/deactivate  {licenseId, activationId, machineId}
//
//  Threading: every call BLOCKS on network I/O. The editor natives must run it on
//  a background thread (juce::Thread / ThreadPool) and resolve the JS promise on
//  the message thread. Never call from processBlock.
// =============================================================================
#pragma once

#include "LicenseTypes.h"

#include <string>

namespace terrain::license
{
    // -------------------------------------------------------------------------
    //  Endpoint configuration. Supplied at hook-up from a non-committed build
    //  setting (e.g. a CMake cache var / CI secret-free config), NOT a literal in
    //  source. The base URL is not a secret, but keeping it out of the skeleton
    //  guarantees an unconfigured build can never phone anywhere.
    // -------------------------------------------------------------------------
    struct LicenseServerConfig
    {
        std::string baseUrl;       // e.g. "https://<your-service>/"  — EMPTY = disabled
        std::string storeUrl;      // "Buy Terrain" target            — EMPTY = hidden/toast
        int         timeoutMs = 15000;

        bool isConfigured() const noexcept
        {
            return baseUrl.rfind("https://", 0) == 0 && baseUrl.size() > 8; // HTTPS only
        }
    };

    // -------------------------------------------------------------------------
    //  Interface. Implementations translate HTTP to the result types:
    //    200 + JSON        -> ServerOutcome::Ok + call-specific status
    //    404 not_found     -> Ok + LookupStatus::NotFound
    //    409 code_in_use   -> Ok + ActivationStatus::CodeInUse
    //    409 seats_full    -> Ok + ActivationStatus::SeatsFull
    //    422 bad_code      -> Ok + ActivationStatus::BadCode
    //    429               -> RateLimited
    //    5xx / bad JSON    -> ServerError
    //    no connection     -> Offline
    //  The machineId sent is MachineFingerprint.value (a salted one-way hash).
    // -------------------------------------------------------------------------
    class ILicenseServerClient
    {
    public:
        virtual ~ILicenseServerClient() = default;

        virtual bool isConfigured() const noexcept = 0;

        virtual PurchaseLookupResult lookup(const std::string& email) = 0;

        virtual ActivationResult activate(const RegistrationIdentity& id,
                                          const MachineFingerprint& machine) = 0;

        virtual DeactivationResult deactivate(const std::string& licenseId,
                                              const std::string& activationId,
                                              const MachineFingerprint& machine) = 0;
    };

    // -------------------------------------------------------------------------
    //  Disabled default: what every build uses until the endpoint is configured.
    //  Sends nothing, fakes nothing.
    // -------------------------------------------------------------------------
    class DisabledServerClient final : public ILicenseServerClient
    {
    public:
        bool isConfigured() const noexcept override { return false; }

        PurchaseLookupResult lookup(const std::string&) override
        {
            PurchaseLookupResult r;
            r.outcome = ServerOutcome::NotConfigured;
            r.userMessage = "Activation server not configured yet.";
            return r;
        }

        ActivationResult activate(const RegistrationIdentity&, const MachineFingerprint&) override
        {
            ActivationResult r;
            r.outcome = ServerOutcome::NotConfigured;
            r.userMessage = "Activation server not configured yet.";
            return r;
        }

        DeactivationResult deactivate(const std::string&, const std::string&,
                                      const MachineFingerprint&) override
        {
            return {}; // NotConfigured, freed = false
        }
    };

    // TODO(hook-up): class HttpsServerClient final : public ILicenseServerClient
    //   - ctor(LicenseServerConfig); isConfigured() -> cfg.isConfigured()
    //   - juce::URL(cfg.baseUrl + "v1/...").withPOSTData(json) with
    //     "Content-Type: application/json", timeout = cfg.timeoutMs
    //   - map status codes as documented above; never log the email or code
    //   - activate(): return the raw signed licence bytes in ActivationResult so
    //     LicenseManager verifies them BEFORE anything is reported as success.
}
