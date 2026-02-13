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
//   OVERDUBBING — silently writes to buffer while playback continues.
//                 At 0% feedback replaces content; at 100% layers.
//
// Features:
//   - BPM-synced loop lengths: 1 beat, 2 beats, 1/2/4/8 bars, Free
//   - Auto-stop recording when synced length is reached
//   - Overdub with adjustable feedback (0% = replace, 100% = infinite layers)
//   - Degrade: one-pole lowpass on feedback path (tape wear per pass)
//   - Variable speed playback: -3x to +3x (10 stepped presets + freeform)
//   - Hermite interpolation for smooth variable-speed reads
//   - Multi-level undo (up to 10 levels): snapshot before each recording
//   - Clear: wipe all loop content and undo history
//==============================================================================
class TapeLoopProcessor
{
public:
    static constexpr double MAX_BUFFER_SECONDS = 60.0;
    static constexpr int MAX_UNDO_LEVELS = 10;

    TapeLoopProcessor() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;
        maxSamples = static_cast<int>(sr * MAX_BUFFER_SECONDS);
        bufL.assign(static_cast<size_t>(maxSamples), 0.0f);
        bufR.assign(static_cast<size_t>(maxSamples), 0.0f);
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
        prevPlaybackSpeed = 1.0f;
        reversalFadeCounter = 0;
        tapeStopCounter = 0;
        tapeStartCounter = 0;
        playFromSnapshot = false;
        snapBufL.clear();
        snapBufR.clear();
    }

    // Clear all loop content and undo history (double-click stop)
    void clear()
    {
        reset();
        undoStack.clear();
        snapBufL.clear();
        snapBufR.clear();
    }

    // Snapshot current state before recording (push to undo stack)
    void snapshotForUndo()
    {
        // Only snapshot if there's something meaningful to save
        // (either existing content or a loop length to preserve)
        UndoState state;
        const int copyLen = (loopLength > 0) ? loopLength : maxSamples;
        state.bufL.assign(bufL.begin(), bufL.begin() + copyLen);
        state.bufR.assign(bufR.begin(), bufR.begin() + copyLen);
        state.loopLength = loopLength;
        state.hasContent = hasContent_;

        undoStack.push_back(std::move(state));

        // Cap undo levels — drop oldest when exceeded
        if (static_cast<int>(undoStack.size()) > MAX_UNDO_LEVELS)
            undoStack.erase(undoStack.begin());
    }

    // Restore from most recent undo snapshot (pop from stack)
    void restoreFromUndo()
    {
        if (undoStack.empty()) return;

        auto& state = undoStack.back();

        // Clear buffer first, then copy the saved portion
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        std::copy(state.bufL.begin(), state.bufL.end(), bufL.begin());
        std::copy(state.bufR.begin(), state.bufR.end(), bufR.begin());
        loopLength = state.loopLength;
        hasContent_ = state.hasContent;

        undoStack.pop_back();

        readPos = 0.0;
        writePos = 0;
        isFirstPass = false;
    }

    bool hasUndo() const { return !undoStack.empty(); }
    int getUndoLevels() const { return static_cast<int>(undoStack.size()); }

    // Apply fade-in/fade-out to the recorded buffer edges to eliminate onset/offset clicks
    void applyEdgeFades(int fadeLen = 768)
    {
        if (loopLength <= fadeLen * 2 || !hasContent_) return;
        for (int i = 0; i < fadeLen; ++i)
        {
            const float gain = static_cast<float>(i) / static_cast<float>(fadeLen);
            // Fade in at the start
            bufL[static_cast<size_t>(i)] *= gain;
            bufR[static_cast<size_t>(i)] *= gain;
            // Fade out at the end
            bufL[static_cast<size_t>(loopLength - 1 - i)] *= gain;
            bufR[static_cast<size_t>(loopLength - 1 - i)] *= gain;
        }
    }

    // Apply retroactive fade-out at current write position when user manually stops overdub
    void applyOverdubFadeOut(int fadeLen = 256)
    {
        if (loopLength <= 0 || overdubSamplesWritten <= 0) return;
        const int actualFade = std::min(fadeLen, overdubSamplesWritten);
        for (int i = 0; i < actualFade; ++i)
        {
            const float gain = static_cast<float>(i + 1) / static_cast<float>(actualFade);
            const int pos = (writePos - 1 - i + loopLength) % loopLength;
            bufL[static_cast<size_t>(pos)] *= gain;
            bufR[static_cast<size_t>(pos)] *= gain;
        }
    }

    //--------------------------------------------------------------------------
    // Process one stereo sample. Modifies wetL/wetR in-place.
    // wantRecord/wantPlay are passed by reference so we can auto-stop recording.
    // speedFreeform: if true, speedParam is continuous (-3..+3 mapped from 0..9)
    // preTapeL/R: signal before tape processor (used for overdub writing to
    //             prevent wow/flutter accumulation in the buffer)
    //--------------------------------------------------------------------------
    void processStereo(float& wetL, float& wetR,
                       bool& wantRecord, bool& wantPlay,
                       float loopLengthParam, float feedback,
                       float degrade, float speedParam,
                       float bpm, bool speedFreeform = false,
                       float preTapeL = 0.0f, float preTapeR = 0.0f)
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
                overdubSamplesWritten = 0; // Track for auto-stop after one pass

                // Snapshot buffer for playback isolation during overdub.
                // Playback reads from this frozen copy while the write head
                // modifies the live buffer. Prevents feed-to-grain compounding
                // (grain engine always processes original content, never its own output).
                snapBufL.assign(bufL.begin(), bufL.begin() + loopLength);
                snapBufR.assign(bufR.begin(), bufR.begin() + loopLength);
                playFromSnapshot = true;
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
                applyEdgeFades(); // Smooth the loop edges to prevent clicks
            }
            else if (loopLength > 0 && overdubSamplesWritten > 0
                     && overdubSamplesWritten < loopLength)
            {
                // Manual overdub stop mid-pass — retroactive fade-out to prevent click
                applyOverdubFadeOut();
            }

            // End playback isolation — switch back to live buffer
            playFromSnapshot = false;
        }

        //--- Tape stop/start effect (decorative motor slowdown/speedup) ---
        if (!wantPlay && prevWantPlay && hasContent_ && loopLength > 0 && !isFirstPass)
        {
            tapeStopSamples = static_cast<int>(sr * 0.2); // 200ms deceleration
            tapeStopCounter = tapeStopSamples;
        }
        if (wantPlay && !prevWantPlay && hasContent_ && loopLength > 0 && !isFirstPass)
        {
            tapeStartSamples = static_cast<int>(sr * 0.12); // 120ms acceleration
            tapeStartCounter = tapeStartSamples;
            tapeStopCounter = 0; // Cancel any in-progress tape stop
        }

        prevWantRecord = wantRecord;
        prevWantPlay = wantPlay;

        //--- STOPPED ---
        if (!wantPlay && !wantRecord)
        {
            // Tape stop effect: brief deceleration tail when playback stops
            if (tapeStopCounter > 0 && hasContent_ && loopLength > 0)
            {
                const float t = static_cast<float>(tapeStopCounter)
                              / static_cast<float>(std::max(1, tapeStopSamples));
                const float mult = t * t; // Squared curve — natural motor deceleration

                const auto& rBufL = playFromSnapshot ? snapBufL : bufL;
                const auto& rBufR = playFromSnapshot ? snapBufR : bufR;
                float loopL = hermiteRead(rBufL, readPos, loopLength) * mult;
                float loopR = hermiteRead(rBufR, readPos, loopLength) * mult;

                wetL += loopL;
                wetR += loopR;

                // Advance at decelerating speed (pitch drops naturally)
                readPos += static_cast<double>(prevPlaybackSpeed * mult);
                const double len = static_cast<double>(loopLength);
                while (readPos >= len) readPos -= len;
                while (readPos < 0.0) readPos += len;

                tapeStopCounter--;
            }
            return;
        }

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
                applyEdgeFades(); // Smooth the loop edges to prevent clicks
            }
            // Free mode: if buffer full, auto-stop
            else if (loopLength == 0 && writePos >= maxSamples)
            {
                loopLength = maxSamples;
                isFirstPass = false;
                readPos = 0.0;
                writePos = 0;
                wantRecord = false;
                applyEdgeFades();
            }

            // Output = pass-through (user hears themselves while recording)
            return;
        }

        //--- Compute current speed (shared by overdub + playback) ---
        static constexpr float speedMults[] = { -3.0f, -2.0f, -1.5f, -1.0f, -0.5f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
        float speed = 1.0f;
        if (hasContent_ && loopLength > 0)
        {
            if (speedFreeform)
            {
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
        }

        //--- OVERDUB (silent background recording on existing content) ---
        // The loop keeps playing normally — recording just updates the buffer.
        // At 0% feedback the old content is replaced; at 100% it layers.
        if (wantRecord && !isFirstPass && hasContent_ && loopLength > 0)
        {
            const int wp = writePos % loopLength;
            const float existL = bufL[static_cast<size_t>(wp)];
            const float existR = bufR[static_cast<size_t>(wp)];

            // Feedback with optional degrade filtering
            float fbL = existL * feedback;
            float fbR = existR * feedback;

            if (degrade > 0.001f)
            {
                const float alpha = 1.0f - degrade * 0.65f;
                degradeStateL = alpha * fbL + (1.0f - alpha) * degradeStateL;
                degradeStateR = alpha * fbR + (1.0f - alpha) * degradeStateR;
                fbL = degradeStateL;
                fbR = degradeStateR;
            }

            // Overdub fade envelope — smooth layer transitions
            static constexpr int OD_FADE = 256;
            float inputGain = 1.0f;
            if (overdubSamplesWritten < OD_FADE)
                inputGain = static_cast<float>(overdubSamplesWritten) / static_cast<float>(OD_FADE);
            if (loopLength > OD_FADE && (loopLength - overdubSamplesWritten) <= OD_FADE)
                inputGain = std::min(inputGain,
                    static_cast<float>(loopLength - overdubSamplesWritten) / static_cast<float>(OD_FADE));

            // Write: faded new input + degraded feedback (write head always at 1x)
            // Use preTapeL/R (grain+filter output WITHOUT tape processor effects)
            // so wow/flutter/saturation never accumulate in the buffer across passes.
            // Tape effects are applied in real-time during playback only.
            bufL[static_cast<size_t>(wp)] = preTapeL * inputGain + fbL;
            bufR[static_cast<size_t>(wp)] = preTapeR * inputGain + fbR;

            // Advance write head at 1x
            writePos = (writePos + 1) % loopLength;

            // Auto-stop overdub after one full pass
            overdubSamplesWritten++;
            if (overdubSamplesWritten >= loopLength)
            {
                wantRecord = false;
                playFromSnapshot = false; // Switch back to live buffer
            }

            // Fall through to PLAYBACK — loop keeps playing normally
        }

        //--- PLAYBACK (runs during both normal play and overdub) ---
        if (wantPlay && hasContent_ && loopLength > 0)
        {
            // Tape start acceleration (decorative motor speedup)
            float tapeStartMult = 1.0f;
            if (tapeStartCounter > 0)
            {
                const float t = 1.0f - static_cast<float>(tapeStartCounter)
                                     / static_cast<float>(std::max(1, tapeStartSamples));
                tapeStartMult = t * t; // Squared curve — natural motor acceleration
                tapeStartCounter--;
            }

            // During overdub, read from frozen snapshot to prevent feed-to-grain
            // compounding. The grain engine always processes original content.
            const auto& readBufL = playFromSnapshot ? snapBufL : bufL;
            const auto& readBufR = playFromSnapshot ? snapBufR : bufR;

            // Detect speed direction reversal (forward ↔ reverse)
            static constexpr int REVERSAL_FADE = 512;
            if ((speed > 0.0f && prevPlaybackSpeed < 0.0f) ||
                (speed < 0.0f && prevPlaybackSpeed > 0.0f))
                reversalFadeCounter = REVERSAL_FADE;
            prevPlaybackSpeed = speed;

            // Read with Hermite interpolation
            float loopL = hermiteRead(readBufL, readPos, loopLength);
            float loopR = hermiteRead(readBufR, readPos, loopLength);

            // Speed reversal fade-in (masks direction change click)
            if (reversalFadeCounter > 0)
            {
                const float fadeGain = 1.0f - static_cast<float>(reversalFadeCounter)
                                            / static_cast<float>(REVERSAL_FADE);
                loopL *= fadeGain;
                loopR *= fadeGain;
                reversalFadeCounter--;
            }

            // Crossfade near loop boundaries to eliminate wrap discontinuity clicks
            // (especially important for feed-to-grain where clicks get amplified)
            static constexpr int XFADE_LEN = 768;
            if (loopLength > XFADE_LEN * 2)
            {
                double posInLoop = readPos;
                while (posInLoop < 0.0) posInLoop += static_cast<double>(loopLength);
                posInLoop = std::fmod(posInLoop, static_cast<double>(loopLength));

                const double fadeStartFwd = static_cast<double>(loopLength - XFADE_LEN);
                if (posInLoop >= fadeStartFwd)
                {
                    // Near the end: blend with the start of the loop
                    const double d = posInLoop - fadeStartFwd;
                    const float alpha = 1.0f - static_cast<float>(d) / static_cast<float>(XFADE_LEN);
                    const float startL = hermiteRead(readBufL, d, loopLength);
                    const float startR = hermiteRead(readBufR, d, loopLength);
                    loopL = loopL * alpha + startL * (1.0f - alpha);
                    loopR = loopR * alpha + startR * (1.0f - alpha);
                }
                else if (posInLoop < static_cast<double>(XFADE_LEN) && speed < 0.0f)
                {
                    // Reverse playback near the start: blend with the end of the loop
                    const double d = posInLoop;
                    const float alpha = static_cast<float>(d) / static_cast<float>(XFADE_LEN);
                    const double endPos = static_cast<double>(loopLength - XFADE_LEN) + d;
                    const float endL = hermiteRead(readBufL, endPos, loopLength);
                    const float endR = hermiteRead(readBufR, endPos, loopLength);
                    loopL = loopL * alpha + endL * (1.0f - alpha);
                    loopR = loopR * alpha + endR * (1.0f - alpha);
                }
            }

            // Subtle degrade filtering on playback output
            if (degrade > 0.001f)
            {
                const float alpha = 1.0f - degrade * 0.4f;
                degradeStateL = alpha * loopL + (1.0f - alpha) * degradeStateL;
                degradeStateR = alpha * loopR + (1.0f - alpha) * degradeStateR;
                loopL = degradeStateL;
                loopR = degradeStateR;
            }

            // Apply tape start acceleration to output (pitch + volume ramp up together)
            loopL *= tapeStartMult;
            loopR *= tapeStartMult;

            // Mix loop playback into the wet signal
            wetL += loopL;
            wetR += loopR;

            // Advance read position (tape start scales speed for pitch ramp)
            readPos += static_cast<double>(speed * tapeStartMult);
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
    int overdubSamplesWritten = 0; // Counts samples for overdub auto-stop
    float prevPlaybackSpeed = 1.0f;   // Track speed for reversal detection
    int reversalFadeCounter = 0;      // Countdown for speed reversal fade-in

    // Tape stop/start motor effect (decorative)
    int tapeStopCounter = 0;          // Remaining samples in stop deceleration
    int tapeStopSamples = 0;          // Total duration of current stop ramp
    int tapeStartCounter = 0;         // Remaining samples in start acceleration
    int tapeStartSamples = 0;         // Total duration of current start ramp

    // Degrade filter state (one-pole lowpass)
    float degradeStateL = 0.0f;
    float degradeStateR = 0.0f;

    // Overdub playback isolation: during overdub, playback reads from this
    // frozen snapshot instead of the live buffer. Prevents feed-to-grain
    // compounding (grain engine always processes original content, not its own output).
    std::vector<float> snapBufL, snapBufR;
    bool playFromSnapshot = false;

    // Multi-level undo stack (up to MAX_UNDO_LEVELS snapshots)
    struct UndoState
    {
        std::vector<float> bufL, bufR;
        int loopLength = 0;
        bool hasContent = false;
    };
    std::vector<UndoState> undoStack;
};
