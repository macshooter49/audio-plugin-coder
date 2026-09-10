#pragma once
// ═══════════════════════════════════════════════════════════════════════════════════════════════
//  fb619 — PRESET BANKS: the file layer under the preset system. Design/PRESET-SYSTEM-v1.md.
//
//  juce_core ONLY, on purpose: Tests/preset_bank_cert.cpp links this header against the real
//  juce_core module (the preset_path_cert line) and drives every function here on a temp root,
//  with no processor and no host. The processor calls in; the editor's natives are couriers.
//
//  ON DISK
//    <userRoot>/<Bank>/<Name>.terrain        a preset (fb618 wrapper: "TRN1" · manifest · chunk)
//    <userRoot>/<Bank>/bank.json             {name, author, version}  — optional, names the bank
//    <userRoot>/favourites.json              {"keys":["Bank/Name", …]}
//    <userRoot>/vocab.json                   {"types":[…],"styles":[…]}  — the JS owns the schema
//    <factoryRoot>/<Bank>/…                  the bundle's Resources/Banks — read-only
//    a .terrainpack                          zip: pack.json + presets/<Name>.terrain
//
//  LAWS
//    · Every write is refused unless its target is INSIDE the user root (isInside). A zip entry
//      with ".." or an absolute path is dropped, not written.
//    · Names go through safeName — the same body as tiSafePresetName in PluginEditor.cpp (the
//      extractor slices that one for its own cert; keep the two bodies identical).
//    · The catalogue scan reads ~1 KB per file (the manifest) and never the chunk. Its caps
//      REPORT (hitBanks / hitPresets) — the fb606 law: a cap that is silent is a bug.
//    · A meta rewrite updates the manifest AND the <preset> child inside the chunk, and
//      re-emits the chunk with the same XmlElement::TextFormat().singleLine() as
//      juce::AudioProcessor::copyXmlToBinary, so a rewrite is byte-identical outside the child.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_core/juce_core.h>
#include <cstring>

