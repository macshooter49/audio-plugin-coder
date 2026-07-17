#!/usr/bin/env python3
"""
freesound-fetch.py  --  Bulk-acquire CC0 noise/texture/foley for the
Terrain Instrument "Noise" factory library (Waves Crate).

Legal note: this tool ONLY keeps sounds whose license is *exactly*
"Creative Commons 0" (public-domain dedication -> commercial use +
redistribution allowed, no attribution required). It double-checks the
license on every single result (defence in depth) and never trusts the
server-side filter alone. It also writes a full provenance manifest for
our records even though CC0 requires no attribution.

Dependencies: NONE beyond the Python 3 standard library (urllib/json).
Runs on the stock macOS python3. No `pip install` required.

Secrets are read from environment variables ONLY -- never hard-code them:
  FREESOUND_API_TOKEN     (required)  token/api-key  -> preview downloads + search
  FREESOUND_OAUTH_TOKEN   (optional)  OAuth2 access token -> ORIGINAL downloads
  FREESOUND_CLIENT_ID     (for `oauth` helper only)
  FREESOUND_CLIENT_SECRET (for `oauth` helper only)

Typical usage
-------------
  # 0. Get an API key at https://freesound.org/apiv2/apply  (see the plan doc)
  export FREESOUND_API_TOKEN='xxxxxxxx'

  # 1. See what's available WITHOUT downloading anything (safe, recommended first):
  python3 scripts/freesound-fetch.py --dry-run

  # 2a. Download HQ previews (~128-192kbps ogg) -- token only, simplest:
  python3 scripts/freesound-fetch.py --mode preview --out ./noise-factory

  # 2b. Download ORIGINAL files (best quality, PAID-PRODUCT choice) -- needs OAuth2:
  python3 scripts/freesound-fetch.py oauth          # one-time, prints access token
  export FREESOUND_OAUTH_TOKEN='yyyyyyyy'
  python3 scripts/freesound-fetch.py --mode original --out ./noise-factory

The run is IDEMPOTENT: a sound already recorded in manifest.json (or already
present on disk) is skipped, so you can stop/resume freely.
"""

import argparse
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# --------------------------------------------------------------------------
# Verified Freesound APIv2 facts (checked against live docs 2026-07-16)
# --------------------------------------------------------------------------
BASE = "https://freesound.org/apiv2"
SEARCH_URL = BASE + "/search/text/"          # canonical text-search endpoint
OAUTH_AUTHORIZE = BASE + "/oauth2/authorize/"
OAUTH_TOKEN = BASE + "/oauth2/access_token/"

# CC0 has TWO representations. The API search FILTER wants the human NAME
# ("Creative Commons 0"), but each result's returned "license" field is a URL.
# Use the name for the filter; match the URL(s) for the verbatim re-check.
CC0_LICENSE  = "Creative Commons 0"                                   # for the search FILTER
CC0_RETURNED = ("http://creativecommons.org/publicdomain/zero/1.0/",  # what the API RETURNS
                "https://creativecommons.org/publicdomain/zero/1.0/")

# Duration window (seconds). Loop-friendly texture beds; skip one-shots/epics.
DURATION_MIN = 1.0
DURATION_MAX = 30.0

# Fields we ask the API to return for every hit.
FIELDS = "id,name,license,previews,download,type,duration,username,url,tags"

PAGE_SIZE = 150          # documented maximum
MIN_REQUEST_INTERVAL = 1.2   # seconds between requests (<=50/min; limit is 60/min)
MAX_RETRIES = 5
USER_AGENT = "TerrainInstrument-NoiseFactory/1.0 (+wavescrate)"

