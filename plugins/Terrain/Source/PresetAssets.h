#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  PresetAssets.h — fb621: EVERYTHING A USER MAKES TRAVELS WITH THE PRESET.
//
//  Max's law (Design/PRESET-SYSTEM-v1.md §3): a preset carries its own audio — one-shots, impulse
//  responses, imported wavetables — embedded as FLAC, so a bank handed to somebody else SOUNDS THE
//  SAME on their machine with no missing-file dialog, ever. Before this, one-shots were ABSOLUTE
//  PATHS (a bank sent to a friend pointed at a folder that does not exist on their disk) and
//  wavetable imports were raw float32 base64 (5.33 bytes per sample of chunk, in every save).
//
//  THE ENVELOPE. One APVTS property per slot holds ONE base64 string, and that string is
//  SELF-DESCRIBING: a slot never needs a second property to be understood, so the absent-means-
//  clear law (fb618) stays one-property-one-meaning and a half-written slot is impossible.
//
//      'T' 'A' '1'   magic
//      u8  kind      0 = FLAC, 1 = raw float32 (the fallback when FLAC refuses the shape)
//      u8  channels
//      u8  bits      24 for FLAC; 32 for the raw fallback
//      u8  flags     reserved, 0
//      u32 sampleRate
//      u32 frames    wavetable frames per table; 0 for anything that is only audio
//      f32 scale     multiply the decoded audio by this
//      u32 nameLen + name (UTF-8)
//      u32 payloadLen + payload
//
//  🚨 WHY `scale` EXISTS. FLAC is an INTEGER format: encoding asks for samples in [-1, 1]. A float
//  WAV can peak above 1.0 (Max's own one-shots do — a normalised-to-0dBFS render bounced through a
//  saturator lands at 1.4 easily), and quantising that to 24-bit would CLIP it — silently, at save
//  time, in a way nobody would notice until the preset came back thinner. So the encoder measures
//  the true peak, divides by it, and records the divisor. 24 bits over a scaled signal is 144 dB of
//  range: the round-trip is inaudible, and it is EXACT for anything already inside [-1, 1].
//
//  🚨 WHY NOT juce::MD5. It lives in juce_cryptography, and this header is included by certs that
//  link juce_core + juce_audio_formats only. FNV-1a 64 is enough to answer the only question asked
//  of it — "is the factory file on THIS machine the same one the preset was made against?"
//
//  BASE64 DIALECT: juce::MemoryBlock::toBase64Encoding, the same one every existing blob in the
//  chunk uses (a decimal length, a '.', then JUCE's own alphabet). Never RFC 4648 here — mixing the
//  two is how a blob silently decodes to noise.
// ══════════════════════════════════════════════════════════════════════════════════════════════

#include <juce_audio_formats/juce_audio_formats.h>