namespace tw { namespace bank {

// ── names ─────────────────────────────────────────────────────────────────────────────────────
inline juce::String safeName (const juce::String& name)
{
    juce::String out;
    const auto trimmed = name.trim().substring (0, 48);
    for (int i = 0; i < trimmed.length(); ++i)
    {
        const auto c = trimmed[i];
        const bool ok = juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '_';
        out << (ok ? juce::String::charToString (c) : juce::String ("_"));
    }
    return out.trim();
}

inline bool isInside (const juce::File& f, const juce::File& root)
{
    return root.exists() && (f == root || f.isAChildOf (root));
}

// ── the chunk: "VC2!" · u32 len · XML · NUL — the mirror of juce::AudioProcessor::copyXmlToBinary ──
inline constexpr juce::uint32 kChunkMagic = 0x21324356u;

inline std::unique_ptr<juce::XmlElement> chunkToXml (const juce::MemoryBlock& chunk)
{
    const auto* p = static_cast<const char*> (chunk.getData()); const size_t n = chunk.getSize();
    if (n < 9 || std::memcmp (p, "VC2!", 4) != 0) return nullptr;
    juce::uint32 len = 0; std::memcpy (&len, p + 4, 4); len = juce::ByteOrder::swapIfBigEndian (len);
    if ((size_t) len + 9 > n) return nullptr;
    return juce::XmlDocument::parse (juce::String::fromUTF8 (p + 8, (int) len));
}

inline void xmlToChunk (const juce::XmlElement& xml, juce::MemoryBlock& out)
{
    out.reset();
    {
        juce::MemoryOutputStream s (out, false);
        s.writeInt ((int) kChunkMagic);
        s.writeInt (0);
        xml.writeTo (s, juce::XmlElement::TextFormat().singleLine());
        s.writeByte (0);
    }
    static_cast<juce::uint32*> (out.getData())[1] = juce::ByteOrder::swapIfBigEndian ((juce::uint32) out.getSize() - 9);
}

// ── the .terrain wrapper ──────────────────────────────────────────────────────────────────────
inline constexpr const char* kMagic = "TRN1";

inline void wrap (juce::MemoryOutputStream& out, const juce::String& manifest, const juce::MemoryBlock& chunk)
{
    const juce::MemoryBlock mj (manifest.toRawUTF8(), manifest.getNumBytesAsUTF8());
    out.write (kMagic, 4);
    out.writeInt ((int) mj.getSize());    out.write (mj.getData(), mj.getSize());
    out.writeInt ((int) chunk.getSize()); out.write (chunk.getData(), chunk.getSize());
    out.flush();
}

inline bool unwrap (const juce::MemoryBlock& file, juce::String& manifestOut, juce::MemoryBlock& chunkOut, juce::String& error)
{
    const auto* p = static_cast<const char*> (file.getData()); const size_t n = file.getSize();
    if (n >= 9 && std::memcmp (p, "VC2!", 4) == 0) { chunkOut = file; manifestOut = {}; return true; }   // a bare chunk
    if (n < 12 || std::memcmp (p, kMagic, 4) != 0) { error = "not a Terrain preset"; return false; }
    juce::MemoryInputStream in (file, false); in.setPosition (4);
    const int ml = in.readInt();
    if (ml < 0 || (size_t) ml > n) { error = "bad manifest length"; return false; }
    { juce::MemoryBlock mb; in.readIntoMemoryBlock (mb, ml); manifestOut = juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()); }
    const int cl = in.readInt();
    if (cl <= 8 || (size_t) cl > n) { error = "bad chunk length"; return false; }
    chunkOut.reset(); in.readIntoMemoryBlock (chunkOut, cl);
    if (chunkOut.getSize() != (size_t) cl) { error = "truncated preset"; return false; }
    return true;
}

inline bool readHeader (const juce::File& f, juce::String& manifestOut, juce::String& error)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) { error = "could not open " + f.getFileName(); return false; }
    char magic[4] = {};
    if (in.read (magic, 4) != 4 || std::memcmp (magic, kMagic, 4) != 0) { error = "not a Terrain preset"; return false; }
    const int ml = in.readInt();
    if (ml < 0 || ml > (1 << 20)) { error = "bad manifest length"; return false; }
    juce::MemoryBlock mb; in.readIntoMemoryBlock (mb, ml);
    manifestOut = juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize());
    return manifestOut.isNotEmpty();
}

// the manifest is the <preset> child's attributes; carries becomes an object, fv an int
inline juce::String manifestFromChild (const juce::XmlElement& pe)
{
    auto* o = new juce::DynamicObject();
    for (int i = 0; i < pe.getNumAttributes(); ++i)
    {
        const auto k = pe.getAttributeName (i), v = pe.getAttributeValue (i);
        if (k == "carries") o->setProperty (k, juce::JSON::parse (v));
        else if (k == "fv") o->setProperty (k, v.getIntValue());
        else o->setProperty (k, v);
    }
    return juce::JSON::toString (juce::var (o), true);
}

// ── sidecar JSON ──────────────────────────────────────────────────────────────────────────────
inline juce::var readJson (const juce::File& f) { return f.existsAsFile() ? juce::JSON::parse (f.loadFileAsString()) : juce::var(); }
inline bool writeJson (const juce::File& f, const juce::var& v)
{
    if (! f.getParentDirectory().exists() && ! f.getParentDirectory().createDirectory()) return false;
    return f.replaceWithText (juce::JSON::toString (v, false));
}

// ── the catalogue ─────────────────────────────────────────────────────────────────────────────
struct Caps { int maxBanks = 200; int maxPresets = 5000; };
struct ScanStats { int banks = 0, presets = 0, unreadable = 0; bool hitBanks = false, hitPresets = false; };