# --------------------------------------------------------------------------
# Category -> search plan.  Sums to ~250 targets so we still clear 200+ after
# the strict CC0 re-check, duration filter, and de-duplication trim it down.
# Each category becomes a subfolder == a cascading sub-menu entry in the Noise
# type menu.  Tune queries/targets freely.
# --------------------------------------------------------------------------
CATEGORY_PLAN = {
    "Ocean":       {"target": 20, "queries": ["ocean waves", "sea shore", "waves beach", "surf"]},
    "Water":       {"target": 12, "queries": ["stream water", "river flowing", "waterfall", "creek"]},
    "Rain":        {"target": 16, "queries": ["rain", "rain on roof", "rainstorm", "drizzle"]},
    "Wind":        {"target": 16, "queries": ["wind", "wind howling", "storm wind", "wind trees"]},
    "Fire":        {"target": 12, "queries": ["fire crackling", "campfire", "bonfire", "flames"]},
    "Nature":      {"target": 16, "queries": ["forest ambience", "crickets night", "birds ambience", "jungle"]},
    "Footsteps":   {"target": 18, "queries": ["footsteps gravel", "footsteps wood", "footsteps snow", "walking concrete", "boots"]},
    "Foley":       {"target": 16, "queries": ["cloth movement", "paper rustle", "rummage", "object handling"]},
    "Doors":       {"target": 14, "queries": ["door creak", "door open close", "door slam", "hinge creak"]},
    "Cracking":    {"target": 12, "queries": ["wood creak", "ice cracking", "branch snap", "structure creak"]},
    "Crackling":   {"target": 12, "queries": ["crackle static", "electric crackle", "vinyl crackle", "surface crackle"]},
    "AirCans":     {"target": 12, "queries": ["spray can", "aerosol", "air hiss", "pressure release steam"]},
    "Mechanical":  {"target": 16, "queries": ["machine hum", "motor running", "engine idle", "mechanism"]},
    "Electrical":  {"target": 12, "queries": ["electrical hum", "electric buzz", "fluorescent hum", "transformer hum"]},
    "Metal":       {"target": 10, "queries": ["metal scrape", "metal rattle", "metal texture", "metal resonance"]},
    "RoomTone":    {"target": 14, "queries": ["room tone", "crowd murmur", "cafe ambience", "office ambience"]},
    "Voices":      {"target": 18, "queries": ["crowd laughing", "people talking crowd", "group laughter", "crowd cheer", "chatter murmur", "audience crowd"]},
    "Appliances":  {"target": 12, "queries": ["refrigerator hum", "washing machine", "fan hum", "air conditioner"]},
    "Transit":     {"target": 12, "queries": ["train interior", "car interior", "traffic", "subway"]},
}
# Grand total target ~= 252.


# --------------------------------------------------------------------------
# HTTP plumbing (stdlib only) with throttle + retry/backoff + 429 handling
# --------------------------------------------------------------------------
_last_request_ts = [0.0]


def _throttle():
    dt = time.time() - _last_request_ts[0]
    if dt < MIN_REQUEST_INTERVAL:
        time.sleep(MIN_REQUEST_INTERVAL - dt)
    _last_request_ts[0] = time.time()


def _request(url, headers=None, method="GET", data=None):
    """Single HTTP request with throttle + retry. Returns (status, bytes)."""
    headers = dict(headers or {})
    headers.setdefault("User-Agent", USER_AGENT)
    attempt = 0
    while True:
        attempt += 1
        _throttle()
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                return resp.getcode(), resp.read()
        except urllib.error.HTTPError as e:
            body = e.read()
            if e.code == 429:  # throttled
                retry_after = int(e.headers.get("Retry-After", "10") or "10")
                _log("  429 throttled -> sleeping %ss" % retry_after)
                time.sleep(retry_after + 1)
                continue
            if e.code in (500, 502, 503, 504) and attempt < MAX_RETRIES:
                back = 2 ** attempt
                _log("  %s server error -> retry in %ss" % (e.code, back))
                time.sleep(back)
                continue
            _log("  HTTP %s on %s :: %s" % (e.code, url, body[:200]))
            return e.code, body
        except (urllib.error.URLError, TimeoutError) as e:
            if attempt < MAX_RETRIES:
                back = 2 ** attempt
                _log("  network error (%s) -> retry in %ss" % (e, back))
                time.sleep(back)
                continue
            raise


def _get_json(url, token):
    status, body = _request(url, headers={"Authorization": "Token %s" % token})
    if status != 200:
        raise RuntimeError("Search failed (HTTP %s): %s" % (status, body[:300]))
    return json.loads(body.decode("utf-8"))


def _log(msg):
    print(msg, flush=True)


# --------------------------------------------------------------------------
# Search
# --------------------------------------------------------------------------
def build_search_url(query):
    # Solr-style filter: two space-separated clauses (license AND duration).
    filt = 'license:"%s" duration:[%s TO %s]' % (CC0_LICENSE, DURATION_MIN, DURATION_MAX)
    params = {
        "query": query,
        "filter": filt,
        "fields": FIELDS,
        "page_size": str(PAGE_SIZE),
        "sort": "downloads_desc",   # popular-first tends to mean cleaner recordings
    }
    return SEARCH_URL + "?" + urllib.parse.urlencode(params)


