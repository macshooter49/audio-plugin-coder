// OrganicsStub.cpp — tp104, integration-owned PLACEHOLDER for the Organics runtime (contract §2).
//
// CMakeLists.txt compiles THIS file only while Source/organics/OrganicEngine.cpp + OrganicsLibrary.cpp (the runtime
// agent's) do not exist, so the plugin links and every integration seam can be exercised before the merge. Every
// symbol of OrganicsApi.h is here, and every one is SILENT: the library has an empty index and answers every request
// with nullptr ("not installed"), and an engine renders nothing and is never active. The lead deletes this file once
// the real runtime has merged (the if(EXISTS …) in CMake then needs no edit).
#include "OrganicsApi.h"
#include <juce_events/juce_events.h>

namespace tw
{
    class OrganicInstrument {};

    OrganicsLibrary& OrganicsLibrary::get()
    {
        static OrganicsLibrary lib;
        return lib;
    }

    juce::File OrganicsLibrary::root() const
    {
        if (const char* env = std::getenv ("TERRAIN_ORGANICS_DIR"); env != nullptr && *env != 0)
            return juce::File (juce::String::fromUTF8 (env));
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("WavesCrate").getChildFile ("Terrain").getChildFile ("Organics");
    }

    juce::var OrganicsLibrary::index() { return juce::var (juce::Array<juce::var>()); }
    void OrganicsLibrary::rescan() {}

    void OrganicsLibrary::request (const juce::String&, std::function<void (std::shared_ptr<const OrganicInstrument>)> done)
    {
        if (! done) return;
        // the contract: the callback runs on the MESSAGE thread, never inside the caller
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        {
            juce::ignoreUnused (mm);
            juce::MessageManager::callAsync ([d = std::move (done)] { d (nullptr); });
        }
        else done (nullptr);
    }

    int          OrganicsLibrary::idToIndex (const juce::String&) const { return -1; }
    juce::String OrganicsLibrary::indexToId (int) const { return {}; }
    int          OrganicsLibrary::residentCount() const { return 0; }
    int64_t      OrganicsLibrary::residentBytes() const { return 0; }

    struct OrganicEngine::Impl {};

    OrganicEngine::OrganicEngine() : impl (std::make_unique<Impl>()) {}
    OrganicEngine::~OrganicEngine() = default;

    void  OrganicEngine::prepare (double, int) {}
    void  OrganicEngine::reset() noexcept {}
    void  OrganicEngine::setInstrument (std::shared_ptr<const OrganicInstrument>) noexcept {}
    void  OrganicEngine::noteOn (int, float, int, const float*, uint32_t) noexcept {}
    void  OrganicEngine::noteOff (bool) noexcept {}
    void  OrganicEngine::pedal (bool) noexcept {}
    void  OrganicEngine::kill() noexcept {}
    void  OrganicEngine::render (const OrganicParams&, float, float*, float*, int) noexcept {}
    bool  OrganicEngine::isActive() const noexcept { return false; }
    float OrganicEngine::readLevel() const noexcept { return 0.0f; }
    void  OrganicEngine::setNonRealtime (bool) noexcept {}
}
