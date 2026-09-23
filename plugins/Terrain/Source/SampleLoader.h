// SampleLoader.h
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "SampleBuffer.h"
#include "SampleKeyDetect.h"   // auto root-note detection (snap loaded sample to oscillator C)
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace tw
{
    /**
     * SampleLoader — async background loader for the sampler.
     *
     * - Decodes file on its own thread (10-min sample = ~230MB; can't block UI).
     * - Computes one mip-level of peak data (kPeakBins min/max pairs) for the
     *   waveform display.
     * - Writes the new buffer atomically into the SampleBuffer.
     * - Calls onProgress(0..1) and onComplete(Result) on the message thread.
     *
     * Length cap: 10 minutes. Rejects beyond, message goes through Result.errorMessage.
     */
    class SampleLoader
    {
    public:
        struct Result
        {
            bool         success = false;
            juce::String errorMessage;
            juce::String filename;
            juce::String absolutePath;
            double       sampleRate    = 0.0;
            int          lengthSamples = 0;
            int          numChannels   = 0;
            std::vector<float> peaksMin; // size: kPeakBins
            std::vector<float> peaksMax;
            // Auto key detection (for display/logging; the snap is already applied
            // to the SampleBuffer). keyDetected=false ⇒ un-pitched ⇒ no snap.
            bool         keyDetected     = false;
            int          keyMidiNote     = -1;
            double       keyFrequency    = 0.0;
            double       keyConfidence   = 0.0;
            int          keySnapSemitones = 0;
        };

        // One-time root-note analysis → writes the snap offset onto the buffer AND
        // mirrors the result into `r`. Runs on the loader's worker thread (off the
        // audio + message threads). See SampleKeyDetect.h.
        static void detectAndApplyKey (const std::shared_ptr<juce::AudioBuffer<float>>& buf,
                                       double nativeRate, SampleBuffer& target, Result& r)
        {
            if (buf == nullptr || buf->getNumSamples() <= 0) { target.clearKeyInfo(); return; }
            const auto k = tw::SampleKeyDetector::detect (buf->getArrayOfReadPointers(),
                                                          buf->getNumChannels(),
                                                          buf->getNumSamples(), nativeRate);
            target.setKeyInfo (k.voiced, k.midiNote, (float) k.frequencyHz,
                               (float) k.confidence, (float) k.snapSemitones);
            r.keyDetected      = k.voiced;
            r.keyMidiNote      = k.midiNote;
            r.keyFrequency     = k.frequencyHz;
            r.keyConfidence    = k.confidence;
            r.keySnapSemitones = k.snapSemitones;
        }

        // ~800px hero canvas × 2 DPR = 1600 peak slots.
        static constexpr int kPeakBins         = 1600;
        // 10-minute hard cap per the v0 design spec.
        static constexpr int kMaxSampleSeconds = 600;

        SampleLoader() { formatManager.registerBasicFormats(); }
        ~SampleLoader() { cancel(); }

        /** Kicks off an async load. Cancels any previous in-flight load first. */
        void load (const juce::File& file,
                   SampleBuffer& target,
                   std::function<void(float /*progress*/)> onProgress,
                   std::function<void(Result)> onComplete)
        {
            cancel();
            shouldStop.store (false);

            workerThread = std::thread ([this, file, &target,
                                         onProgress = std::move (onProgress),
                                         onComplete = std::move (onComplete)]()
            {
                Result r;
                r.filename     = file.getFileName();
                r.absolutePath = file.getFullPathName();

                std::unique_ptr<juce::AudioFormatReader> reader (
                    formatManager.createReaderFor (file));
                if (! reader)
                {
                    r.errorMessage = "Unsupported format. Use WAV, AIFF, FLAC, or MP3.";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                const auto totalSamples = (int) reader->lengthInSamples;
                const auto nativeRate   = reader->sampleRate;
                const auto numChans     = (int) reader->numChannels;

                if (totalSamples <= 0 || nativeRate <= 0.0 || numChans <= 0)
                {
                    r.errorMessage = "Could not read file (empty or invalid).";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                const double sampleSeconds = (double) totalSamples / nativeRate;
                if (sampleSeconds > (double) kMaxSampleSeconds)
                {
                    r.errorMessage = "Sample exceeds 10 min limit. Trim externally and re-import.";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                // Force at least 2 channels in the destination for downstream code that
                // assumes stereo. Mono → stereo gets duplicated below.
                auto buf = std::make_shared<juce::AudioBuffer<float>> (
                    juce::jmax (2, numChans), totalSamples);

                // Read in chunks for progress callbacks (~5% per tick).
                const int chunkSize = juce::jmax (16384, totalSamples / 20);
                int       readPos   = 0;
                while (readPos < totalSamples && ! shouldStop.load())
                {
                    const int thisChunk = juce::jmin (chunkSize, totalSamples - readPos);
                    reader->read (buf.get(), readPos, thisChunk, readPos, true, true);
                    readPos += thisChunk;

                    const float progress = (float) readPos / (float) totalSamples;
                    juce::MessageManager::callAsync ([cb = onProgress, progress]
                    {
                        if (cb) cb (progress);
                    });
                }

                if (shouldStop.load())
                {
                    r.errorMessage = "Cancelled.";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                // Mono → stereo: duplicate channel 0 to channel 1.
                if (numChans == 1)
                    buf->copyFrom (1, 0, *buf, 0, 0, totalSamples);

                // Compute peaks (single mip level — one pair of min/max per bin).
                r.peaksMin.assign (kPeakBins, 0.0f);
                r.peaksMax.assign (kPeakBins, 0.0f);
                const int samplesPerBin = juce::jmax (1, totalSamples / kPeakBins);
                const int activeChans   = buf->getNumChannels();
                for (int b = 0; b < kPeakBins; ++b)
                {
                    const int start = b * samplesPerBin;
                    const int end   = juce::jmin (start + samplesPerBin, totalSamples);
                    float minV = 0.0f, maxV = 0.0f;
                    for (int ch = 0; ch < activeChans; ++ch)
                    {
                        const auto* d = buf->getReadPointer (ch);
                        for (int i = start; i < end; ++i)
                        {
                            const float s = d[i];
                            minV = juce::jmin (minV, s);
                            maxV = juce::jmax (maxV, s);
                        }
                    }
                    r.peaksMin[b] = minV;
                    r.peaksMax[b] = maxV;
                }

                // Atomic swap into SampleBuffer.
                detectAndApplyKey (buf, nativeRate, target, r);   // auto snap loaded sample to oscillator C
                target.setSampleRate (nativeRate);
                target.store (buf);

                r.success       = true;
                r.sampleRate    = nativeRate;
                r.lengthSamples = totalSamples;
                r.numChannels   = numChans;
                juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (std::move (r)); });
            });
        }

        /** Async load from IN-MEMORY audio bytes (e.g. base64-decoded drop) — NO temp file, so it works
            even when the host sandbox / macOS TCC blocks disk writes. Mirrors load() but reads the audio
            from a MemoryInputStream instead of a juce::File. */
        void loadFromMemory (juce::MemoryBlock data,
                             juce::String filenameHint,
                             SampleBuffer& target,
                             std::function<void(float /*progress*/)> onProgress,
                             std::function<void(Result)> onComplete)
        {
            cancel();
            shouldStop.store (false);

            workerThread = std::thread ([this, data = std::move (data), filenameHint, &target,
                                         onProgress = std::move (onProgress),
                                         onComplete = std::move (onComplete)]()
            {
                Result r;
                r.filename     = filenameHint;
                r.absolutePath = filenameHint;

                std::unique_ptr<juce::AudioFormatReader> reader (
                    formatManager.createReaderFor (
                        std::make_unique<juce::MemoryInputStream> (data.getData(), data.getSize(), false)));
                if (! reader)
                {
                    r.errorMessage = "Unsupported format. Use WAV, AIFF, FLAC, or MP3.";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                const auto totalSamples = (int) reader->lengthInSamples;
                const auto nativeRate   = reader->sampleRate;
                const auto numChans     = (int) reader->numChannels;

                if (totalSamples <= 0 || nativeRate <= 0.0 || numChans <= 0)
                {
                    r.errorMessage = "Could not read file (empty or invalid).";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                const double sampleSeconds = (double) totalSamples / nativeRate;
                if (sampleSeconds > (double) kMaxSampleSeconds)
                {
                    r.errorMessage = "Sample exceeds 10 min limit. Trim externally and re-import.";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                auto buf = std::make_shared<juce::AudioBuffer<float>> (
                    juce::jmax (2, numChans), totalSamples);

                const int chunkSize = juce::jmax (16384, totalSamples / 20);
                int       readPos   = 0;
                while (readPos < totalSamples && ! shouldStop.load())
                {
                    const int thisChunk = juce::jmin (chunkSize, totalSamples - readPos);
                    reader->read (buf.get(), readPos, thisChunk, readPos, true, true);
                    readPos += thisChunk;
                    const float progress = (float) readPos / (float) totalSamples;
                    juce::MessageManager::callAsync ([cb = onProgress, progress] { if (cb) cb (progress); });
                }

                if (shouldStop.load())
                {
                    r.errorMessage = "Cancelled.";
                    juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (r); });
                    return;
                }

                if (numChans == 1)
                    buf->copyFrom (1, 0, *buf, 0, 0, totalSamples);

                r.peaksMin.assign (kPeakBins, 0.0f);
                r.peaksMax.assign (kPeakBins, 0.0f);
                const int samplesPerBin = juce::jmax (1, totalSamples / kPeakBins);
                const int activeChans   = buf->getNumChannels();
                for (int b = 0; b < kPeakBins; ++b)
                {
                    const int start = b * samplesPerBin;
                    const int end   = juce::jmin (start + samplesPerBin, totalSamples);
                    float minV = 0.0f, maxV = 0.0f;
                    for (int ch = 0; ch < activeChans; ++ch)
                    {
                        const auto* d = buf->getReadPointer (ch);
                        for (int i = start; i < end; ++i)
                        {
                            const float s = d[i];
                            minV = juce::jmin (minV, s);
                            maxV = juce::jmax (maxV, s);
                        }
                    }
                    r.peaksMin[b] = minV;
                    r.peaksMax[b] = maxV;
                }

                detectAndApplyKey (buf, nativeRate, target, r);   // auto snap loaded sample to oscillator C
                target.setSampleRate (nativeRate);
                target.store (buf);

                r.success       = true;
                r.sampleRate    = nativeRate;
                r.lengthSamples = totalSamples;
                r.numChannels   = numChans;
                juce::MessageManager::callAsync ([cb = onComplete, r] { if (cb) cb (std::move (r)); });
            });
        }

        /** Cancels an in-flight load (joins the worker thread). */
        void cancel()
        {
            shouldStop.store (true);
            if (workerThread.joinable()) workerThread.join();
        }

    private:
        juce::AudioFormatManager formatManager;
        std::thread              workerThread;
        std::atomic<bool>        shouldStop { false };
    };
}
