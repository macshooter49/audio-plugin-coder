# Noise Factory Library — bulk-acquire 200+ CC0 textures from Freesound

**Goal:** bundle **200+ real recorded noise/texture/foley clips** as *factory content* for the
Terrain Instrument "Noise" engine — Ocean, air cans, foley, footsteps, door cracking, crackling,
walking noises, and more. They play as **fixed looping textures** through the sample-player-as-noise
engine already being built (see `NOISE-ENGINE-PLAN.md`).

**Hard constraint: strictly CC0 only** (license == `"Creative Commons 0"`). This is a **paid**
plugin. No CC-BY, no CC-Sampling+, no NonCommercial, no attribution-required content — ever. CC0 is a
public-domain dedication that *explicitly* permits commercial use and redistribution with **no
attribution required**, which is exactly why it's the only license we touch. We still keep a full
provenance manifest (source URL, Freesound id, uploader, license) for our own records.

Script: **`scripts/freesound-fetch.py`** (repo root `scripts/`, stdlib-only, ready to run).
Status: RESEARCH + pipeline. **Do not download yet** — no API credentials provisioned.

Verified against the live Freesound docs on **2026-07-16** (see §D for the exact pages).

---

## A. What Max must do RIGHT NOW (step by step)

1. **Create a free Freesound account** — https://freesound.org (top-right "Join").
2. **Apply for API credentials** — https://freesound.org/apiv2/apply
   - Fill in an app name (e.g. "Terrain Instrument noise research"), description, and a URL. For the
     "Callback / redirect URI" put something you control; a placeholder like
     `https://wavescrate.com/freesound-callback` or even `http://localhost/callback` is fine — you
     only need it for the OAuth2 originals flow (§ below), and you just read the `code` out of the
     redirected URL by hand.
   - Approval is typically **instant/automatic**.
   - You receive **three** things: a **Client id**, a **Client secret/API key** (the token), and the
     ability to run the OAuth2 flow. The **token/API key** is what the script calls
     `FREESOUND_API_TOKEN`.
3. **Export the token and do a DRY RUN first (downloads nothing):**
   ```bash
   export FREESOUND_API_TOKEN='<your api key>'
   python3 scripts/freesound-fetch.py --dry-run
   ```
   This searches every category and prints how many CC0 hits exist (target ≥200) without touching a
   single audio file. Confirms the key works and the plan clears 200 before we pull anything.
4. **Decide preview vs original** (see §A.2). For a paid product we recommend **originals via OAuth2**
   (do the one-time `oauth` step), falling back to **HQ previews** if you want zero friction.
5. **⚠️ Email `mtg@upf.edu` before shipping** — the Freesound *API Terms* say commercial API use is
   "negotiated on a case-by-case basis with UPF." This does **not** affect the CC0 sounds' own rights
   (those are public domain and yours to bundle), but the *act of using the API* for a commercial
   product should be disclosed/cleared. One short email. See §D for the exact clause and why this is a
   low risk, not a blocker. (Manual download from the website is always an alternative — CC0 rights
   attach to the sound regardless of how you obtain it.)

**Quota check:** the standard API allows **2000 requests/day** — pulling ~250 sounds + ~40 search
requests ≈ **~290 requests**, comfortably inside one day's quota (§D).

### A.1 — Token auth vs OAuth2 (the critical distinction)

| | **Token / API key** | **OAuth2 access token** |
|---|---|---|
| How you get it | Instantly at `/apiv2/apply` | Interactive "log in & authorize" flow |
| HTTP header | `Authorization: Token <key>` (or `?token=<key>`) | `Authorization: Bearer <access_token>` |
| **Search** (`/search/text/`) | ✅ yes | ✅ yes |
| **HQ/LQ preview** download (mp3/ogg) | ✅ yes | ✅ yes |
| **ORIGINAL** file download (`/sounds/<id>/download/`) | ❌ **no** — marked *OAuth2 required* | ✅ **yes** |
| Lifetime | permanent | access token ~24h + refresh token |

**Bottom line:** previews need only the token; **downloading the original source file requires OAuth2.**

### A.2 — Preview vs Original: which for a factory library?

- **Preview** = `preview-hq-ogg` (~192 kbps Vorbis) or `preview-hq-mp3` (~128 kbps). Lossy. Fine for
  quick evaluation; *acceptable but not ideal* for broadband noise (lossy codecs can smear hiss and
  add warble/pre-echo on crackle & transients). **Token only — dead simple.**
