// =============================================================================
//  Terrain — Licensing / Copy-Protection SKELETON
//  LicenseManager.h — the façade the processor owns. FAIL-CLOSED by default.
// -----------------------------------------------------------------------------
//  STATUS: SKELETON. Interfaces + stubs. NOT wired into processBlock yet.
//  Full design: plugins/Terrain/.ideas/licensing-design.md
//
//  Contract:
//    * Default state is DENY. `isAudioAllowed()` returns false until an evaluation
//      explicitly proves a valid licence or an active trial. Missing / invalid /
//      tampered state all keep audio disabled and the gate up.
//    * `isAudioAllowed()` is the ONLY method the audio thread calls. It reads one
//      relaxed atomic — no locks, no allocation, no I/O — so processBlock can gate
//      output cheaply. All the heavy work (verify, disk, server) runs on the
//      message thread via initialiseAtBoot()/refresh()/activate()/applyLicenseFile().
//    * Enforcement boundary: the only consequence of "not allowed" is that Terrain
//      outputs silence and shows the registration gate. Nothing else on the user's
//      machine is touched.
// =============================================================================
#pragma once

#include "LicenseTypes.h"
#include "TrialClock.h"
#include "SignatureVerifier.h"
#include "MachineBinding.h"
#include "LicenseStore.h"
#include "LicenseServerClient.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace terrain::license
{
    // The processor supplies the collaborators (dependency injection keeps the
    // core testable; the stubs above give a compiling, fail-closed default).
    struct LicenseManagerDeps
    {
        std::unique_ptr<ISignatureVerifier> verifier;
        std::unique_ptr<IMachineBinding>    machine;
        std::unique_ptr<ILicenseStore>      store;
        std::unique_ptr<ILicenseServerClient> server;   // null == DisabledServerClient
        TrialPolicy                         trialPolicy{};
    };

    class LicenseManager
    {
    public:
        explicit LicenseManager(LicenseManagerDeps deps)
            : verifier_(std::move(deps.verifier)),
              machine_(std::move(deps.machine)),
              store_(std::move(deps.store)),
              server_(deps.server ? std::move(deps.server)
                                  : std::make_unique<DisabledServerClient>()),
              trial_(deps.trialPolicy)
        {
        }

        // Convenience: a fully fail-closed manager wired to the skeleton stubs.
        static std::unique_ptr<LicenseManager> makeDefaultSkeleton()
        {
            LicenseManagerDeps d;
            d.verifier = std::make_unique<StubEd25519Verifier>();
            d.machine  = std::make_unique<StubMachineBinding>();
            d.store    = std::make_unique<InMemoryStore>();
            d.server   = std::make_unique<DisabledServerClient>();   // no endpoint -> nothing sent
            return std::make_unique<LicenseManager>(std::move(d));
        }

        // -------- audio thread (hot path) ------------------------------------
        // One relaxed atomic read. Default false until proven otherwise.
        bool isAudioAllowed() const noexcept
        {
            return audioAllowed_.load(std::memory_order_relaxed);
        }

        // -------- message thread ---------------------------------------------
        // Called once when the processor is constructed / prepared.
        GateDecision initialiseAtBoot(UnixTime nowUnix)
        {
            return reevaluate(nowUnix);
        }

        // Called periodically (e.g. editor timer) and whenever registration state
        // changes, to keep the trial countdown current and re-check expiry.
        GateDecision refresh(UnixTime nowUnix)
        {
            return reevaluate(nowUnix);
        }

        GateDecision currentDecision() const { return decision_; }

        // -------- activation (backs the settings-UI natives) ----------------
        // Purchase-verified activation. Every call BLOCKS on the network: the
        // editor natives run them on a background thread and resolve the JS
        // promise on the message thread with {status: jsStatus(result), ...}.
        //
        //   licenseLookup({email})               -> lookupPurchase(email)
        //   licenseActivate({name,email,code})   -> activate(identity, now)
        //   licenseDeactivate({})                -> deactivate(now)
        //
        // With no endpoint configured (the default) the server client answers
        // NotConfigured without touching the network, and nothing is faked.
        bool isServerConfigured() const noexcept { return server_ && server_->isConfigured(); }

        // Step 2: does a Terrain purchase exist for this (order) email?
        PurchaseLookupResult lookupPurchase(const std::string& email)
        {
            pendingIdentity_.email = email;
            if (! server_) return {};
            return server_->lookup(email);
        }

        // Step 3: email + one of its codes -> signed licence bound to this machine.
        // Success is reported ONLY after the returned licence verifies locally
        // (signature, product, machine), so a compromised or misconfigured server
        // can never unlock audio by saying "ok".
        ActivationResult activate(const RegistrationIdentity& id, UnixTime nowUnix)
        {
            pendingIdentity_ = id;
            ActivationResult r;
            if (! server_ || ! machine_ || ! server_->isConfigured())
            {
                r.userMessage = "Activation server not configured yet.";
                return r;   // ServerOutcome::NotConfigured — nothing sent
            }

            const MachineFingerprint fp = machine_->currentFingerprint();
            if (! fp.isValid())
            {
                r.outcome = ServerOutcome::ServerError;
                r.failure = GateReason::MachineMismatch;
                r.userMessage = "Terrain couldn't identify this computer.";
                return r;
            }

            r = server_->activate(id, fp);
            if (! r.success()) return r;

            const GateDecision d = applyLicenseFile(r.licenseBlob, nowUnix);
            if (d.status != LicenseStatus::Licensed)
            {
                if (store_) store_->clearLicense();   // never keep a licence that failed
                reevaluate(nowUnix);
                r.status  = ActivationStatus::LicenseRejected;
                r.failure = d.reason;
                r.userMessage = d.userMessage;
            }
            r.licenseBlob.clear();
            return r;
        }

        // Sign out + free this computer's seat on the server. The local licence is
        // removed even if the server can't be reached (the customer can free the
        // seat later from their Waves Crate account).
        DeactivationResult deactivate(UnixTime nowUnix)
        {
            DeactivationResult r;
            if (server_ && machine_ && store_)
            {
                LicenseFile lf;
                if (auto blob = store_->readLicenseBlob(); blob && parseLicense(*blob, lf))
                    r = server_->deactivate(lf.licenseId, lf.activationId, machine_->currentFingerprint());
            }
            signOut(nowUnix);
            return r;
        }

        // signOut(): remove the licence from THIS machine only. Trial is untouched.
        void signOut(UnixTime nowUnix)
        {
            if (store_) store_->clearLicense();
            reevaluate(nowUnix);
        }

        // Apply an already-obtained signed licence blob (e.g. manual import or the
        // server response). Verifies signature + binding before accepting.
        GateDecision applyLicenseFile(const Bytes& blob, UnixTime nowUnix)
        {
            if (store_) store_->writeLicenseBlob(blob);
            return reevaluate(nowUnix);
        }

    private:
        // The single evaluation funnel. Order: licence first, then trial, else gate.
        GateDecision reevaluate(UnixTime nowUnix)
        {
            GateDecision d = evaluate(nowUnix);
            decision_ = d;
            audioAllowed_.store(d.audioAllowed, std::memory_order_relaxed);
            return d;
        }

        GateDecision evaluate(UnixTime nowUnix)
        {
            // Guard against a broken/absent dependency: deny.
            if (! verifier_ || ! machine_ || ! store_)
                return GateDecision::denied(LicenseStatus::Invalid, GateReason::StoreCorrupt,
                                            "Licensing not initialised.");

            // 1) A stored licence takes precedence over the trial.
            if (auto blob = store_->readLicenseBlob())
            {
                LicenseFile lf;
                if (! parseLicense(*blob, lf))
                    return GateDecision::denied(LicenseStatus::Invalid, GateReason::StoreCorrupt,
                                                "Your licence file could not be read. Please re-register.");

                if (! verifier_->verify(canonicalBytes(lf), lf.signature))
                    return GateDecision::denied(LicenseStatus::Invalid, GateReason::SignatureInvalid,
                                                "Your licence signature is invalid. Please re-register.");

                if (lf.product != kProduct || lf.productMajor != kProductMajor)
                    return GateDecision::denied(LicenseStatus::Invalid, GateReason::ProductMismatch,
                                                "This licence is for a different version of Terrain.");

                if (! machine_->matches(lf.boundMachineId))
                    return GateDecision::denied(LicenseStatus::Invalid, GateReason::MachineMismatch,
                                                "This licence is registered to a different computer.");

                if (! lf.isPerpetual() && nowUnix > lf.expiresAt)
                    return GateDecision::denied(LicenseStatus::Invalid, GateReason::Expired,
                                                "This licence has expired.");

                GateDecision ok;
                ok.audioAllowed = true;
                ok.status = LicenseStatus::Licensed;
                ok.reason = GateReason::None;
                ok.userMessage = "Terrain is registered to " + lf.boundName + ".";
                return ok;
            }

            // 2) No licence -> the 14-day demo.
            TrialState st = store_->readTrial().value_or(TrialState{});
            st = trial_.observe(st, nowUnix);
            store_->writeTrial(st);

            if (trial_.isExpired(st))
                return GateDecision::denied(LicenseStatus::TrialExpired, GateReason::TrialExpired,
                                            "Your 14-day trial has ended. Register Terrain to keep playing.");

            GateDecision demo;
            demo.audioAllowed = true;
            demo.status = LicenseStatus::TrialActive;
            demo.reason = st.rollbackSeen ? GateReason::ClockTampered : GateReason::None;
            demo.trialDaysRemaining = trial_.daysRemaining(st);
            demo.userMessage = "Trial: " + std::to_string(demo.trialDaysRemaining) + " day(s) left.";
            return demo;
        }

        // ---- serialization seams (hook-up implements against Licence.json) ----
        // SKELETON: no format yet -> parsing fails (fail-closed), so a stored blob
        // never grants audio until the real parser + canonical form land.
        static bool parseLicense(const Bytes& /*blob*/, LicenseFile& /*out*/)
        {
            return false; // TODO(hook-up): parse JSON, decode base64 fields+signature.
        }

        // The exact byte sequence the server signs and the client re-derives to
        // verify. MUST be canonical (stable field order, no whitespace ambiguity).
        static Bytes canonicalBytes(const LicenseFile& /*lf*/)
        {
            return {}; // TODO(hook-up): serialise all fields EXCEPT signature, canonically.
        }

        static constexpr const char* kProduct      = "Terrain";
        static constexpr std::uint32_t kProductMajor = 1;

        std::unique_ptr<ISignatureVerifier> verifier_;
        std::unique_ptr<IMachineBinding>    machine_;
        std::unique_ptr<ILicenseStore>      store_;
        std::unique_ptr<ILicenseServerClient> server_;
        TrialClock                          trial_;

        RegistrationIdentity pendingIdentity_{};
        GateDecision         decision_{};            // last message-thread snapshot
        std::atomic<bool>    audioAllowed_{ false };  // FAIL-CLOSED default.
    };
}
