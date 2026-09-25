// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_artic_gate.js — tp105 THE ARTICULATION PILL NEVER CUTS A WORD (Max: "Mediu" cut off beside ‹ Xylophone ›,
//  and "I don't like how it looks"). node Tests/_org_artic_gate.js [page.html] [out-dir]
//
//  Every articulation of every INSTALLED instrument (each map.json "artics" under the library root — TERRAIN_ORGANICS_DIR,
//  else ~/Library/WavesCrate/TerrainInstrument/Organics — plus a fixed list of the common orchestral names, so the gate
//  also runs on a machine without the library) is painted into osc A's header pill with that instrument's real name:
//   1  THE LABEL IS A WORD, NOT A TRUNCATION — the page's short form, never an ellipsis, never the full name sliced
//   2  IT FITS — no overflow (scrollWidth ≤ clientWidth) and its ink clears the ‹ by ≥ 4 px, with the instrument's name
//   3  NOTHING MOVES — ‹, the name, ›, [A] and [+] sit where they sat for the first one (≤ 0.01 px), whatever the label
//   4  THE FULL NAME STAYS — in the pill's tooltip, and as a row of the dropdown
//   5  tp107 WHITE INSIDE (Max: "always follow the same style as every other button") — every label is painted in the house
//      header type: the instrument name's colour, weight, size and tracking (9.5 px / 0.8 px, --text-primary), never the dim
//      grey; bars 1–3 hold in THAT type (the box grew 38 → 40 px and its side padding went 3 → 2 px to fit it)
//  Writes org-artic-pills.png (a few of them, stacked) into the out-dir.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs = require('fs'), path = require('path'), os = require('os');
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
const OUT = process.argv[3] || path.resolve(__dirname, '../Design/organics/impl-shots');
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };

const ROOT = process.env.TERRAIN_ORGANICS_DIR || path.join(os.homedir(), 'Library/WavesCrate/TerrainInstrument/Organics');
const LIB = [];
try { fs.readdirSync(ROOT).forEach(d => { try { const j = JSON.parse(fs.readFileSync(path.join(ROOT, d, 'map.json'), 'utf8'));
  const a = (j.artics || []).map(x => typeof x === 'string' ? x : (x && x.name) || ''); if (a.length) LIB.push({ id: d, name: j.name || d, artics: a }); } catch (e) {} }); } catch (e) {}
const COMMON = ['Sustain', 'Staccato', 'Staccatissimo', 'Spiccato', 'Pizzicato', 'Tremolo', 'Legato', 'Marcato', 'Portato', 'Harmonics', 'Col Legno', 'Sul Tasto',
  'Sul Ponticello', 'Vibrato', 'Non-Vibrato', 'Con Sordino', 'Flutter Tongue', 'Trill', 'Muted', 'Harmon Mute', 'Straight Mute', 'Cup Mute', 'Palm Mute', 'Soft Mallets',
  'Medium Mallets', 'Hard Mallets', 'Bowed', 'Expressive', 'Release', 'Principal', 'Glissando Up', 'Overblown', 'Rolled', 'Dampened'];
const SETS = LIB.concat(COMMON.map((c, i) => ({ id: 'common' + i, name: 'Salamander Grand Piano V3 Concert', artics: ['Sustain', c] })));   // each common name beside Sustain, under the longest name the slot will ever see