- **Original** = whatever the uploader posted (often WAV/FLAC/AIFF, sometimes mp3). **Best fidelity**,
  which matters for a paid product. Requires OAuth2 and formats are heterogeneous (we normalise them
  in post anyway — §E).

**Recommendation:** use **originals via OAuth2** for the shipped library (quality bar for a paid
plugin), and keep **HQ-preview mode** as the friction-free fallback / for the dry-run vetting pass.
The script does both via `--mode original|preview`. The OAuth2 flow is a *one-time* ~2-minute step,
so yes — it's worth it.

### A.3 — The OAuth2 flow (only needed for `--mode original`)

The script automates the exchange; you just click "Authorize" once.
```bash
export FREESOUND_CLIENT_ID='<client id>'
export FREESOUND_CLIENT_SECRET='<client secret>'
python3 scripts/freesound-fetch.py oauth      # prints an authorize URL
#  -> open URL, log in, click Authorize
#  -> browser redirects to your callback with ?code=XXXX  (code expires in 10 min)
#  -> paste XXXX at the prompt; script exchanges it for tokens
export FREESOUND_OAUTH_TOKEN='<printed access token>'   # valid ~24h
python3 scripts/freesound-fetch.py --mode original --out ./noise-factory
```
Flow under the hood (verified §D):
1. `GET /apiv2/oauth2/authorize/?client_id=…&response_type=code` → user authorizes.
2. Freesound redirects to your callback with `?code=…` (**code valid 10 min**).
3. `POST /apiv2/oauth2/access_token/` with `client_id, client_secret, grant_type=authorization_code,
   code` → returns **access_token (24h)** + **refresh_token** (renew without re-authorizing via
   `grant_type=refresh_token`).

### A.4 — Rate limits / quotas (verified)