inline juce::var scanRoot (const juce::File& root, bool factory, const Caps& caps, ScanStats& st, juce::Array<juce::var>& banksOut)
{
    if (! root.isDirectory()) return {};
    auto dirs = root.findChildFiles (juce::File::findDirectories, false);
    dirs.sort();
    for (auto& d : dirs)
    {
        if (d.getFileName().startsWithChar ('.')) continue;
        if (st.banks >= caps.maxBanks) { st.hitBanks = true; break; }
        const auto info = readJson (d.getChildFile ("bank.json"));
        auto* b = new juce::DynamicObject();
        b->setProperty ("name",    info.isObject() && info.hasProperty ("name") ? info["name"].toString() : d.getFileName());
        b->setProperty ("author",  info.isObject() ? info.getProperty ("author", "").toString() : juce::String());
        b->setProperty ("version", info.isObject() ? info.getProperty ("version", "").toString() : juce::String());
        b->setProperty ("factory", factory);
        b->setProperty ("dir",     d.getFullPathName());
        juce::Array<juce::var> presets;
        auto files = d.findChildFiles (juce::File::findFiles, false, "*.terrain");
        files.sort();
        for (auto& f : files)
        {
            if (st.presets >= caps.maxPresets) { st.hitPresets = true; break; }
            juce::String manifest, err;
            juce::var m;
            if (readHeader (f, manifest, err)) m = juce::JSON::parse (manifest);
            if (! m.isObject()) { ++st.unreadable; continue; }
            auto* p = m.getDynamicObject();
            if (p->getProperty ("name").toString().isEmpty()) p->setProperty ("name", f.getFileNameWithoutExtension());
            p->setProperty ("bank", b->getProperty ("name"));          // the folder wins: a moved file is in the bank it sits in
            if (b->getProperty ("author").toString().isEmpty() && p->getProperty ("author").toString().isNotEmpty())
                b->setProperty ("author", p->getProperty ("author"));
            p->setProperty ("path",  f.getFullPathName());
            p->setProperty ("bytes", (double) f.getSize());   // fb621 — the inspector's Size row
            p->setProperty ("mtime", (juce::int64) f.getLastModificationTime().toMilliseconds());
            p->setProperty ("factory", factory);
            presets.add (m); ++st.presets;
        }
        b->setProperty ("presets", juce::var (presets));
        banksOut.add (juce::var (b)); ++st.banks;
        if (st.hitPresets) break;
    }
    return {};
}

inline juce::var scan (const juce::File& factoryRoot, const juce::File& userRoot, const Caps& caps, ScanStats& st)
{
    juce::Array<juce::var> banks;
    scanRoot (factoryRoot, true,  caps, st, banks);
    scanRoot (userRoot,    false, caps, st, banks);
    auto* o = new juce::DynamicObject();
    o->setProperty ("banks", juce::var (banks));
    auto* c = new juce::DynamicObject();
    c->setProperty ("banks", st.banks); c->setProperty ("presets", st.presets); c->setProperty ("unreadable", st.unreadable);
    c->setProperty ("hitBanks", st.hitBanks); c->setProperty ("hitPresets", st.hitPresets);
    c->setProperty ("maxBanks", caps.maxBanks); c->setProperty ("maxPresets", caps.maxPresets);
    o->setProperty ("caps", juce::var (c));
    o->setProperty ("userRoot", userRoot.getFullPathName());
    o->setProperty ("factoryRoot", factoryRoot.getFullPathName());
    return juce::var (o);
}

// ── writes — user root only ───────────────────────────────────────────────────────────────────
inline juce::File bankDir (const juce::File& userRoot, const juce::String& bank) { return userRoot.getChildFile (safeName (bank)); }
inline juce::File presetPath (const juce::File& userRoot, const juce::String& bank, const juce::String& name)
{ return bankDir (userRoot, bank).getChildFile (safeName (name) + ".terrain"); }

inline bool ensureBank (const juce::File& userRoot, const juce::String& bank, const juce::String& author, juce::File& dirOut, juce::String& err)
{
    const auto nm = safeName (bank);
    if (nm.isEmpty()) { err = "a bank needs a name"; return false; }
    dirOut = userRoot.getChildFile (nm);
    if (! isInside (dirOut, userRoot) && ! (userRoot.createDirectory() && isInside (dirOut, userRoot))) { err = "outside the user root"; return false; }
    if (! dirOut.isDirectory() && ! dirOut.createDirectory()) { err = "could not create " + dirOut.getFullPathName(); return false; }
    const auto info = dirOut.getChildFile ("bank.json");
    if (! info.existsAsFile())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", bank.trim()); o->setProperty ("author", author.trim()); o->setProperty ("version", "1");
        if (! writeJson (info, juce::var (o))) { err = "could not write bank.json"; return false; }
    }
    return true;
}