(async () => {
  const { b, p, errs } = await H.launch({ page: PAGE, dpr: 3 });
  await H.enable(p, 'a'); await H.setEngine(p, 'a', 7); await H.sleep(800);
  const rows = await p.evaluate((SETS) => {
    const d = document.getElementById('osc-a-device'), pill = d.querySelector('.org-artic .samp-sel-disp'), R = d.getBoundingClientRect();
    const pos = () => { const g = s => { const e = d.querySelector(s); const r = e.getBoundingClientRect(); return [r.left - R.left, r.top - R.top, r.width, r.height]; };
      return { prev: g('.org-nav[data-dir="-1"]'), next: g('.org-nav[data-dir="1"]'), name: g('.org-inst-wrap'), A: g('.osc-letter'), plus: g('.swap-btn') }; };
    const out = []; let base = null, baseInst = null;
    SETS.forEach(S => S.artics.forEach((full, i) => {
      window.__orgPaintHead('a', { ok: true, status: 'ok', id: S.id, name: S.name, family: 'violin', category: 'Strings', artics: S.artics.length > 1 ? S.artics : S.artics.concat(['Staccato']), artic: i });
      const txt = pill.textContent, rg = document.createRange(); rg.selectNodeContents(pill); const tr = rg.getBoundingClientRect(), nav = d.querySelector('.org-nav[data-dir="-1"]').getBoundingClientRect();
      const P = pos(); if (!base || baseInst !== S.id) { base = P; baseInst = S.id; }   /* the ‹ follows the NAME's width (every engine's corner is right-aligned): compare within one instrument */
      let moved = 0; Object.keys(P).forEach(k => P[k].forEach((v, j) => { if (k !== 'name' || j !== 2) moved = Math.max(moved, Math.abs(v - base[k][j])); }));
      const cs = getComputedStyle(pill), ns = getComputedStyle(d.querySelector('.org-inst-wrap .preset-display'));
      const house = ['color', 'fontWeight', 'fontSize', 'letterSpacing'].every(k => cs[k] === ns[k]);
      out.push({ house, col: cs.color, inst: S.id, full, txt, short: window.__orgArticLabels(S.artics.length > 1 ? S.artics : S.artics.concat(['Staccato']))[i], over: pill.scrollWidth - pill.clientWidth, clear: nav.left + (nav.width - 5) / 2 - tr.right, moved,   /* to the ‹'s ink: the chevron is 5 px wide, centred in its box */
        tip: (pill.parentElement.title || ''), ell: /…|\.\.\./.test(txt) }); }));
    return out; }, SETS);
  const vocab = new Set(await p.evaluate(() => window.__orgArtVocab()));
  // a deliberate word = one of the page's chosen labels, or the name's own first word whole (≤ 5 letters); a stem is reported
  const word = r => vocab.has(r.txt) || r.txt === r.full.trim().split(/\s+/)[0] || /^[A-Z] [A-Z][a-z']{1,3}$/.test(r.txt);   // a chosen label, the name's own first word, or a clash's 'G Twan'
  const bad1 = rows.filter(r => r.ell || r.txt !== r.short || !word(r));
  const stems = rows.filter(r => !word(r));
  ok(bad1.length === 0, `[1] every label is a deliberate word (${rows.length} labels, ${LIB.length} installed instruments + ${COMMON.length} common names)`,
     (stems.length ? 'stems (unknown names): ' + stems.map(r => r.full + '→' + r.txt).join(', ') + '\n        ' : '') + (bad1.length ? 'BAD: ' + bad1.map(r => r.full + '→' + r.txt).join(', ') + '\n        ' : '') + [...new Set(rows.map(r => r.full + '→' + r.txt))].join(' · '));
  const bad2 = rows.filter(r => r.over > 0.5 || r.clear < 4);
  ok(bad2.length === 0, `[2] every label fits its box and clears the ‹ by ≥ 4 px (worst clearance ${Math.min(...rows.map(r => r.clear)).toFixed(2)} px)`,
     bad2.map(r => `${r.full}→${r.txt} over ${r.over} clear ${r.clear.toFixed(2)}`).join(' · '));
  const nh = rows.filter(r => !r.house);
  const mv = Math.max(...rows.map(r => r.moved));
  ok(mv <= 0.01, `[3] nothing moves: ‹ name › [A] [+] hold their place across all ${rows.length} labels (max ${mv.toFixed(3)} px)`);
  const menu = await p.evaluate(async () => { window.__orgPaintHead('a', { ok: true, status: 'ok', id: 'x', name: 'Xylophone', family: 'marimba', category: 'Mallets & Bells', artics: ['Medium Mallets', 'Hard Mallets', 'Soft Mallets'], artic: 0 });
    window.__orgOpenArtic('a'); const m = document.querySelector('.org-artic-menu'); const items = m ? [...m.querySelectorAll('.pi .nm')].map(e => e.textContent) : [];
    const tip = document.querySelector('#osc-a-device .org-artic').title; await new Promise(r => setTimeout(r, 30)); document.body.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); return { items, tip, closed: !document.querySelector('.org-artic.open') }; });
  ok(rows.every(r => r.tip.indexOf(r.full) >= 0) && menu.items.join('|') === 'Medium Mallets|Hard Mallets|Soft Mallets',
     '[4] the full name stays: in every tooltip, and as the dropdown\'s rows', `Xylophone dropdown: ${menu.items.join(' · ')} · tooltip "${menu.tip}"`);
  ok(nh.length === 0, `[5] every label wears the house header type — the instrument name's colour/weight/size/tracking (${rows[0] && rows[0].col})`,
     nh.slice(0, 5).map(r => r.full + ' ' + r.col).join(' · '));
  // the picture: a few pills, stacked
  try { fs.mkdirSync(OUT, { recursive: true }); } catch (e) {}
  const shots = [];
  for (const [nm, ar, i] of [['Xylophone', ['Medium Mallets', 'Hard Mallets', 'Soft Mallets'], 0], ['Vibraphone', ['Soft Mallets', 'Hard Mallets', 'Bowed'], 2], ['Violin Section', ['Sustain', 'Tremolo', 'Spiccato', 'Pizzicato'], 3],
                             ['Trumpet', ['Sustain', 'Vibrato', 'Staccato', 'Harmon Mute', 'Straight Mute'], 3], ['Contrabass', ['Vibrato', 'Non-Vibrato', 'Tremolo', 'Pizzicato'], 1], ['Flute', ['Vibrato', 'Non-Vibrato', 'Expressive', 'Staccato'], 2]]) {
    await p.evaluate((nm, ar, i) => window.__orgPaintHead('a', { ok: true, status: 'ok', id: 'x', name: nm, family: 'violin', category: 'Strings', artics: ar, artic: i }), nm, ar, i);
    await H.sleep(60);
    shots.push((await p.screenshot({ encoding: 'base64', clip: await p.evaluate(() => { const r = document.getElementById('osc-a-device').getBoundingClientRect(); return { x: r.left - 4, y: r.top - 2, width: r.width + 8, height: 30 }; }) })));
  }
  const grid = await p.evaluate(async (imgs) => { const I = await Promise.all(imgs.map(async b => { const i = new Image(); i.src = 'data:image/png;base64,' + b; await i.decode(); return i; }));
    const c = document.createElement('canvas'); c.width = I[0].width; c.height = I[0].height * I.length; const x = c.getContext('2d'); I.forEach((im, k) => x.drawImage(im, 0, k * I[0].height)); return c.toDataURL('image/png').split(',')[1]; }, shots);
  fs.writeFileSync(path.join(OUT, 'org-artic-pills.png'), Buffer.from(grid, 'base64'));
  console.log('  (picture: ' + path.join(OUT, 'org-artic-pills.png') + ')');
  const real = errs.filter(e => !/formatOutput/.test(e));
  ok(real.length === 0, '[—] no page errors', real.join(' | '));
  await b.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL (crash)', e); process.exit(1); });