def search_category(token, category, queries, target, seen_ids):
    """Collect up to `target` CC0 results for one category. Returns list of hits."""
    collected = []
    for query in queries:
        if len(collected) >= target:
            break
        url = build_search_url(query)
        _log("  [%s] query=%r" % (category, query))
        while url and len(collected) < target:
            data = _get_json(url, token)
            for r in data.get("results", []):
                sid = r.get("id")
                # ---- defence in depth: verify CC0 verbatim, never trust filter ----
                # (the API RETURNS license as a URL, not the filter's "Creative Commons 0" name)
                if r.get("license") not in CC0_RETURNED:
                    continue
                if sid in seen_ids:
                    continue
                dur = r.get("duration") or 0
                if not (DURATION_MIN <= dur <= DURATION_MAX):
                    continue
                seen_ids.add(sid)
                r["_category"] = category
                collected.append(r)
                if len(collected) >= target:
                    break
            url = data.get("next")  # follow pagination cursor
    _log("  [%s] -> %d/%d found" % (category, len(collected), target))
    return collected


# --------------------------------------------------------------------------
# Download
# --------------------------------------------------------------------------
def slugify(name):
    s = re.sub(r"[^\w\s-]", "", name.lower()).strip()
    s = re.sub(r"[\s_-]+", "-", s)
    return s[:48] or "sound"


def target_filename(hit, mode):
    cat = hit["_category"]
    slug = slugify(hit.get("name", ""))
    sid = hit["id"]
    if mode == "original":
        ext = (hit.get("type") or "wav").lower()
    else:
        ext = "ogg"  # we grab preview-hq-ogg (better for broadband noise than mp3)
    return "%s_%s_%s.%s" % (cat, slug, sid, ext)


def download_one(hit, mode, out_dir, api_token, oauth_token):
    fname = target_filename(hit, mode)
    cat_dir = os.path.join(out_dir, hit["_category"])
    os.makedirs(cat_dir, exist_ok=True)
    dest = os.path.join(cat_dir, fname)
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest, "exists"

    if mode == "original":
        if not oauth_token:
            raise RuntimeError("--mode original needs FREESOUND_OAUTH_TOKEN (run the `oauth` helper).")
        dl_url = hit.get("download") or (BASE + "/sounds/%s/download/" % hit["id"])
        headers = {"Authorization": "Bearer %s" % oauth_token}
    else:
        previews = hit.get("previews") or {}
        dl_url = previews.get("preview-hq-ogg") or previews.get("preview-hq-mp3")
        if not dl_url:
            return None, "no-preview"
        # CDN preview; token header is harmless and covers auth'd variants.
        headers = {"Authorization": "Token %s" % api_token}

    status, body = _request(dl_url, headers=headers)
    if status != 200 or not body:
        return None, "http-%s" % status
    tmp = dest + ".part"
    with open(tmp, "wb") as f:
        f.write(body)
    os.replace(tmp, dest)
    return dest, "downloaded"


# --------------------------------------------------------------------------
# Manifest (idempotent, keyed by freesound id)
# --------------------------------------------------------------------------
def load_manifest(path):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            try:
                return json.load(f)
            except json.JSONDecodeError:
                return {}
    return {}


def save_manifest(path, manifest):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    os.replace(tmp, path)


def manifest_entry(hit, mode, rel_path):
    return {
        "id": hit["id"],
        "name": hit.get("name"),
        "category": hit["_category"],
        "license": hit.get("license"),
        "username": hit.get("username"),
        "url": hit.get("url"),
        "duration": hit.get("duration"),
        "type": hit.get("type"),
        "tags": hit.get("tags"),
        "download_mode": mode,
        "file": rel_path,
        "source": "freesound.org",
    }