inline bool writePreset (const juce::File& dst, const juce::File& userRoot, const juce::String& manifest, const juce::MemoryBlock& chunk, juce::String& err)
{
    if (! dst.getParentDirectory().exists() && ! dst.getParentDirectory().createDirectory()) { err = "could not create " + dst.getParentDirectory().getFullPathName(); return false; }
    if (! isInside (dst, userRoot)) { err = "outside the user root"; return false; }
    juce::MemoryOutputStream out; wrap (out, manifest, chunk);
    if (! dst.replaceWithData (out.getData(), out.getDataSize())) { err = "could not write " + dst.getFullPathName(); return false; }
    return true;
}

// patch = {name?, bank?, author?, type?, styles?, note?} — every present key lands in BOTH the manifest and the child
inline bool rewriteMeta (const juce::File& f, const juce::File& userRoot, const juce::var& patch, juce::String& err)
{
    if (! isInside (f, userRoot)) { err = "outside the user root"; return false; }
    juce::MemoryBlock file; if (! f.loadFileAsData (file)) { err = "could not read " + f.getFileName(); return false; }
    juce::String manifest; juce::MemoryBlock chunk;
    if (! unwrap (file, manifest, chunk, err)) return false;
    auto xml = chunkToXml (chunk); if (xml == nullptr) { err = "bad chunk"; return false; }
    auto* pe = xml->getChildByName ("preset");
    if (pe == nullptr) { pe = new juce::XmlElement ("preset"); xml->insertChildElement (pe, 0); pe->setAttribute ("fv", 1); }
    if (auto* po = patch.getDynamicObject())
        for (auto& kv : po->getProperties())
        {
            const auto k = kv.name.toString();
            if (k == "name" || k == "bank" || k == "author" || k == "type" || k == "styles" || k == "note")
                pe->setAttribute (k, kv.value.toString());
        }
    juce::MemoryBlock chunk2; xmlToChunk (*xml, chunk2);
    juce::MemoryOutputStream out; wrap (out, manifestFromChild (*pe), chunk2);
    if (! f.replaceWithData (out.getData(), out.getDataSize())) { err = "could not write " + f.getFullPathName(); return false; }
    return true;
}

inline bool movePreset (const juce::File& f, const juce::File& userRoot, const juce::String& toBank, juce::File& out, juce::String& err)
{
    if (! isInside (f, userRoot)) { err = "outside the user root"; return false; }
    juce::File dir; if (! ensureBank (userRoot, toBank, {}, dir, err)) return false;
    out = dir.getChildFile (f.getFileName());
    if (out == f) return true;
    if (out.existsAsFile()) { err = "a preset with that name is already in " + toBank; return false; }
    if (! f.moveFileTo (out)) { err = "could not move " + f.getFileName(); return false; }
    auto* p = new juce::DynamicObject(); p->setProperty ("bank", toBank.trim());
    return rewriteMeta (out, userRoot, juce::var (p), err);
}

inline bool removePreset (const juce::File& f, const juce::File& userRoot, juce::String& err)
{
    if (! isInside (f, userRoot) || f.getFileExtension() != ".terrain") { err = "outside the user root"; return false; }
    if (! f.deleteFile()) { err = "could not delete " + f.getFileName(); return false; }
    return true;
}

inline bool renameBank (const juce::File& userRoot, const juce::String& oldName, const juce::String& newName, juce::String& err)
{
    const auto from = bankDir (userRoot, oldName), to = bankDir (userRoot, newName);
    if (! isInside (from, userRoot) || ! from.isDirectory()) { err = "no bank " + oldName; return false; }
    if (safeName (newName).isEmpty()) { err = "a bank needs a name"; return false; }
    if (to != from && to.exists()) { err = newName + " already exists"; return false; }
    if (to != from && ! from.moveFileTo (to)) { err = "could not rename " + oldName; return false; }
    auto info = readJson (to.getChildFile ("bank.json"));
    if (! info.isObject()) info = juce::var (new juce::DynamicObject());
    info.getDynamicObject()->setProperty ("name", newName.trim());
    writeJson (to.getChildFile ("bank.json"), info);
    auto* p = new juce::DynamicObject(); p->setProperty ("bank", newName.trim());
    for (auto& f : to.findChildFiles (juce::File::findFiles, false, "*.terrain"))
        if (! rewriteMeta (f, userRoot, juce::var (p), err)) return false;
    return true;
}

