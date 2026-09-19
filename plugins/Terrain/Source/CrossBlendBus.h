#pragma once

// Deliberately JUCE-FREE (the FxModValue.h idiom) so a cert can include it without a plugin.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace tw
{
/*  ═══ tp53 — THE CROSS-BANK MODULATOR BUS ═══════════════════════════════════════════════════════
    Max: "EFGH cannot cross-blend with ABCD."  He is right, and the reason is architectural, not a
    missing menu row: oscillators E–H are a SECOND VOICE BANK (`ensureBankB` builds its own
    UnisonSynth with its own sixteen tw::SynthVoice objects, each reading A–D's parameter ids
    through the A→E remap).  A blend slot's modulator is `modPrev_[src]` — the previous sample of an
    oscillator rendered INSIDE THE SAME VOICE — so osc E's voice has no A to reach for.  There is no
    A in that object at all.

    THE BUS.  Every voice publishes the four modulator taps it already computes (`modPrev_`, the
    pre-gain mono sum each blend slot reads) into a row keyed by the MIDI note it is playing, at the
    ABSOLUTE sample position in the block.  A voice in the other bank playing that same note reads
    that row.  Nothing is re-rendered and no engine is duplicated: the modulator IS the real
    oscillator, whatever engine it is running, because it is that oscillator's own tap.

    ⚠️ THE TWO DIRECTIONS ARE NOT THE SAME, AND THE REASON IS THE RENDER ORDER.
    `processBlock` renders bank 0 and THEN bank 1 into the same scratch (PluginProcessor.cpp — the
    `bb->renderNextBlock` line right under `synthEngine.renderNextBlock`).  A row is zeroed by the
    first voice that CLAIMS it in a block, so:
      • bank 1 reading bank 0  — bank 0 has already written this block.  The tap is THIS block's,
        one sample delayed, exactly like every in-bank source.  E/F/G/H ← A/B/C/D is EXACT.
      • bank 0 reading bank 1  — bank 1 has not run yet, so the row still holds LAST block's
        samples, untouched.  A/B/C/D ← E/F/G/H is therefore a clean ONE BLOCK delay line (10.7 ms
        at 512/48k): contiguous across the boundary, no glitch, no discontinuity — block N reads
        block N−1 whole, block N+1 reads block N whole.  For a sustained modulator that is a phase
        offset and the FM spectrum is unchanged; on a fast sweep or a transient it lags.
    That asymmetry is FREE — it is what the single buffer buys — and it is why nothing here reorders
    the two banks: the order is what keeps a patch with no cross route bit-identical to tp52.

    ROWS are claimed by note, not by voice index: the two banks are separate Synthesisers and
    nothing guarantees they hand the same note to the same voice slot.  Two voices of one bank on
    the SAME note (a retrigger inside a release) share a row — the second one to claim wins, and the
    reader hears that one.  Rare, harmless, and stated rather than discovered.

    THREADS.  Written and read only on the audio thread, inside renderNextBlock.  `prepare` is
    message-thread (prepareToPlay) and is the only place that allocates.
    ═════════════════════════════════════════════════════════════════════════════════════════════ */
struct CrossBlendBus
{
    static constexpr int kBanks = 2;
    static constexpr int kOscs  = 4;
    static constexpr int kRows  = 32;   // 16 voices per bank; a row is a NOTE, so this is roomy

    // ── message thread, prepareToPlay only ──────────────────────────────────────────────────────
    void prepare (int maxBlockSamples)
    {
        maxBlock_ = std::max (16, maxBlockSamples);
        store_.assign ((size_t) kBanks * kRows * kOscs * (size_t) maxBlock_, 0.0f);
        for (int b = 0; b < kBanks; ++b)
            for (int r = 0; r < kRows; ++r)
                rows_[b][r] = Row {};
        block_ = 4;   // 0 means "never claimed"/"never wanted"; start clear of the two-block want window
        for (auto& b : want_) for (auto& w : b) w = 0;
        live_  = ! store_.empty();
    }

    bool isLive() const noexcept { return live_; }

    // ── audio thread, once per processBlock, BEFORE either bank renders ─────────────────────────
    void startBlock (int numSamples) noexcept
    {
        ++block_;
        blockLen_ = std::min (std::max (0, numSamples), maxBlock_);
    }

    /*  Claim this voice's own row and zero it for the block.  Returns the row's four writable
        lanes, or nullptr when the bus is not live / the note is out of range.
        ⚠️ The zeroing happens ONCE per block per row, on the first claim — a second segment of the
        same block (JUCE splits renderNextBlock at every MIDI event) must not wipe what the first
        segment already wrote, which is why the clear is behind the stamp and not the call.  */
    float* claim (int bank, int note) noexcept
    {
        if (! live_ || (unsigned) bank >= (unsigned) kBanks || note < 0) return nullptr;
        const int r = rowFor (bank, note, true);
        if (r < 0) return nullptr;
        Row& row = rows_[bank][r];
        if (row.stamp != block_)
        {
            row.stamp = block_;
            row.len   = blockLen_;
            std::fill_n (lane (bank, r, 0), (size_t) kOscs * (size_t) maxBlock_, 0.0f);
        }
        return lane (bank, r, 0);
    }

    /*  The other bank's row for this note, or nullptr when that note is not sounding over there.
        `outLen` is how many samples of it are valid — LAST block's length when the writer has not
        run yet this block, which is the A←E direction described above.  */
    const float* read (int otherBank, int note, int& outLen) const noexcept
    {
        outLen = 0;
        if (! live_ || (unsigned) otherBank >= (unsigned) kBanks || note < 0) return nullptr;
        const int r = rowFor (otherBank, note, false);
        if (r < 0) return nullptr;
        const Row& row = rows_[otherBank][r];
        if (row.stamp != block_ && row.stamp != block_ - 1) return nullptr;   // older than that is silence, not history
        outLen = row.len;
        return lane (otherBank, r, 0);
    }

    int laneStride() const noexcept { return maxBlock_; }

    /*  ═══ THE CROSS-FORCE ═════════════════════════════════════════════════════════════════════
        In-bank, a blend slot keeps its source ALIVE: `modSrcForce_` renders an oscillator whose
        Level is 0 so that "turn it down and it still modulates" holds — the behaviour fb523 wrote
        that flag for.  Across banks the carrier and the source are in different voice objects, so
        the source has no way to know anyone is listening, and a modulator turned down would simply
        stop.  A reader therefore REQUESTS its source here during its arm pass, and the owning bank
        forces that lane alive on its next block.
        ⚠️ ONE BLOCK LATE, ON PURPOSE.  Bank 0 arms before bank 1 runs, so a request raised by bank 1
        is seen by bank 0 next block — the first block after you arm a cross source on a SILENT
        oscillator is quiet, then it speaks.  It is a parameter change, which is already block-rate;
        the two-block window below is what keeps it held while the slot stays armed.  */
    void requestSource (int otherBank, int osc) noexcept
    {
        if ((unsigned) otherBank >= (unsigned) kBanks || (unsigned) osc >= (unsigned) kOscs) return;
        want_[otherBank][osc] = block_;
    }
    bool sourceWanted (int bank, int osc) const noexcept
    {
        if ((unsigned) bank >= (unsigned) kBanks || (unsigned) osc >= (unsigned) kOscs) return false;
        return want_[bank][osc] + 2 >= block_;   // held while the asking slot stays armed
    }
    /*  Is anyone in the other bank listening to THIS bank at all?  A voice only claims (and so only
        clears and writes) a row when the answer is yes, which is what makes the whole board free in
        a patch that uses no cross source: no clear, no stores, nothing but this one comparison.  */
    bool anyWanted (int bank) const noexcept
    {
        if ((unsigned) bank >= (unsigned) kBanks) return false;
        for (int o = 0; o < kOscs; ++o) if (want_[bank][o] + 2 >= block_) return true;
        return false;
    }

private:
    struct Row { int note = -1; std::uint64_t stamp = 0; int len = 0; };

    float* lane (int bank, int row, int osc) noexcept
    {
        return store_.data() + (((size_t) bank * kRows + (size_t) row) * kOscs + (size_t) osc) * (size_t) maxBlock_;
    }
    const float* lane (int bank, int row, int osc) const noexcept
    {
        return store_.data() + (((size_t) bank * kRows + (size_t) row) * kOscs + (size_t) osc) * (size_t) maxBlock_;
    }

    /*  A note's row.  `makeIfAbsent` takes the stalest row when the note has none yet — a row whose
        stamp is older than the last block is not sounding and cannot be being read.  */
    int rowFor (int bank, int note, bool makeIfAbsent) const noexcept
    {
        int free = -1; std::uint64_t oldest = ~0ull;
        for (int r = 0; r < kRows; ++r)
        {
            const Row& row = rows_[bank][r];
            if (row.note == note) return r;
            if (! makeIfAbsent) continue;
            if (row.stamp < oldest) { oldest = row.stamp; free = r; }
        }
        if (! makeIfAbsent || free < 0) return -1;
        rows_[bank][free].note  = note;
        rows_[bank][free].stamp = 0;     // forces the clear in claim()
        rows_[bank][free].len   = 0;
        return free;
    }

    mutable Row        rows_[kBanks][kRows] {};
    std::vector<float> store_;
    int                maxBlock_ = 0;
    int                blockLen_ = 0;
    std::uint64_t      want_[kBanks][kOscs] {};   // last block each bank's lane was asked for by the other
    std::uint64_t      block_    = 0;
    bool               live_     = false;
};

} // namespace tw