- **Standard resources (search + download): 60 requests/minute, 2000 requests/day.**
- Stricter tier (upload/describe/comment/rate/bookmark — we don't use these): 30/min, 500/day.
- Over the limit → **HTTP 429 Too many requests** with a `detail` field.
- The script self-throttles to **~50 req/min** (1.2 s spacing) and backs off on 429 using
  `Retry-After`. Need more than 2000/day? Email `mtg@upf.edu`. We won't — one run is ~290 requests.

---

## B. The download script (`scripts/freesound-fetch.py`)

Stdlib-only (no `pip install`), runs on stock macOS `python3`. Verified `py_compile`-clean.

**What it does (maps 1:1 to the requirements):**
- **Secrets from env only** — `FREESOUND_API_TOKEN`, `FREESOUND_OAUTH_TOKEN`,
  `FREESOUND_CLIENT_ID/SECRET`. Nothing hard-coded.
- For each category, `GET /apiv2/search/text/` with
  `filter=license:"Creative Commons 0" duration:[1.0 TO 30.0]`, `page_size=150`,
  `fields=id,name,license,previews,download,type,duration,username,url,tags`, `sort=downloads_desc`
  (popular-first ≈ cleaner recordings). **Paginates** via the response `next` cursor until the
  per-category target is met.
- **Defence-in-depth CC0 check:** every result must have `license == "Creative Commons 0"` *exactly*
  (and pass the duration window) before it's kept — the server filter is never trusted alone.
- **Downloads** the **original** (OAuth2 `Bearer`, `--mode original`) or the **HQ preview**
  (`preview-hq-ogg` → mp3 fallback, token, `--mode preview`).
- **Names** files `<Category>_<slugified-name>_<freesoundId>.<ext>` inside a per-category subfolder,
  and writes **`manifest.json`** keyed by id → `{id, name, category, license, username, url,
  duration, type, tags, download_mode, file, source}`.
- **Throttle + retry:** ≥1.2 s between requests, exponential backoff on 5xx/network, 429 honours
  `Retry-After`.
- **Idempotent / resumable:** already-in-manifest ids and already-on-disk files are skipped; the
  manifest is saved after every file, so you can Ctrl-C and re-run.
- `--dry-run` searches and counts only (downloads nothing) — the safe first pass.
- `--only Ocean,Rain` restricts to a category subset; `--out DIR` sets the destination.

**Quick reference:**
```bash
python3 scripts/freesound-fetch.py --dry-run                       # count CC0 hits, no download
python3 scripts/freesound-fetch.py --mode preview  --out ./noise-factory
python3 scripts/freesound-fetch.py oauth                           # one-time, get OAuth token
python3 scripts/freesound-fetch.py --mode original --out ./noise-factory
python3 scripts/freesound-fetch.py --only Ocean,Footsteps --mode original --out ./noise-factory
```

---

## C. Category → search-query plan (sums to ~252 → clears 200+)

18 categories. Each becomes a **subfolder = a cascading sub-menu entry** in the Noise type menu.
Targets are padded above 200 so we still land ≥200 after the strict CC0 re-check, duration filter,
de-dupe, and manual quality-cull. All extend the menu as **new "Textures/Recorded" subcategories**;
the existing **Colors / Tape / Vinyl / Space** groups stay **synthesized** (algorithmic) and are
untouched.

| # | Category (subfolder) | Search terms | Target | Menu placement |
|---|---|---|---|---|
| 1 | **Ocean** | ocean waves · sea shore · waves beach · surf | 20 | new · Water/Nature group |
| 2 | **Water** | stream water · river flowing · waterfall · creek | 12 | new · Water/Nature group |
| 3 | **Rain** | rain · rain on roof · rainstorm · drizzle | 16 | new · Weather group |
| 4 | **Wind** | wind · wind howling · storm wind · wind trees | 16 | extends **Space→Wind** (adds real recordings) |
| 5 | **Fire** | fire crackling · campfire · bonfire · flames | 12 | new · Elements group |
| 6 | **Nature** | forest ambience · crickets night · birds ambience · jungle | 16 | new · Nature group |
| 7 | **Footsteps** | footsteps gravel · footsteps wood · footsteps snow · walking concrete · boots | 18 | new · Foley group |
| 8 | **Foley** | cloth movement · paper rustle · rummage · object handling | 16 | new · Foley group |
| 9 | **Doors** | door creak · door open close · door slam · hinge creak | 14 | new · Foley group |
| 10 | **Cracking** | wood creak · ice cracking · branch snap · structure creak | 12 | new · Foley/Texture group |
| 11 | **Crackling** | crackle static · electric crackle · vinyl crackle · surface crackle | 12 | extends **Tape→Crackle / Vinyl** |
| 12 | **AirCans** | spray can · aerosol · air hiss · pressure release steam | 12 | new · Air group (extends **Tape→Air**) |
| 13 | **Mechanical** | machine hum · motor running · engine idle · mechanism | 16 | new · Machinery group |
| 14 | **Electrical** | electrical hum · electric buzz · fluorescent hum · transformer hum | 12 | extends **Tape→Hum** (real hum) |
| 15 | **Metal** | metal scrape · metal rattle · metal texture · metal resonance | 10 | new · Machinery/Texture group |
| 16 | **RoomTone** | room tone · crowd murmur · cafe ambience · office ambience | 14 | new · Ambience group |
| 17 | **Appliances** | refrigerator hum · washing machine · fan hum · air conditioner | 12 | new · Machinery group |
| 18 | **Transit** | train interior · car interior · traffic · subway | 12 | new · Ambience group |
| | | **TOTAL** | **252** | |

The exact per-category queries + targets live in `CATEGORY_PLAN` at the top of the script — edit
there. Categories 4, 11, 12, 14 deliberately *deepen* the current synthesized Tape/Vinyl/Space menu
entries with real recordings; the rest are brand-new subcategory groups.

---

## D. Freesound API specifics (verified against current docs, 2026-07-16)

Docs read: [API index](https://freesound.org/docs/api/) ·
[Overview](https://freesound.org/docs/api/overview.html) ·
[Authentication](https://freesound.org/docs/api/authentication.html) ·
[Resources](https://freesound.org/docs/api/resources_apiv2.html) ·
[API Terms of Use](https://freesound.org/docs/api/terms_of_use.html) ·
[TOS](https://freesound.org/help/tos_api/).

- **Base URL:** `https://freesound.org/apiv2`
- **Text search endpoint:** `GET /apiv2/search/text/`
  Example: `https://freesound.org/apiv2/search/text/?query=dogs&token=YOUR_API_KEY`
  (The docs' quickstart uses this exact path; a shorter `/apiv2/search/` alias also appears, but
  `/search/text/` is the canonical, long-standing endpoint the script uses.)
  Params used: `query`, `filter`, `sort`, `page`/`next` cursor, `page_size` (**max 150**, default
  15), `fields`, and optionally `group_by_pack`.
- **License filter syntax:** `filter=license:"Creative Commons 0"`. Valid license strings are
  `"Attribution"`, `"Attribution NonCommercial"`, `"Creative Commons 0"` (and legacy `"Sampling+"`).
  **We accept only `"Creative Commons 0"`.** Combine clauses in one `filter` (space-separated), e.g.
  `filter=license:"Creative Commons 0" duration:[1.0 TO 30.0]` (Solr range syntax).
- **`fields` names:** `id, name, tags, description, license, username, url, type, channels, filesize,
  duration, samplerate, previews, images, download, num_downloads, avg_rating, created, pack, md5,
  is_explicit` … Default if omitted: `id,name,tags,username,license`.
  - **`previews`** object keys: `preview-hq-mp3`, `preview-lq-mp3`, `preview-hq-ogg`, `preview-lq-ogg`
    (full CDN URLs). `preview-hq-*` ≈ 128–192 kbps.
  - **`download`** field = the API download URL for the **original** file.
  - **`type`** = original file extension (`wav`, `aiff`, `flac`, `mp3`, `ogg`, …) — used to name
    original downloads.
- **Sound download endpoint:** `GET /apiv2/sounds/<sound_id>/download/` — **OAuth2 required**
  (`Authorization: Bearer <access_token>`). Previews are plain CDN URLs (token header suffices).
- **Auth headers:** token = `Authorization: Token <key>` (or `?token=<key>`); OAuth2 =
  `Authorization: Bearer <access_token>`.
- **OAuth2 flow:** authorize (`/oauth2/authorize/`, `response_type=code`) → code (**10-min** life) →
  `POST /oauth2/access_token/` (`grant_type=authorization_code`) → access token (**24h**) +
  refresh token (`grant_type=refresh_token` to renew).
- **Rate limits:** standard **60/min, 2000/day**; upload/comment/rate tier **30/min, 500/day**;
  over-limit → **429**. Multiple keys to dodge limits is prohibited. More headroom → email
  `mtg@upf.edu`.

### D.1 — Terms of Use / redistribution (READ THIS)

Two **separate** legal instruments — don't conflate them:

1. **The sound's license (CC0)** governs the *audio content*. CC0 = public-domain dedication:
   commercial use ✅, redistribution ✅, modification ✅, **no attribution required** ✅, irrevocable.
   *This is what legally lets us bundle & sell the sounds.* It attaches to the sound however you
   obtain it (API or manual website download).
2. **The Freesound API Terms of Use** govern *use of the API service itself*. Key clauses:
   - Commercial use of the **API**: *"Terms for commercial use of the API will be negotiated on a
     case-by-case basis with UPF, including limits regarding the volume of use."*
   - Must not *"replicate Freesound in another site or present Freesound data pretending it is
     yours,"* and must *"make reasonable use of the API and respect request limits"* (no multi-key
     circumvention).
   - Must handle content *"in accordance with the applicable Content license/s"* — which for CC0
     imposes **no** restriction.

**Risk assessment:** the CC0 sounds are unambiguously safe to bundle in a paid product. The only open
item is the API-TOS commercial clause, which is a **disclosure/permission** matter, **not** a
copyright block. **Action:** email `mtg@upf.edu` describing the use ("bundling CC0 sounds as factory
content in a commercial VST; using the API to identify/fetch them; ~250 one-time downloads") and ask
whether that's covered or needs a commercial API agreement. They are generally accommodating,
especially for CC0. If any friction, the fallback is trivial: identify sounds via the API, download
the originals manually from each sound's page — CC0 rights are identical either way.

Even though CC0 requires no credit, keep `manifest.json` (id/uploader/url/license) permanently as our
provenance record, and consider a bundled `CREDITS.txt` thanking contributors as goodwill (optional).

---

## E. Bundling recommendations

### E.1 — Size estimate & format
~250 clips, trimmed to loop-friendly length. Rough per-clip @ 48 kHz:

| Format | ~per 8-s clip | ~250 clips | Notes |
|---|---|---|---|
| WAV 16-bit stereo | ~1.5 MB | ~375 MB | too big to embed |
| WAV 16-bit mono | ~0.75 MB | ~190 MB | |
| **FLAC 16-bit (lossless)** | **~0.4–0.8 MB** | **~120–180 MB** | **recommended** — lossless, ~50% of WAV |
| OGG Vorbis q6 | ~0.2 MB | ~50 MB | lossy; smears broadband hiss — avoid for the master library |

**Recommend:** ship **FLAC, 48 kHz, 16-bit** (JUCE's `FlacAudioFormat` decodes it natively).
16-bit is transparent for noise beds; 24-bit is wasted bytes. Noise textures are broadband, so avoid
lossy for the shipped master (OGG/MP3 warble the hiss). ~**120–180 MB** total → very manageable on
disk.

- **Mono vs stereo:** keep **stereo** where the field is genuinely spatial (ocean, wind, rain, room
  tone, crowd, forest) and **fold to mono** for point-source foley (footsteps, doors, switches,
  spray cans) to save ~half the size. Simplest alternative: keep everything stereo (~180 MB, still
  fine).
- **Length:** trim to **~4–10 s** loop-friendly regions (long enough not to feel static, short
  enough to keep the library lean). The `duration:[1 TO 30]` search filter already excludes
  one-shots and long field recordings.

### E.2 — Embed in BinaryData vs ship a factory folder

**Ship a factory folder on disk and scan at runtime — do NOT embed in JUCE BinaryData.**
- 120–180 MB of BinaryData would bloat the binary, slow plugin load, and — because we build **both
  VST3 and AU** — the bytes get **duplicated per format** (≈2×). Bad.
- Instead, install a factory folder and scan it at startup (mirrors the wavetable **cascading folder
  menu** already built, fb27–35 — reuse that infra).

**Install path (factory content):**
- macOS (per-user): `~/Library/Application Support/Waves Crate/Terrain Instrument/Noise/`
  or system-wide (installed by the `.pkg`): `/Library/Application Support/Waves Crate/Terrain
  Instrument/Noise/`
- Windows: `C:\ProgramData\Waves Crate\Terrain Instrument\Noise\`

**Folder layout = the menu structure** (one subfolder per §C category):
```
Noise/
  Ocean/      Ocean_distant-surf_12345.flac  …
  Rain/       Rain_on-roof_67890.flac        …
  Footsteps/  …
  Doors/      …
  … (18 subfolders)
```
The Noise type menu's **cascading subcategory** picker is populated by scanning these subfolders
(folder name → submenu group; file → entry). Synthesized **Colors / Tape / Vinyl / Space** stay at
the top of the menu; the scanned **Textures** section (these 18 groups) sits beneath. Ties directly
into the noise engine's **Imports P5** work — the same scan/loader path serves both factory and
user imports.

### E.3 — Seamless-loop processing (offline bake step)

These play as **looping** textures, so the loop seam must not click. Add a **pre-processing bake**
before packaging (not at runtime — bake once, ship clean loops):
1. **DC-offset removal** + **peak/loudness normalise** so every texture sits at a similar level
   (addresses the "ear-level-match makeup gains" open item in memory). Loudness-normalise to a
   target RMS/LUFS rather than peak, so beds match by perceived loudness.
2. **Trim to a clean loop region** near zero-crossings; target ~4–10 s.
3. **Equal-power crossfade the tail into the head** (~50–200 ms) so the wrap is seamless — reuse the
   project's existing **equal-gain + smoothstep loop-crossfade** (see the sample loop-crossfade
   overhaul memory). Alternatively the runtime sample-player-as-noise engine can do a live crossfade
   loop, but baking it is cleaner and cheaper.
4. Export to the shipped FLAC/48k/16-bit spec.

A tiny follow-up script (`scripts/noise-bake.py`, TODO) can batch this with `ffmpeg`/`sox` or a JUCE
offline tool. Out of scope for this fetch task but noted for the pipeline.

---

## Open items / TODO
- [ ] Max: create account + get API key (`/apiv2/apply`); export `FREESOUND_API_TOKEN`.
- [ ] Max: run `--dry-run` to confirm ≥200 CC0 hits before any download.
- [ ] Max: email `mtg@upf.edu` to clear commercial API use (disclosure, not a copyright block).
- [ ] Decide preview vs original (recommend original/OAuth2 for the shipped master).
- [ ] Build `scripts/noise-bake.py` (normalise + trim + seam-crossfade + FLAC export).
- [ ] Wire the factory-folder scan into the Noise menu's cascading subcategory picker (reuse
      wavetable folder-scan infra); place under a new "Textures" section below Colors/Tape/Vinyl/Space.
- [ ] Manual quality-cull pass after fetch (drop noisy/mislabelled/short clips; the padding to 252
      covers the loss).