inline bool removeBank (const juce::File& userRoot, const juce::String& bank, juce::String& err)
{
    const auto d = bankDir (userRoot, bank);
    if (! isInside (d, userRoot) || d == userRoot || ! d.isDirectory()) { err = "no bank " + bank; return false; }
    if (! d.deleteRecursively()) { err = "could not remove " + bank; return false; }
    return true;
}

// ── favourites and vocabulary — sidecars the JS owns ──────────────────────────────────────────
inline juce::File favouritesFile (const juce::File& userRoot) { return userRoot.getChildFile ("favourites.json"); }
inline juce::File vocabFile      (const juce::File& userRoot) { return userRoot.getChildFile ("vocab.json"); }
inline juce::String favKey (const juce::String& bank, const juce::String& name) { return bank.trim() + "/" + name.trim(); }
inline bool setFavourite (const juce::File& userRoot, const juce::String& bank, const juce::String& name, bool on)
{
    auto v = readJson (favouritesFile (userRoot));
    juce::Array<juce::var> keys;
    if (auto* a = v.isObject() ? v.getProperty ("keys", juce::var()).getArray() : nullptr) keys = *a;
    const auto k = favKey (bank, name);
    for (int i = keys.size(); --i >= 0;) if (keys[i].toString() == k) keys.remove (i);
    if (on) keys.add (k);
    auto* o = new juce::DynamicObject(); o->setProperty ("keys", juce::var (keys));
    return writeJson (favouritesFile (userRoot), juce::var (o));
}

// ── packs ─────────────────────────────────────────────────────────────────────────────────────
inline bool exportPack (const juce::File& dir, const juce::var& bankInfo, const juce::File& dstZip, juce::String& err)
{
    if (! dir.isDirectory()) { err = "no bank folder"; return false; }
    juce::ZipFile::Builder b;
    const auto packJson = juce::JSON::toString (bankInfo, false);
    const auto tmp = juce::File::createTempFile ("json");
    if (! tmp.replaceWithText (packJson)) { err = "could not stage pack.json"; return false; }
    b.addFile (tmp, 6, "pack.json");
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.terrain"); files.sort();
    for (auto& f : files) b.addFile (f, 6, "presets/" + f.getFileName());
    juce::FileOutputStream out (dstZip);
    if (! out.openedOk()) { err = "could not write " + dstZip.getFullPathName(); tmp.deleteFile(); return false; }
    out.setPosition (0); out.truncate();
    const bool ok = b.writeToStream (out, nullptr);
    out.flush(); tmp.deleteFile();
    if (! ok) { err = "could not write the pack"; return false; }
    return true;
}

