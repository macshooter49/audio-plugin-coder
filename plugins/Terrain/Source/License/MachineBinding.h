// =============================================================================
//  Terrain — Licensing SKELETON
//  MachineBinding.h — derive a stable, non-reversible machine fingerprint and
//  check a licence's bound machine id against this machine.
// -----------------------------------------------------------------------------
//  Purpose: bind a licence to the computer it was activated on so one licence
//  file cannot be freely copied to unlimited machines. Seat COUNTING is enforced
//  server-side (see design doc §Server, "1 of 3" in the settings mockup); the
//  client only proves "this licence names THIS machine".
//
//  Privacy boundary: the fingerprint is a salted one-way hash of stable hardware/
//  OS identifiers. Raw serial numbers / MAC addresses / PII are NEVER stored in
//  the licence or sent to the server — only the hash. No other machine data is
//  collected. This is enforcement scoped to Terrain, nothing else.
//
//  Stub returns an empty fingerprint (fail-closed: an empty id matches nothing).
// =============================================================================
#pragma once

#include "LicenseTypes.h"

#include <string>

namespace terrain::license
{
    class IMachineBinding
    {
    public:
        virtual ~IMachineBinding() = default;

        // A stable, salted, one-way fingerprint for THIS machine. Empty if it
        // could not be derived (treated as "no match" -> fail-closed).
        virtual MachineFingerprint currentFingerprint() const = 0;

        // True iff `boundMachineId` (from a licence) names this machine.
        virtual bool matches(const std::string& boundMachineId) const = 0;
    };

    // Fail-closed stub. Real impl (hook-up) hashes platform-stable ids:
    //   * macOS : IOPlatformUUID (IOKit)         -> SHA-256(salt || uuid)
    //   * Windows: MachineGuid (registry) / SMBIOS UUID
    //   * salt is a compile-time constant unique to Terrain
    // and compares with a constant-time equal. NEVER logs the raw ids.
    class StubMachineBinding final : public IMachineBinding
    {
    public:
        MachineFingerprint currentFingerprint() const override
        {
            return {}; // empty -> isValid() == false
        }

        bool matches(const std::string& boundMachineId) const override
        {
            const auto fp = currentFingerprint();
            return fp.isValid() && ! boundMachineId.empty() && fp.value == boundMachineId;
        }
    };
}
