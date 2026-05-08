// SampleLoader.h
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "SampleBuffer.h"
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
        };

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
