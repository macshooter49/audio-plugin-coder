// floor_page.js — THE FLOOR PAGE, FOUND FOR YOU.
//
//  Three gates measure a change against the page AS IT STOOD BEFORE the change (fb462's law: a
//  page error, a pixel, a footprint only counts if the pre-change page did not already have it).
//  They took that page from an environment variable, and without it the bar went red with
//  "PREPAGE not set/found — cannot compare" — which every session then read as pre-existing
//  noise and stepped over (fb563/fb564: eleven runs, three red bars, zero information).
//
//  The floor is git's business. When the variable is not set, this hands the gate HEAD's copy of
//  index.html (the last committed page): for uncommitted work that is exactly the pre-change page;
//  on a clean tree it is the page itself, and the bar becomes a smoke test that still cannot pass
//  vacuously — if git is unavailable the gate gets '' and says so. ONE definition, three callers.
//
//    const PRE = require('./floor_page')('WARPM_PREPAGE');
//
const fs = require('fs'), path = require('path'), os = require('os');
const { execSync } = require('child_process');
module.exports = function floorPage (envVar) {
  const given = process.env[envVar] || '';
  if (given) return fs.existsSync(given) ? given : '';
  try {
    const root = path.join(__dirname, '..', '..', '..');                       // the repo (this file lives in plugins/Terrain/Tests)
    // ⚠️ fb605 — THE FLOOR MUST COME OUT OF THE RIGHT PLUGIN, AND THE RENAME MADE THAT SUBTLE.
    // This commit moved the synth from plugins/TerrainInstrument to plugins/Terrain — and the
    // path plugins/Terrain was ALREADY TAKEN, by the FX plugin, in every commit before it. So a
    // naive "try plugins/Terrain first, fall back" resolves a PRE-rename HEAD to the FX's page
    // (357 KB) instead of the synth's (2.7 MB) and hands every caller a floor from the wrong
    // plugin — green, silent, meaningless. Measured, not guessed: at baffa4c the two paths hold
    // 357,457 and 2,739,378 bytes.
    // So the ERA is decided first: if this HEAD still has plugins/TerrainInstrument, that IS the
    // synth and plugins/Terrain is the FX. Otherwise the rename has landed and plugins/Terrain is
    // the synth. Deterministic on both sides of the commit.
    const has = p => { try { execSync('git cat-file -e HEAD:' + p, { cwd: root, stdio: 'ignore' }); return true; }
                       catch (e) { return false; } };
    const preRename = 'plugins/TerrainInstrument/Source/ui/public/index.html';
    const rel = has(preRename) ? preRename : 'plugins/Terrain/Source/ui/public/index.html';
    const sha  = execSync('git rev-parse --short HEAD', { cwd: root, stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim();
    const out  = path.join(os.tmpdir(), 'ti_floor_' + sha + '.html');
    if (! fs.existsSync(out)) fs.writeFileSync(out, execSync('git show HEAD:' + rel, { cwd: root, maxBuffer: 64 * 1024 * 1024, stdio: ['ignore', 'pipe', 'ignore'] }));
    return out;
  } catch (e) { return ''; }
};
