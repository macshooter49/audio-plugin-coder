// =============================================================================
//  Terrain — Licensing SKELETON
//  LicenseStore.h — persistence for the signed licence file and the trial marker.
// -----------------------------------------------------------------------------
//  On-disk locations (macOS), matching the settings mockup:
//    ~/Library/Application Support/Waves Crate/Terrain/Licence.json   (signed)
//    ~/Library/Application Support/Waves Crate/Terrain/Trial.dat      (checksummed)
//  Windows equivalent: %APPDATA%\Waves Crate\Terrain\ .
//
//  The store ONLY reads/writes Terrain's own files under its own app-support dir.
//  It never touches anything else on disk. Removing the licence (sign out) or the
//  trial marker only affects Terrain.
//
//  Interface is abstract so the manager can be tested with an in-memory store.
//  The real store (hook-up) uses juce::File + juce::PropertiesFile / JSON.
// =============================================================================
#pragma once

#include "LicenseTypes.h"

#include <optional>

namespace terrain::license
{
    class ILicenseStore
    {
    public:
        virtual ~ILicenseStore() = default;

        // Raw signed licence bytes, if a licence file is present.
        virtual std::optional<Bytes> readLicenseBlob() const = 0;
        virtual bool writeLicenseBlob(const Bytes& blob) = 0;
        virtual bool clearLicense() = 0;              // sign out

        // Trial marker (persisted TrialState). Read may fail -> nullopt.
        virtual std::optional<TrialState> readTrial() const = 0;
        virtual bool writeTrial(const TrialState& state) = 0;
    };

    // -------------------------------------------------------------------------
    //  In-memory store — SKELETON / TEST ONLY. Lets the self-test exercise the
    //  full trial lifecycle without touching the filesystem. Not shipped.
    // -------------------------------------------------------------------------
    class InMemoryStore final : public ILicenseStore
    {
    public:
        std::optional<Bytes> readLicenseBlob() const override { return license_; }
        bool writeLicenseBlob(const Bytes& blob) override { license_ = blob; return true; }
        bool clearLicense() override { license_.reset(); return true; }

        std::optional<TrialState> readTrial() const override { return trial_; }
        bool writeTrial(const TrialState& state) override { trial_ = state; return true; }

    private:
        std::optional<Bytes>      license_;
        std::optional<TrialState> trial_;
    };
}