namespace tw::asset
{

inline constexpr int kMagic0 = 'T', kMagic1 = 'A', kMagic2 = '1';
inline constexpr int kKindFlac = 0, kKindRawFloat = 1;

struct Envelope
{
    juce::AudioBuffer<float> audio;
    double       sampleRate = 0.0;
    int          frames     = 0;      // wavetable frames; 0 = plain audio
    juce::String name;
    int          kind       = kKindFlac;
    size_t       bytes      = 0;      // the encoded size, for the browser's Size column
};

// ── little helpers: everything little-endian, matching the chunk's own u32 convention ──────────
inline void putU32 (juce::MemoryOutputStream& o, juce::uint32 v) { o.writeInt ((int) v); }
inline void putF32 (juce::MemoryOutputStream& o, float v)        { o.writeFloat (v); }

inline juce::String hashOf (const void* data, size_t bytes)
{
    juce::uint64 h = 1469598103934665603ull;                       // FNV-1a 64
    const auto* p = static_cast<const juce::uint8*> (data);
    for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

// ── FACTORY REFERENCES ─────────────────────────────────────────────────────────────────────────
//  Content that ships INSIDE the plugin bundle (the 120-table Terra wavetable library, the factory
//  noise library) must never be embedded: it is already on every machine that can open the preset,
//  and embedding it would put ~1.4 MB of the SAME audio in every preset that used it. A reference
//  names the file relative to its factory root and carries a hash, so a library that changed under
//  a preset's feet can SAY SO instead of quietly sounding different.
inline juce::String makeRef (const juce::String& relPath, const juce::String& hash)
{
    return "ref:1|" + relPath.replaceCharacter ('|', '/') + "|" + hash;
}
inline bool isRef (const juce::String& s) { return s.startsWith ("ref:1|"); }
inline bool parseRef (const juce::String& s, juce::String& relOut, juce::String& hashOut)
{
    if (! isRef (s)) return false;
    const auto body = s.substring (6);
    const int bar = body.lastIndexOfChar ('|');
    if (bar <= 0) return false;
    relOut = body.substring (0, bar); hashOut = body.substring (bar + 1);
    return relOut.isNotEmpty();
}

// ── ENCODE ─────────────────────────────────────────────────────────────────────────────────────
//  Called ONCE, when the asset arrives (an import, a drop, a decode) — never on the save path.
//  getStateInformation runs on every host project save; it must copy a string, not re-encode a
//  buffer. The processor caches what this returns beside the audio it describes.
inline juce::String encode (const juce::AudioBuffer<float>& audio, double sampleRate,
                            int frames, const juce::String& name)
{
    const int nch = juce::jmax (1, audio.getNumChannels());
    const int n   = audio.getNumSamples();
    if (n <= 0) return {};

    float peak = 0.0f;
    for (int c = 0; c < nch; ++c)
    {
        const auto r = audio.findMinMax (c, 0, n);
        peak = juce::jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));
    }
    if (! std::isfinite (peak) || peak <= 0.0f) peak = 1.0f;
    const float scale = juce::jmax (1.0f, peak);                   // only ever divides DOWN

    juce::MemoryBlock payload;
    int kind = kKindFlac, bits = 24;
    {
        juce::AudioBuffer<float> scaled;
        const juce::AudioBuffer<float>* src = &audio;
        if (scale > 1.0f)
        {
            scaled.makeCopyOf (audio);
            scaled.applyGain (1.0f / scale);
            src = &scaled;
        }
        // FLAC wants a sane integer rate; a wavetable has no real one, so it gets a placeholder and
        // the frame count carries the meaning instead.
        const double sr = (sampleRate >= 1.0 && sampleRate <= 384000.0) ? sampleRate : 48000.0;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (payload, false);
        juce::FlacAudioFormat fmt;
        auto writer = fmt.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (sr)
                                                       .withNumChannels (nch)
                                                       .withBitsPerSample (24));
        if (writer != nullptr && writer->writeFromAudioSampleBuffer (*src, 0, n))
            writer.reset();                                        // flush before the block is read
        else
        {
            writer.reset(); payload.reset();
            kind = kKindRawFloat; bits = 32;                        // FLAC refused: ship the floats
            juce::MemoryOutputStream raw (payload, false);
            for (int c = 0; c < nch; ++c) raw.write (audio.getReadPointer (c), (size_t) n * sizeof (float));
            raw.flush();
        }
    }
    if (payload.getSize() == 0) return {};

    juce::MemoryBlock out;
    {
        juce::MemoryOutputStream o (out, false);
        o.writeByte ((char) kMagic0); o.writeByte ((char) kMagic1); o.writeByte ((char) kMagic2);
        o.writeByte ((char) kind); o.writeByte ((char) nch); o.writeByte ((char) bits); o.writeByte (0);
        o.writeByte (0);                                            // pad to 8
        putU32 (o, (juce::uint32) juce::jlimit (0.0, 4.0e9, sampleRate));
        putU32 (o, (juce::uint32) juce::jmax (0, frames));
        putF32 (o, scale);
        const auto nameUtf8 = name.toRawUTF8();
        const auto nameLen  = (juce::uint32) juce::CharPointer_UTF8 (nameUtf8).sizeInBytes() - 1;
        putU32 (o, nameLen); if (nameLen > 0) o.write (nameUtf8, nameLen);
        putU32 (o, (juce::uint32) payload.getSize());
        o.write (payload.getData(), payload.getSize());
        o.flush();
    }
    return out.toBase64Encoding();
}