// ═══ fb621 — LOOK INSIDE A PACK WITHOUT INSTALLING IT ══════════════════════════════════════════
//  Max's law from the mockup: the import sheet shows you WHAT IS IN THE BOX before you say yes —
//  how many presets, what they carry, how big. Cheap because of the file layout: a .terrain leads
//  with its manifest, so this reads a few hundred bytes per entry, not the whole preset.
inline bool inspectPack (const juce::File& zipFile, juce::var& out, juce::String& err)
{
    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() <= 0) { err = "not a readable .terrainpack"; return false; }
    auto* o = new juce::DynamicObject();
    o->setProperty ("name",   zipFile.getFileNameWithoutExtension());
    o->setProperty ("author", juce::String());
    juce::int64 bytes = 0; int count = 0, envs = 0;
    int wt = 0, smp = 0, ir = 0, flow = 0, lfo = 0, nodes = 0;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        if (e == nullptr) continue;
        const auto nm = e->filename;
        if (nm.endsWithIgnoreCase ("pack.json"))
        {
            std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
            if (s != nullptr)
                if (auto v = juce::JSON::parse (s->readEntireStreamAsString()); v.isObject())
                {
                    if (v.getProperty ("name", "").toString().isNotEmpty())   o->setProperty ("name",   v.getProperty ("name", ""));
                    o->setProperty ("author",  v.getProperty ("author", ""));
                    o->setProperty ("version", v.getProperty ("version", ""));
                }
            continue;
        }
        if (! nm.endsWithIgnoreCase (".terrain")) continue;
        ++count; bytes += e->uncompressedSize;
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
        if (s == nullptr) continue;
        // the header only: "TRN1" · i32 manifest length · manifest
        char magic[4] = {}; if (s->read (magic, 4) != 4 || std::memcmp (magic, kMagic, 4) != 0) continue;
        const int mlen = s->readInt();
        if (mlen <= 0 || mlen > 1 << 20) continue;
        juce::MemoryBlock mb ((size_t) mlen);
        if (s->read (mb.getData(), mlen) != mlen) continue;
        const auto man = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (mb.getData()), mlen));
        if (! man.isObject()) continue;
        const auto car = man.getProperty ("carries", juce::var());
        auto n = [&car] (const char* k) { return (int) car.getProperty (k, 0); };
        wt += n ("wt"); smp += n ("smp"); ir += n ("ir"); flow += n ("flow"); lfo += n ("lfo");
        const int nd = n ("nodes"); nodes += nd; if (nd > 0) ++envs;
    }
    if (count == 0) { err = "the pack holds no presets"; return false; }
    auto* c = new juce::DynamicObject();
    c->setProperty ("wt", wt); c->setProperty ("smp", smp); c->setProperty ("ir", ir);
    c->setProperty ("flow", flow); c->setProperty ("lfo", lfo); c->setProperty ("nodes", nodes);
    o->setProperty ("carries", juce::var (c));
    o->setProperty ("presets", count);
    o->setProperty ("environments", envs);
    o->setProperty ("bytes", (double) bytes);
    o->setProperty ("path", zipFile.getFullPathName());
    out = juce::var (o);
    return true;
}

inline bool importPack (const juce::File& zipFile, const juce::File& userRoot, juce::File& bankDirOut, juce::String& err)
{
    juce::ZipFile z (zipFile);
    if (z.getNumEntries() == 0) { err = "not a Terrain pack"; return false; }
    juce::var info;
    if (auto* e = z.getEntry ("pack.json"))
        if (auto in = std::unique_ptr<juce::InputStream> (z.createStreamForEntry (*e))) info = juce::JSON::parse (in->readEntireStreamAsString());
    juce::String bank = info.isObject() ? info.getProperty ("name", "").toString() : juce::String();
    if (bank.isEmpty()) bank = zipFile.getFileNameWithoutExtension();
    const auto author = info.isObject() ? info.getProperty ("author", "").toString() : juce::String();
    juce::File dir;
    if (bankDir (userRoot, bank).exists()) { err = bank + " is already installed"; return false; }
    if (! ensureBank (userRoot, bank, author, dir, err)) return false;
    int n = 0;
    for (int i = 0; i < z.getNumEntries(); ++i)
    {
        const auto* e = z.getEntry (i);
        const auto name = e->filename;
        if (! name.startsWith ("presets/") || ! name.endsWith (".terrain")) continue;
        const auto leaf = name.fromLastOccurrenceOf ("/", false, false);
        if (leaf.isEmpty() || name.contains ("..") || leaf.contains ("/") || leaf.contains ("\\")) continue;   // zip-slip
        const auto dst = dir.getChildFile (leaf);
        if (! isInside (dst, userRoot)) continue;
        if (auto in = std::unique_ptr<juce::InputStream> (z.createStreamForEntry (i)))
        {
            juce::MemoryBlock mb; in->readIntoMemoryBlock (mb);
            juce::String manifest, e2; juce::MemoryBlock chunk;
            if (! unwrap (mb, manifest, chunk, e2)) continue;                 // not a preset: skip
            if (dst.replaceWithData (mb.getData(), mb.getSize())) ++n;
        }
    }
    if (n == 0) { dir.deleteRecursively(); err = "the pack carries no presets"; return false; }
    bankDirOut = dir;
    return true;
}

}} // namespace tw::bank
