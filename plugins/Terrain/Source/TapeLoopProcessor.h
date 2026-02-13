#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
// TapeLoopProcessor: BPM-synced stereo tape looper
//
// Signal flow:
//   STOPPED    — pass-through (no effect on signal)
//   RECORDING  — captures input to buffer, pass-through output
//   PLAYING    — reads loop from buffer, mixes with live input
//   OVERDUBBING — reads existing loop, blends with new input via feedback,
//                 writes back. Output = input + existing loop content.
//
// Features:
//   - BPM-synced loop lengths: 1 beat, 2 beats, 1/2/4/8 bars, Free
//   - Auto-stop recording when synced length is reached
//   - Overdub with adjustable feedback (0% = replace, 100% = infinite layers)
//   - Degrade: one-pole lowpass on feedback path (tape wear per pass)
//   - Variable speed playback: -3x to +3x (10 stepped presets + freeform)
//   - Hermite interpolation for smooth variable-speed reads
//   - Speed locked to 1x during record/overdub (standard looper behavior)
//   - Single-level undo: snapshot before each recording, restore on undo
//   - Clear: wipe all loop content
//==============================================================================
class TapeLoopProcessor
{
public:
    static constexpr double MAX_BUFFER_SECONDS = 60.0;

    TapeLoopProcessor() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;
        maxSamples = static_cast<int>(sr * MAX_BUFFER_SECONDS);
        bufL.assign(static_cast<size_t>(maxSamples), 0.0f);
        bufR.assign(static_cast<size_t>(maxSamples), 0.0f);
        undoBufL.assign(static_cast<size_t>(maxSamples), 0.0f);
        undoBufR.assign(static_cast<size_t>(maxSamples), 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        loopLength = 0;
        writePos = 0;
        readPos = 0.0;
        hasContent_ = false;
        isFirstPass = true;
        prevWantRecord = false;
        prevWantPlay = false;
        degradeStateL = 0.0f;
        degradeStateR = 0.0f;
        hasUndo_ = false;
    }

    // Clear all loop content (double-click stop)
    void clear()
    {
        reset();
        std::fill(undoBufL.begin(), undoBufL.end(), 0.0f);
        std::fill(undoBufR.begin(), undoBufR.end(), 0.0f);
        undoLoopLength = 0;
    }

    // Snapshot current state before recording (for undo)
    void snapshotForUndo()
    {
        std::copy(bufL.begin(), bufL.end(), undoBufL.begin());
        std::copy(bufR.begin(), bufR.end(), undoBufR.begin());
        undoLoopLength = loopLength;
        undoHasContent = hasContent_;
        hasUndo_ = true;
    }

    // Restore from undo snapshot
    void restoreFromUndo()
    {
        if (!hasUndo_) return;
        std::copy(undoBufL.begin(), undoBufL.end(), bufL.begin());
        std::copy(undoBufR.begin(), undoBufR.end(), bufR.begin());
        loopLength = undoLoopLength;
        hasContent_ = undoHasContent;
        readPos = 0.0;
        writePos = 0;
        isFirstPass = false;
        hasUndo_ = false;
    }

    bool hasUndo() const { return hasUndo_; }

