// =============================================================================
//  Terrain — Licensing SKELETON
//  SignatureVerifier.h — offline signature verification with an EMBEDDED PUBLIC
//  KEY ONLY. There is NEVER a private key or any server secret in this binary.
// -----------------------------------------------------------------------------
//  The plugin only ever VERIFIES. Signing happens on the server (see design doc
//  §Server). The client holds one hard-coded Ed25519 public key and checks the
//  detached signature over the licence's canonical bytes.
//
//  This header defines the interface plus a FAIL-CLOSED stub that always returns
//  false (no key wired, no crypto lib linked yet). Real implementation at hook-up
//  time: libsodium `crypto_sign_verify_detached`, or a small vendored ed25519
//  ("ref10"/"orlp") compiled into the plugin. NO third-party network, NO secrets.
// =============================================================================
#pragma once

#include "LicenseTypes.h"

#include <cstdint>

namespace terrain::license
{
    // -------------------------------------------------------------------------
    //  Interface — abstract so the real verifier can be swapped in and the trial
    //  path can be tested with a stub. Verifiers are pure functions of their input.
    // -------------------------------------------------------------------------
    class ISignatureVerifier
    {
    public:
        virtual ~ISignatureVerifier() = default;

        // Returns true ONLY if `signature` is a valid Ed25519 signature over
        // `message` under the embedded public key. Any error -> false (fail-closed).
        virtual bool verify(const Bytes& message, const Bytes& signature) const = 0;

        // True once a real, non-placeholder public key is compiled in.
        virtual bool hasEmbeddedKey() const noexcept = 0;
    };

    // -------------------------------------------------------------------------
    //  Embedded public key placeholder. NO REAL KEY IN THE SKELETON.
    //  At release this is replaced by the 32-byte base64 Ed25519 public key that
    //  matches the server's signing key. The matching PRIVATE key never leaves the
    //  signing service.
    // -------------------------------------------------------------------------
    struct EmbeddedKey
    {
        // Intentionally EMPTY in the skeleton. Do not ship until populated.
        static constexpr const char* kEd25519PublicKeyBase64 = "";

        static bool isPlaceholder() noexcept
        {
            return kEd25519PublicKeyBase64[0] == '\0';
        }
    };

    // -------------------------------------------------------------------------
    //  Fail-closed stub. Compiles and links with zero dependencies; verifies
    //  nothing. This is what lets the skeleton build in isolation. Because it
    //  returns false, any real licence is treated as invalid for now (correct
    //  fail-closed behaviour) while the trial path remains exercisable.
    // -------------------------------------------------------------------------
    class StubEd25519Verifier final : public ISignatureVerifier
    {
    public:
        bool verify(const Bytes& /*message*/, const Bytes& /*signature*/) const override
        {
            return false; // No key, no crypto -> deny. FAIL-CLOSED.
        }

        bool hasEmbeddedKey() const noexcept override
        {
            return ! EmbeddedKey::isPlaceholder();
        }
    };

    // TODO(hook-up): class Ed25519Verifier final : public ISignatureVerifier
    //   - decode EmbeddedKey::kEd25519PublicKeyBase64 once at construction
    //   - verify() -> crypto_sign_verify_detached(sig, msg, msglen, pk) == 0
    //   - constant-time; no logging of key material; no allocation on the hot path
    //     (verification only runs on load/registration, never in processBlock).
}