inline juce::String encodeMono (const float* data, int numSamples, double sampleRate,
                                int frames, const juce::String& name)
{
    if (data == nullptr || numSamples <= 0) return {};
    juce::AudioBuffer<float> b (1, numSamples);
    std::memcpy (b.getWritePointer (0), data, (size_t) numSamples * sizeof (float));
    return encode (b, sampleRate, frames, name);
}

// ── DECODE ─────────────────────────────────────────────────────────────────────────────────────
inline bool decode (const juce::String& b64, Envelope& out, juce::String& error)
{
    juce::MemoryBlock mb;
    if (b64.isEmpty() || ! mb.fromBase64Encoding (b64)) { error = "not base64"; return false; }
    if (mb.getSize() < 28) { error = "too short"; return false; }
    const auto* p = static_cast<const juce::uint8*> (mb.getData());
    if (p[0] != kMagic0 || p[1] != kMagic1 || p[2] != kMagic2) { error = "not a TA1 asset"; return false; }

    juce::MemoryInputStream in (mb, false);
    in.skipNextBytes (3);
    const int kind = in.readByte(), nch = in.readByte(), bits = in.readByte();
    in.readByte(); in.readByte();                                   // flags, pad
    const auto sr    = (juce::uint32) in.readInt();
    const auto frms  = (juce::uint32) in.readInt();
    const float scale = in.readFloat();
    const auto nameLen = (juce::uint32) in.readInt();
    if (nameLen > 4096) { error = "bad name length"; return false; }
    juce::MemoryBlock nameBytes ((size_t) nameLen + 1, true);
    if (nameLen > 0) in.read (nameBytes.getData(), (int) nameLen);
    const auto payLen = (juce::uint32) in.readInt();
    if (payLen == 0 || (juce::int64) payLen > in.getNumBytesRemaining()) { error = "bad payload length"; return false; }
    juce::MemoryBlock payload ((size_t) payLen);
    in.read (payload.getData(), (int) payLen);

    out.sampleRate = (double) sr;
    out.frames     = (int) frms;
    out.name       = juce::String::fromUTF8 (static_cast<const char*> (nameBytes.getData()));
    out.kind       = kind;
    out.bytes      = mb.getSize();

    if (kind == kKindFlac)
    {
        juce::FlacAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatReader> reader (
            fmt.createReaderFor (new juce::MemoryInputStream (payload, false), true));
        if (reader == nullptr) { error = "FLAC would not open"; return false; }
        const int n = (int) juce::jmin ((juce::int64) std::numeric_limits<int>::max(), reader->lengthInSamples);
        if (n <= 0) { error = "empty FLAC"; return false; }
        out.audio.setSize ((int) juce::jmax (1u, reader->numChannels), n);
        reader->read (&out.audio, 0, n, 0, true, reader->numChannels > 1);
        if (out.sampleRate <= 0.0) out.sampleRate = reader->sampleRate;
    }
    else if (kind == kKindRawFloat)
    {
        const int total = (int) (payload.getSize() / sizeof (float));
        const int n = total / juce::jmax (1, nch);
        if (n <= 0) { error = "empty float payload"; return false; }
        out.audio.setSize (juce::jmax (1, nch), n);
        const auto* f = static_cast<const float*> (payload.getData());
        for (int c = 0; c < out.audio.getNumChannels(); ++c)
            std::memcpy (out.audio.getWritePointer (c), f + (size_t) c * (size_t) n, (size_t) n * sizeof (float));
    }
    else { error = "unknown asset kind " + juce::String (kind); return false; }

    if (bits == 24 && scale > 1.0f && std::isfinite (scale))
        out.audio.applyGain (scale);
    else if (kind == kKindFlac && scale > 1.0f && std::isfinite (scale))
        out.audio.applyGain (scale);
    return true;
}

// a cheap "is this ours?" that does not decode the audio — the header alone is enough
inline bool isEnvelope (const juce::String& b64)
{
    if (b64.isEmpty() || b64.length() < 8) return false;
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (b64) || mb.getSize() < 28) return false;
    const auto* p = static_cast<const juce::uint8*> (mb.getData());
    return p[0] == kMagic0 && p[1] == kMagic1 && p[2] == kMagic2;
}

// the encoded size without decoding — what the browser prices a preset's carries with
inline size_t sizeOf (const juce::String& b64) { return (size_t) juce::jmax (0, (b64.length() * 3) / 4); }

}   // namespace tw::asset