# --------------------------------------------------------------------------
# OAuth2 helper (interactive, one-time) -> prints access + refresh tokens
# --------------------------------------------------------------------------
def cmd_oauth(_args):
    client_id = os.environ.get("FREESOUND_CLIENT_ID")
    client_secret = os.environ.get("FREESOUND_CLIENT_SECRET")
    if not client_id or not client_secret:
        _log("Set FREESOUND_CLIENT_ID and FREESOUND_CLIENT_SECRET first (from freesound.org/apiv2/apply).")
        return 2
    auth_url = OAUTH_AUTHORIZE + "?" + urllib.parse.urlencode(
        {"client_id": client_id, "response_type": "code"})
    _log("\n1) Open this URL in a browser, log in, click 'Authorize':\n\n   %s\n" % auth_url)
    _log("2) You'll be redirected to your app's callback with ?code=XXXX (code lives 10 min).")
    code = input("3) Paste the code here: ").strip()
    data = urllib.parse.urlencode({
        "client_id": client_id,
        "client_secret": client_secret,
        "grant_type": "authorization_code",
        "code": code,
    }).encode("utf-8")
    status, body = _request(
        OAUTH_TOKEN, method="POST", data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"})
    if status != 200:
        _log("Token exchange failed (HTTP %s): %s" % (status, body[:300]))
        return 1
    tok = json.loads(body.decode("utf-8"))
    _log("\nSUCCESS. Access token valid ~24h; refresh token renews it.\n")
    _log("  export FREESOUND_OAUTH_TOKEN='%s'\n" % tok.get("access_token"))
    _log("  refresh_token: %s" % tok.get("refresh_token"))
    _log("  expires_in:    %s seconds" % tok.get("expires_in"))
    return 0


# --------------------------------------------------------------------------
# Main fetch command
# --------------------------------------------------------------------------
def cmd_fetch(args):
    api_token = os.environ.get("FREESOUND_API_TOKEN")
    if not api_token:
        _log("ERROR: set FREESOUND_API_TOKEN (get it at https://freesound.org/apiv2/apply).")
        return 2
    oauth_token = os.environ.get("FREESOUND_OAUTH_TOKEN")
    if args.mode == "original" and not oauth_token and not args.dry_run:
        _log("ERROR: --mode original needs FREESOUND_OAUTH_TOKEN. Run: freesound-fetch.py oauth")
        return 2

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    manifest_path = os.path.join(out_dir, "manifest.json")
    manifest = load_manifest(manifest_path)
    seen_ids = set(int(k) for k in manifest.keys())  # idempotency across runs

    plan = CATEGORY_PLAN
    if args.only:
        want = set(c.strip() for c in args.only.split(","))
        plan = {k: v for k, v in plan.items() if k in want}

    grand_total = 0
    for category, cfg in plan.items():
        hits = search_category(api_token, category, cfg["queries"], cfg["target"], seen_ids)
        if args.dry_run:
            grand_total += len(hits)
            continue
        for hit in hits:
            path, outcome = download_one(hit, args.mode, out_dir, api_token, oauth_token)
            if outcome in ("downloaded", "exists") and path:
                rel = os.path.relpath(path, out_dir)
                manifest[str(hit["id"])] = manifest_entry(hit, args.mode, rel)
                grand_total += 1
                _log("    %-11s %s" % (outcome, rel))
            else:
                _log("    SKIP (%s) id=%s %r" % (outcome, hit["id"], hit.get("name")))
            save_manifest(manifest_path, manifest)  # save-as-you-go = resumable

    if args.dry_run:
        _log("\nDRY RUN complete. CC0 hits found across categories: %d (target >=200)." % grand_total)
        _log("No files downloaded. Re-run without --dry-run to fetch.")
    else:
        _log("\nDONE. %d sounds in manifest -> %s" % (len(manifest), manifest_path))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description="Fetch CC0 noise/texture library from Freesound.")
    sub = ap.add_subparsers(dest="cmd")

    fp = sub.add_parser("fetch", help="search + (optionally) download (default)")
    fp.add_argument("--mode", choices=["preview", "original"], default="preview",
                    help="preview = HQ ogg via token; original = source file via OAuth2")
    fp.add_argument("--out", default="./noise-factory", help="output directory")
    fp.add_argument("--only", default="", help="comma-separated category subset")
    fp.add_argument("--dry-run", action="store_true", help="search only; download nothing")
    fp.set_defaults(func=cmd_fetch)

    op = sub.add_parser("oauth", help="interactive OAuth2 flow -> prints access token")
    op.set_defaults(func=cmd_oauth)

    # allow bare flags (no subcommand) to mean `fetch`
    if not argv or argv[0].startswith("-"):
        argv = ["fetch"] + argv
    args = ap.parse_args(argv)
    if not getattr(args, "func", None):
        ap.print_help()
        return 1
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
