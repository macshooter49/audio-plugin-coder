#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
// TapeLoopProcessor: BPM-synced stereo tape looper
//
// Signal flow:
//   STOPPED    — pass-through (no effect on signal)
//   COUNT-IN   — 4-beat pre-roll before recording starts (pass-through)
//   RECORDING  — captures input to buffer, pass-through output
//   PLAYING    — reads loop from buffer, mixes with live input
//   OVERDUBBING — silently writes to buffer while playback continues.
//                 At 0% feedback replaces content; at 100% layers.
//
// Features:
//   - BPM-synced loop lengths: 1 beat, 2 beats, 1/2/4/8 bars, Free
//   - Dynamic loop resizing (extend/shrink while content exists)
//   - 4-beat count-in before recording (BPM-synced)
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
        reversalPrevL = 0.0f;
        reversalPrevR = 0.0f;
        reversalHoldL = 0.0f;
        reversalHoldR = 0.0f;
        tapeStopCounter = 0;
        tapeStartCounter = 0;
        overdubDistance = 0.0;
        prevOverdubReadPos = 0.0;
        prevWriteInputL = 0.0f;
        prevWriteInputR = 0.0f;
        overdubInputAccumL = 0.0;
        overdubInputAccumR = 0.0;
        overdubInputAccumCount = 0;
        overdubPrevWpCenter = -1;
        overdubPosOriginalL = 0.0f;
        overdubPosOriginalR = 0.0f;
        resetAntiAliasState();
        playFromSnapshot = false;
        snapBufL.clear();
        snapBufR.clear();
        countInActive = false;
        countInBeatIndex = -1;
        countInSampleCounter = 0;
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
        UndoState state;
        const int copyLen = (loopLength > 0) ? loopLength : maxSamples;
        state.bufL.assign(bufL.begin(), bufL.begin() + copyLen);
        state.bufR.assign(bufR.begin(), bufR.begin() + copyLen);
        state.loopLength = loopLength;
        state.hasContent = hasContent_;

        undoStack.push_back(std::move(state));

        if (static_cast<int>(undoStack.size()) > MAX_UNDO_LEVELS)
            undoStack.erase(undoStack.begin());
    }

    // Restore from most recent undo snapshot (pop from stack)
    void restoreFromUndo()
    {
        if (undoStack.empty()) return;

        auto& state = undoStack.back();

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

    // Edge fades removed — they destructively zeroed the first/last 768
    // samples of the buffer, causing audible silence at the loop wrap point.
    // The playback crossfade (XFADE_LEN) now handles seamless boundaries.
    void applyEdgeFades(int /*fadeLen*/ = 768)
    {
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
    // Dynamic loop length resizing. Call once per processBlock (not per sample).
    // When the user changes the length knob while content exists, resize the loop:
    //   Extending → expose buffer content past the old boundary (non-destructive
    //               so a previously longer recording can be reclaimed)
    //   Shrinking → wrap read/write positions into new range; buffer past the
    //               new boundary is preserved so the user can extend back
    //   Free mode → keep current length unchanged
    //--------------------------------------------------------------------------
    void updateLength(float loopLengthParam, float bpm)
    {
        if (!hasContent_ || isFirstPass || countInActive) return;
        if (loopLength <= 0) return;

        int lengthIdx = static_cast<int>(std::round(loopLengthParam));
        if (lengthIdx < 0) lengthIdx = 0;
        if (lengthIdx > 6) lengthIdx = 6;

        // Free mode: keep current length
        if (lengthIdx >= 6) return;

        int newLength = calculateLoopSamples(lengthIdx, bpm);
        if (newLength <= 0 || newLength == loopLength) return;

        // Non-destructive resize: when extending, expose whatever was previously
        // in the buffer past the old boundary (silent if never recorded, or the
        // tail of an earlier longer recording). When shrinking, leave the audio
        // past the new boundary in the buffer untouched so the user can grow
        // back to the original length without losing material.
        newLength = std::min(newLength, maxSamples);
        loopLength = newLength;

        // Wrap positions into new range
        if (loopLength > 0)
        {
            readPos = std::fmod(readPos, static_cast<double>(loopLength));
            if (readPos < 0.0) readPos += static_cast<double>(loopLength);
            writePos = writePos % loopLength;
        }

        applyEdgeFades(256);
    }

    //--------------------------------------------------------------------------
    // Process one stereo sample. Modifies wetL/wetR in-place.
    // wantRecord/wantPlay are passed by reference so we can auto-stop recording.
    // speedFreeform: if true, speedParam is continuous (-3..+3 mapped from 0..9)
    // preTapeL/R: signal before tape processor (used for overdub writing to
    //             prevent wow/flutter accumulation in the buffer)
    // externalFeedActive: true when the host (PluginProcessor) is routing the
    //             loop output back into preTape via feed-to-grain or similar.
    //             When true, the tape loop's internal self-feedback at slow
    //             speeds is disabled to avoid doubling loop content.
    //--------------------------------------------------------------------------
    void processStereo(float& wetL, float& wetR,
                       bool& wantRecord, bool& wantPlay,
                       float loopLengthParam, float feedback,
                       float degrade, float speedParam,
                       float bpm, bool speedFreeform = false,
                       float preTapeL = 0.0f, float preTapeR = 0.0f,
                       bool externalFeedActive = false)
    {
        int lengthIdx = static_cast<int>(std::round(loopLengthParam));
        if (lengthIdx < 0) lengthIdx = 0;
        if (lengthIdx > 6) lengthIdx = 6;

        // Track whether we were counting in at the start of this sample
        const bool wasCountingIn = countInActive;

        //--- Cancel count-in if user un-pressed record ---
        if (!wantRecord && countInActive)
        {
            countInActive = false;
            countInBeatIndex = -1;
        }

        //--- Edge: recording just requested → start count-in ---
        if (wantRecord && !prevWantRecord)
        {
            countInActive = true;
            countInBeatIndex = 0;
            countInSampleCounter = 0;
            countInSamplesPerBeat = static_cast<int>(sr * 60.0
                / static_cast<double>(std::max(20.0f, bpm)));
            countInWasOverdub = hasContent_;

            // Snap loop position to top the moment record is pressed (when
            // there's existing content). User intent: "stop everything and
            // start at the top of the head" — they want to FEEL the rewind
            // immediately, not wait until count-in finishes. Count-in audio
            // then plays from the start of the loop.
            if (countInWasOverdub)
            {
                readPos = 0.0;
                writePos = 0;
            }

            // Consume the wantPlay transition so tape start effect
            // doesn't fire during count-in (we'll trigger it after)
            prevWantPlay = wantPlay;
        }

        //--- Count-in processing ---
        if (countInActive)
        {
            countInSampleCounter++;
            if (countInSampleCounter >= countInSamplesPerBeat)
            {
                countInSampleCounter = 0;
                countInBeatIndex++;
            }

            if (countInBeatIndex >= 4)
            {
                // Count-in complete — start actual recording
                countInActive = false;
                countInBeatIndex = -1;

                snapshotForUndo();

                if (!countInWasOverdub)
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
                    // Overdub from position 0. User intent for pressing record
                    // is "stop and start at the top of the head" — rewind so
                    // the new pass aligns with the loop boundary regardless of
                    // where readPos happened to be when count-in ended. Count-in
                    // itself still plays the loop audibly (so feed-to-grain has
                    // signal during pre-roll); only the transition after
                    // count-in jumps back to 0.
                    isFirstPass = false;
                    readPos = 0.0;
                    writePos = 0;
                    overdubSamplesWritten = 0;
                    overdubDistance = 0.0;
                    prevOverdubReadPos = 0.0;
                    overdubInputAccumL = 0.0;
                    overdubInputAccumR = 0.0;
                    overdubInputAccumCount = 0;
                    overdubPrevWpCenter = -1;
                    overdubPosOriginalL = 0.0f;
                    overdubPosOriginalR = 0.0f;
                    resetAntiAliasState();

                    snapBufL.assign(bufL.begin(), bufL.begin() + loopLength);
                    snapBufR.assign(bufR.begin(), bufR.begin() + loopLength);
                    playFromSnapshot = true;

                    // Trigger tape start motor effect (playback begins now)
                    tapeStartSamples = static_cast<int>(sr * 0.12);
                    tapeStartCounter = tapeStartSamples;
                    tapeStopCounter = 0;
                }

                prevWantRecord = true;
                prevWantPlay = wantPlay;
                // Fall through to normal processing for this first sample
            }
            else
            {
                // Still counting in — fully silent pre-roll. No audible loop,
                // no readPos advance (so loop progress indicator stays at 0).
                // After count-in completes, playback resumes from the top
                // and recording starts. User intent: count-in should be
                // invisible/silent, not act like a play button.
                prevWantRecord = wantRecord;
                prevWantPlay = wantPlay;
                return;
            }
        }

        //--- Edge: recording just stopped by user (skip if was counting in) ---
        if (!wantRecord && prevWantRecord && !wasCountingIn)
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
                applyEdgeFades(256); // Re-apply edge fades to prevent loop-boundary clicks
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

        //--- OVERDUB (background recording on existing content) ---
        // Unified write strategy: write head follows read head at all speeds.
        //   |speed| > 1: gap-filling between sparse write positions prevents
        //                Hermite interpolation bleed.
        //   |speed| < 1: multiple input samples may map to the same buffer
        //                position; last write wins (decimation). This is
        //                acceptable for typical playback ranges and avoids
        //                the buffer-wrap problem that 1x sequential writes
        //                caused at slow speeds (writer would wrap the buffer
        //                ~1/speed times during one read pass, overwriting
        //                content the reader had not yet visited).
        if (wantRecord && !isFirstPass && hasContent_ && loopLength > 0)
        {
            const double lenD = static_cast<double>(loopLength);
            const double absSpeed = static_cast<double>(std::abs(speed));

            // Write follows read head — matches playback position at all speeds
            double curOdPos = readPos;
            while (curOdPos < 0.0) curOdPos += lenD;
            curOdPos = std::fmod(curOdPos, lenD);

            double stepDist = 0.0;
            if (overdubSamplesWritten > 0)
            {
                stepDist = curOdPos - prevOverdubReadPos;
                if (speed > 0.0f && stepDist < 0.0) stepDist += lenD;
                if (speed < 0.0f && stepDist > 0.0) stepDist -= lenD;
                stepDist = std::abs(stepDist);
            }

            const int wpCenter = static_cast<int>(curOdPos) % loopLength;
            prevOverdubReadPos = curOdPos;

            // Internal self-feedback at slow speeds. The tape deck captures
            // its own slowed playback output directly into the overdub source,
            // so the user does NOT need to route through the grain engine
            // (which would otherwise impart its granular character on every
            // speed-change re-record). The loop self-feed is already
            // bandlimited (a slowed signal has no content above speed×Nyquist)
            // so it does not need to pass through the AA pre-filter.
            //
            // Disabled when externalFeedActive: the host is already routing
            // the loop output back into preTape via feed-to-grain (or similar)
            // and we'd double the loop content otherwise.
            float loopFeedL = 0.0f;
            float loopFeedR = 0.0f;
            if (absSpeed < 1.0 && !externalFeedActive)
            {
                const auto& srcL = (playFromSnapshot && !snapBufL.empty()) ? snapBufL : bufL;
                const auto& srcR = (playFromSnapshot && !snapBufR.empty()) ? snapBufR : bufR;
                loopFeedL = hermiteRead(srcL, readPos, loopLength);
                loopFeedR = hermiteRead(srcR, readPos, loopLength);
            }

            // Anti-aliasing pre-filter: 4th-order Butterworth lowpass with
            // cutoff tracking the decimated Nyquist. At |speed| >= 1 the cutoff
            // sits at 0.45 fs (essentially transparent). At slow speeds it
            // suppresses content above the new Nyquist on the LIVE input
            // (preTape) before the boxcar accumulator decimates into the buffer.
            const double aaSpeed = std::min(1.0, absSpeed);
            updateAntiAliasCoeffs(aaSpeed * 0.45);
            const float aaPreTapeL = applyAntiAlias(preTapeL, aaSecL);
            const float aaPreTapeR = applyAntiAlias(preTapeR, aaSecR);
            const float aaInL = aaPreTapeL + loopFeedL;
            const float aaInR = aaPreTapeR + loopFeedR;

            // Detect new buffer position. At |speed| < 1 multiple consecutive
            // samples land on the same wpCenter; we accumulate them and write
            // the running average (boxcar AA). At |speed| >= 1 wpCenter changes
            // every sample, so accumulator resets each iteration and the
            // average degenerates to the single current sample (current behavior).
            const bool isNewPosition = (overdubSamplesWritten == 0)
                                       || (wpCenter != overdubPrevWpCenter);

            if (isNewPosition)
            {
                overdubInputAccumL = 0.0;
                overdubInputAccumR = 0.0;
                overdubInputAccumCount = 0;
                // Snapshot original content for feedback (frozen for the dwell
                // so re-writes during slow-speed dwells do not compound).
                overdubPosOriginalL = bufL[static_cast<size_t>(wpCenter)];
                overdubPosOriginalR = bufR[static_cast<size_t>(wpCenter)];

                // Advance degrade filter state once per new position. Updating
                // every sample at slow speeds would over-converge the filter.
                if (degrade > 0.001f && feedback > 0.01f)
                {
                    const float alpha = 1.0f - degrade * 0.65f;
                    const float fbInL = overdubPosOriginalL * feedback;
                    const float fbInR = overdubPosOriginalR * feedback;
                    degradeStateL = alpha * fbInL + (1.0f - alpha) * degradeStateL;
                    degradeStateR = alpha * fbInR + (1.0f - alpha) * degradeStateR;
                }
                else if (feedback <= 0.01f)
                {
                    degradeStateL = 0.0f;
                    degradeStateR = 0.0f;
                }
            }

            // Always accumulate current (anti-aliased) input
            overdubInputAccumL += static_cast<double>(aaInL);
            overdubInputAccumR += static_cast<double>(aaInR);
            overdubInputAccumCount++;

            // Running boxcar average over the current dwell
            const float avgInputL = static_cast<float>(
                overdubInputAccumL / static_cast<double>(overdubInputAccumCount));
            const float avgInputR = static_cast<float>(
                overdubInputAccumR / static_cast<double>(overdubInputAccumCount));

            // Overdub fade envelope based on distance traveled through buffer
            static constexpr int OD_FADE = 256;
            float inputGain = 1.0f;
            if (overdubDistance < static_cast<double>(OD_FADE))
                inputGain = static_cast<float>(overdubDistance / static_cast<double>(OD_FADE));
            const double distRemaining = lenD - overdubDistance;
            if (loopLength > OD_FADE * 2 && distRemaining <= static_cast<double>(OD_FADE))
                inputGain = std::min(inputGain,
                    static_cast<float>(distRemaining / static_cast<double>(OD_FADE)));

            // Feedback gain: use target level directly, no ramp.
            const float fbGain = feedback;

            // Feedback uses snapshotted original (not live buf — prevents
            // compounding when the same position is rewritten multiple times
            // during a slow-speed dwell). Degrade state was advanced above.
            float fbL = overdubPosOriginalL * fbGain;
            float fbR = overdubPosOriginalR * fbGain;
            if (degrade > 0.001f && fbGain > 0.01f)
            {
                fbL = degradeStateL;
                fbR = degradeStateR;
            }

            const float newL = avgInputL * inputGain + fbL;
            const float newR = avgInputR * inputGain + fbR;

            // Write at determined position. At slow speeds we re-write the
            // same position each sample with a progressively-better average;
            // the final write at each position holds the full boxcar mean.
            bufL[static_cast<size_t>(wpCenter)] = newL;
            bufR[static_cast<size_t>(wpCenter)] = newR;

            overdubPrevWpCenter = wpCenter;

            // Fill gap positions with interpolated input for speeds > 1x.
            // Each gap gets its own interpolated input value (not a duplicate)
            // and reads its own existing content for feedback — eliminates the
            // sample-rate-reduction artifacts that caused freeform speed degradation.
            if (overdubSamplesWritten > 0)
            {
                const int numGaps = std::max(0, static_cast<int>(std::ceil(stepDist)) - 1);
                for (int i = 1; i <= numGaps; ++i)
                {
                    int gapPos;
                    if (speed > 0.0f)
                        gapPos = (wpCenter - i + loopLength) % loopLength;
                    else
                        gapPos = (wpCenter + i) % loopLength;

                    // Interpolate input between previous and current frame
                    const float t = static_cast<float>(i) / static_cast<float>(numGaps + 1);
                    const float interpL = aaInL + (prevWriteInputL - aaInL) * t;
                    const float interpR = aaInR + (prevWriteInputR - aaInR) * t;

                    // Read THIS position's existing content for feedback
                    const float gapExL = bufL[static_cast<size_t>(gapPos)] * fbGain;
                    const float gapExR = bufR[static_cast<size_t>(gapPos)] * fbGain;

                    bufL[static_cast<size_t>(gapPos)] = interpL * inputGain + gapExL;
                    bufR[static_cast<size_t>(gapPos)] = interpR * inputGain + gapExR;
                }
            }

            // Track input for next sample's gap interpolation
            prevWriteInputL = aaInL;
            prevWriteInputR = aaInR;

            // Advance write position (writer follows read head)
            writePos = wpCenter;

            // Track distance for auto-stop (one full pass through buffer)
            overdubDistance += stepDist;
            overdubSamplesWritten++;

            if (overdubDistance >= lenD)
            {
                wantRecord = false;
                playFromSnapshot = false;
                applyEdgeFades(256);
            }

            // Fall through to PLAYBACK
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
                tapeStartMult = t * t;
                tapeStartCounter--;
            }

            // During overdub, read from frozen snapshot
            const auto& readBufL = playFromSnapshot ? snapBufL : bufL;
            const auto& readBufR = playFromSnapshot ? snapBufR : bufR;

            // Detect speed direction reversal (forward ↔ reverse)
            // Crossfade from pre-reversal output to new direction instead of
            // dipping through zero (which causes an audible click).
            static constexpr int REVERSAL_FADE = 768;
            if ((speed > 0.0f && prevPlaybackSpeed < 0.0f) ||
                (speed < 0.0f && prevPlaybackSpeed > 0.0f))
            {
                reversalFadeCounter = REVERSAL_FADE;
                reversalPrevL = reversalHoldL;
                reversalPrevR = reversalHoldR;
            }
            prevPlaybackSpeed = speed;

            // Read with Hermite interpolation
            float loopL = hermiteRead(readBufL, readPos, loopLength);
            float loopR = hermiteRead(readBufR, readPos, loopLength);

            // Speed reversal crossfade: blend from stored pre-reversal output
            // into new reversed output (avoids zero-crossing click)
            if (reversalFadeCounter > 0)
            {
                const float t = static_cast<float>(reversalFadeCounter)
                              / static_cast<float>(REVERSAL_FADE);
                const float fadeOut = t * t;        // Squared curve — smooth tail
                const float fadeIn  = 1.0f - fadeOut;
                loopL = reversalPrevL * fadeOut + loopL * fadeIn;
                loopR = reversalPrevR * fadeOut + loopR * fadeIn;
                reversalFadeCounter--;
            }

            // Store current output for future reversal crossfade
            reversalHoldL = loopL;
            reversalHoldR = loopR;

            // No playback crossfade — Hermite interpolation already wraps
            // indices via modulo, producing smooth sub-sample transitions.
            // The old crossfade had a design flaw that created a discontinuity
            // at the exact wrap point (reading from mismatched positions).

            // Subtle degrade filtering on playback output
            if (degrade > 0.001f)
            {
                const float alpha = 1.0f - degrade * 0.4f;
                degradeStateL = alpha * loopL + (1.0f - alpha) * degradeStateL;
                degradeStateR = alpha * loopR + (1.0f - alpha) * degradeStateR;
                loopL = degradeStateL;
                loopR = degradeStateR;
            }

            // Apply tape start acceleration
            loopL *= tapeStartMult;
            loopR *= tapeStartMult;

            // Mix loop playback into the wet signal
            wetL += loopL;
            wetR += loopR;

            // Advance read position
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
        // During the first synced recording the tracker should advance with
        // the write head (the read head doesn't move on the first pass), so
        // the user can see how far through the bar count the recording is.
        // After the first pass the read head drives progress as normal.
        // Free mode (loopLength == 0) returns 0 above.
        const double pos = isFirstPass ? static_cast<double>(writePos) : readPos;
        double p = pos;
        while (p < 0.0) p += loopLength;
        return static_cast<float>(std::fmod(p, static_cast<double>(loopLength)))
             / static_cast<float>(loopLength);
    }

    // Returns current count-in beat (0-3) or -1 if not counting in
    int getCountInBeat() const { return countInActive ? countInBeatIndex : -1; }

    bool isRecordingFirstPass() const { return isFirstPass; }
    bool isCountingIn() const { return countInActive; }

private:
    int calculateLoopSamples(int lengthIdx, float bpm) const
    {
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

    //==========================================================================
    // Anti-aliasing pre-filter for slow-speed overdub
    //
    // 4th-order Butterworth lowpass, implemented as two cascaded second-order
    // sections (biquads). At |speed| >= 1 the cutoff sits near Nyquist and
    // the filter is essentially transparent. At slow speeds the cutoff
    // tracks the decimated Nyquist (|speed| × Nyquist) so high-frequency
    // content that would otherwise alias into the audible band is suppressed
    // BEFORE the boxcar accumulator decimates the input into the buffer.
    //
    // Boxcar alone averages 1-2 samples per dwell at speeds close to 1×,
    // which is too few for meaningful anti-aliasing. The biquad cascade
    // provides ~24 dB/oct rolloff regardless of speed.
    //==========================================================================
    struct BiquadSection
    {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };
    BiquadSection aaSecL[2];
    BiquadSection aaSecR[2];
    double aaCachedFcNorm = -1.0;
    float aaB0[2] = { 0.0f, 0.0f }, aaB1[2] = { 0.0f, 0.0f }, aaB2[2] = { 0.0f, 0.0f };
    float aaA1[2] = { 0.0f, 0.0f }, aaA2[2] = { 0.0f, 0.0f };

    void updateAntiAliasCoeffs(double fcNorm)
    {
        if (fcNorm < 0.0001) fcNorm = 0.0001;
        if (fcNorm > 0.49)   fcNorm = 0.49;
        if (std::abs(fcNorm - aaCachedFcNorm) < 1e-5) return;
        aaCachedFcNorm = fcNorm;

        // Q values for two cascaded biquad sections forming a 4th-order Butterworth
        static constexpr double Q_values[2] = { 0.5411961001461970, 1.3065629648763766 };
        static constexpr double TWO_PI = 6.283185307179586;

        const double w0 = TWO_PI * fcNorm;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double oneMinusCos = 1.0 - cosW0;

        for (int i = 0; i < 2; ++i)
        {
            const double alpha = sinW0 / (2.0 * Q_values[i]);
            const double invA0 = 1.0 / (1.0 + alpha);

            aaB0[i] = static_cast<float>((oneMinusCos * 0.5) * invA0);
            aaB1[i] = static_cast<float>(oneMinusCos * invA0);
            aaB2[i] = aaB0[i];
            aaA1[i] = static_cast<float>((-2.0 * cosW0) * invA0);
            aaA2[i] = static_cast<float>((1.0 - alpha) * invA0);
        }
    }

    float applyAntiAlias(float in, BiquadSection sections[2])
    {
        float x = in;
        for (int i = 0; i < 2; ++i)
        {
            BiquadSection& s = sections[i];
            const float y = aaB0[i] * x + aaB1[i] * s.x1 + aaB2[i] * s.x2
                           - aaA1[i] * s.y1 - aaA2[i] * s.y2;
            s.x2 = s.x1;
            s.x1 = x;
            s.y2 = s.y1;
            s.y1 = y;
            x = y;
        }
        return x;
    }

    void resetAntiAliasState()
    {
        for (int i = 0; i < 2; ++i)
        {
            aaSecL[i] = BiquadSection{};
            aaSecR[i] = BiquadSection{};
        }
        aaCachedFcNorm = -1.0;
    }

    double sr = 44100.0;
    int maxSamples = 0;

    std::vector<float> bufL, bufR;

    int loopLength = 0;
    int writePos = 0;
    double readPos = 0.0;
    bool hasContent_ = false;
    bool isFirstPass = true;
    bool prevWantRecord = false;
    bool prevWantPlay = false;
    int overdubSamplesWritten = 0;
    double overdubDistance = 0.0;
    double prevOverdubReadPos = 0.0;
    float prevWriteInputL = 0.0f;   // Previous frame's input for gap interpolation
    float prevWriteInputR = 0.0f;

    // Boxcar anti-aliasing for slow-speed overdub. At |speed| < 1 multiple
    // consecutive input samples land on the same buffer position; instead of
    // last-write-wins (which is point decimation and aliases), we accumulate
    // them and write the running average — equivalent to a sinc-windowed
    // lowpass at the decimated Nyquist.
    double overdubInputAccumL = 0.0;
    double overdubInputAccumR = 0.0;
    int    overdubInputAccumCount = 0;
    int    overdubPrevWpCenter = -1;
    float  overdubPosOriginalL = 0.0f;   // Original buf content at current wpCenter
    float  overdubPosOriginalR = 0.0f;   // (frozen on first hit, used for feedback)
    float prevPlaybackSpeed = 1.0f;
    int reversalFadeCounter = 0;
    float reversalPrevL = 0.0f;     // Pre-reversal output for crossfade
    float reversalPrevR = 0.0f;
    float reversalHoldL = 0.0f;     // Last output sample (captured each sample)
    float reversalHoldR = 0.0f;

    // Tape stop/start motor effect
    int tapeStopCounter = 0;
    int tapeStopSamples = 0;
    int tapeStartCounter = 0;
    int tapeStartSamples = 0;

    // Degrade filter state
    float degradeStateL = 0.0f;
    float degradeStateR = 0.0f;

    // Overdub playback isolation
    std::vector<float> snapBufL, snapBufR;
    bool playFromSnapshot = false;

    // Count-in state (4-beat pre-roll before recording)
    bool countInActive = false;
    int countInBeatIndex = -1;       // -1 = no count-in, 0-3 = current beat
    int countInSampleCounter = 0;    // Samples elapsed in current beat
    int countInSamplesPerBeat = 0;   // Samples per beat at current BPM
    bool countInWasOverdub = false;  // Was content present when count-in started?

    // Multi-level undo stack
    struct UndoState
    {
        std::vector<float> bufL, bufR;
        int loopLength = 0;
        bool hasContent = false;
    };
    std::vector<UndoState> undoStack;
};