    //--------------------------------------------------------------------------
    // Process one stereo sample. Modifies wetL/wetR in-place.
    // wantRecord/wantPlay are passed by reference so we can auto-stop recording.
    // speedFreeform: if true, speedParam is continuous (-3..+3 mapped from 0..9)
    //--------------------------------------------------------------------------
    void processStereo(float& wetL, float& wetR,
                       bool& wantRecord, bool& wantPlay,
                       float loopLengthParam, float feedback,
                       float degrade, float speedParam,
                       float bpm, bool speedFreeform = false)
    {
        int lengthIdx = static_cast<int>(std::round(loopLengthParam));
        if (lengthIdx < 0) lengthIdx = 0;
        if (lengthIdx > 6) lengthIdx = 6;

        //--- Edge: recording just started ---
        if (wantRecord && !prevWantRecord)
        {
            // Snapshot for undo before any recording
            snapshotForUndo();

            if (!hasContent_ || !prevWantPlay)
            {
                // New recording (first ever, or was stopped before)
                std::fill(bufL.begin(), bufL.end(), 0.0f);
                std::fill(bufR.begin(), bufR.end(), 0.0f);
                writePos = 0;
                readPos = 0.0;
                isFirstPass = true;
                hasContent_ = true;
                degradeStateL = 0.0f;
                degradeStateR = 0.0f;

                if (lengthIdx < 6)
                    loopLength = calculateLoopSamples(lengthIdx, bpm);
                else
                    loopLength = 0; // Free mode — finalized when recording stops
            }
            else
            {
                // Overdub (was playing, user hit record)
                isFirstPass = false;
                // Sync write head to current read position
                writePos = static_cast<int>(readPos) % loopLength;
            }
        }

        //--- Edge: recording just stopped by user ---
        if (!wantRecord && prevWantRecord)
        {
            if (isFirstPass)
            {
                // Finalize loop (Free mode: use however much was recorded)
                if (loopLength == 0 && writePos > 0)
                    loopLength = writePos;
                isFirstPass = false;
                readPos = 0.0;
            }
        }

        prevWantRecord = wantRecord;
        prevWantPlay = wantPlay;

        //--- STOPPED ---
        if (!wantPlay && !wantRecord)
            return;

        //--- FIRST RECORDING ---
        if (wantRecord && isFirstPass)
        {
            if (writePos < maxSamples)
            {
                bufL[static_cast<size_t>(writePos)] = wetL;
                bufR[static_cast<size_t>(writePos)] = wetR;
                writePos++;
            }

            // Auto-stop when synced loop length is reached
            if (loopLength > 0 && writePos >= loopLength)
            {
                isFirstPass = false;
                readPos = 0.0;
                writePos = 0;
                wantRecord = false; // auto-stop recording, keep playing
            }
            // Free mode: if buffer full, auto-stop
            else if (loopLength == 0 && writePos >= maxSamples)
            {
                loopLength = maxSamples;
                isFirstPass = false;
                readPos = 0.0;
                writePos = 0;
                wantRecord = false;
            }

            // Output = pass-through (user hears themselves while recording)
            return;
        }

        //--- OVERDUB (recording on existing content) ---
        if (wantRecord && !isFirstPass && hasContent_ && loopLength > 0)
        {
            const int rp = writePos % loopLength;
            const float existL = bufL[static_cast<size_t>(rp)];
            const float existR = bufR[static_cast<size_t>(rp)];

            // Feedback with optional degrade filtering
            float fbL = existL * feedback;
            float fbR = existR * feedback;

            if (degrade > 0.001f)
            {
                // One-pole lowpass on feedback path (tape wear simulation)
                // alpha near 1.0 = clean, near 0.0 = heavy filtering
                const float alpha = 1.0f - degrade * 0.65f;
                degradeStateL = alpha * fbL + (1.0f - alpha) * degradeStateL;
                degradeStateR = alpha * fbR + (1.0f - alpha) * degradeStateR;
                fbL = degradeStateL;
                fbR = degradeStateR;
            }

            // Write: new input + degraded feedback
            bufL[static_cast<size_t>(rp)] = wetL + fbL;
            bufR[static_cast<size_t>(rp)] = wetR + fbR;

            // Output: live input + existing loop content (before overdub write)
            wetL += existL;
            wetR += existR;

            writePos = (writePos + 1) % loopLength;
            // Keep readPos in sync during overdub
            readPos = static_cast<double>(writePos);
            return;
        }

        //--- PLAYBACK (no recording) ---
        if (wantPlay && hasContent_ && loopLength > 0)
        {
            // 10 stepped presets: -3x, -2x, -1.5x, -1x, -0.5x, 0.5x, 1x, 1.5x, 2x, 3x
            static constexpr float speedMults[] = { -3.0f, -2.0f, -1.5f, -1.0f, -0.5f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
            float speed;
            if (speedFreeform)
            {
                // Freeform: map 0..9 → -3.0..+3.0 (skip 0 to avoid stall)
                float raw = -3.0f + (speedParam / 9.0f) * 6.0f;
                speed = (std::abs(raw) < 0.05f) ? 0.05f : raw;
            }
            else
            {
                int speedIdx = static_cast<int>(std::round(speedParam));
                if (speedIdx < 0) speedIdx = 0;
                if (speedIdx > 9) speedIdx = 9;
                speed = speedMults[speedIdx];
            }

            // Read with Hermite interpolation
            float loopL = hermiteRead(bufL, readPos, loopLength);
            float loopR = hermiteRead(bufR, readPos, loopLength);

            // Subtle degrade filtering on playback output
            if (degrade > 0.001f)
            {
                const float alpha = 1.0f - degrade * 0.4f;
                degradeStateL = alpha * loopL + (1.0f - alpha) * degradeStateL;
                degradeStateR = alpha * loopR + (1.0f - alpha) * degradeStateR;
                loopL = degradeStateL;
                loopR = degradeStateR;
            }

            // Mix loop playback into the wet signal
            wetL += loopL;
            wetR += loopR;

            // Advance read position
            readPos += static_cast<double>(speed);
            const double len = static_cast<double>(loopLength);
            while (readPos >= len) readPos -= len;
            while (readPos < 0.0) readPos += len;
        }
    }

    bool hasContent() const { return hasContent_; }

    float getProgress() const
    {
        if (loopLength <= 0) return 0.0f;
        double p = readPos;
        while (p < 0.0) p += loopLength;
        return static_cast<float>(std::fmod(p, static_cast<double>(loopLength)))
             / static_cast<float>(loopLength);
    }

    bool isRecordingFirstPass() const { return isFirstPass; }

private:
    int calculateLoopSamples(int lengthIdx, float bpm) const
    {
        // Beat counts: 1 beat, 2 beats, 1 bar(4), 2 bars(8), 4 bars(16), 8 bars(32)
        static constexpr int beatCounts[] = { 1, 2, 4, 8, 16, 32 };
        if (lengthIdx < 0 || lengthIdx >= 6) return 0;

        const float safeBpm = std::max(20.0f, bpm);
        const double secondsPerBeat = 60.0 / static_cast<double>(safeBpm);
        const int samples = static_cast<int>(beatCounts[lengthIdx] * secondsPerBeat * sr);
        return std::min(samples, maxSamples);
    }

    float hermiteRead(const std::vector<float>& buf, double pos, int length) const
    {
        double p = pos;
        while (p < 0.0) p += static_cast<double>(length);

        const int idx1 = static_cast<int>(p) % length;
        const int idx0 = (idx1 - 1 + length) % length;
        const int idx2 = (idx1 + 1) % length;
        const int idx3 = (idx1 + 2) % length;

        const float frac = static_cast<float>(p - std::floor(p));

        const float y0 = buf[static_cast<size_t>(idx0)];
        const float y1 = buf[static_cast<size_t>(idx1)];
        const float y2 = buf[static_cast<size_t>(idx2)];
        const float y3 = buf[static_cast<size_t>(idx3)];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sr = 44100.0;
    int maxSamples = 0;

    std::vector<float> bufL, bufR;

    int loopLength = 0;      // Current loop length in samples (0 = not set / Free mode recording)
    int writePos = 0;        // Write head position
    double readPos = 0.0;    // Fractional read head position (for variable speed)
    bool hasContent_ = false; // Whether buffer has recorded content
    bool isFirstPass = true;  // True during initial recording (vs overdub)
    bool prevWantRecord = false;
    bool prevWantPlay = false;

    // Degrade filter state (one-pole lowpass)
    float degradeStateL = 0.0f;
    float degradeStateR = 0.0f;

    // Undo snapshot (single-level)
    std::vector<float> undoBufL, undoBufR;
    int undoLoopLength = 0;
    bool undoHasContent = false;
    bool hasUndo_ = false;
};
