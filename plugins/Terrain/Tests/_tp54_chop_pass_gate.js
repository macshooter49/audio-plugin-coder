// tp54 — MAX'S SECOND PASS ON THE CHOP PAGE (page; the overlay is extracted from PluginEditor.cpp).
//   🚨 THIS GATE EXISTS BECAUSE _tp53_list_gate DID NOT. That one asserted the chop panel's STRUCTURE
//   and CSS with the panel forced .open and no target, so it never asked the panel to PAINT — and it
//   shipped a panel whose every control was dead (tp53 deleted fmtMs/fmtPct/fmtPitch/fmtStretch along
//   with the ADSR canvas, and ovApplyState aborts on the first ReferenceError). Every bar below drives
//   the thing the way a hand does: a real sample, a real right-click, a real drag.
//   node Tests/_tp54_chop_pass_gate.js
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs'); const path = require('path');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const cpp = fs.readFileSync('Source/PluginEditor.cpp', 'utf8'); const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX('); const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join('');
const html = fs.readFileSync('Source/ui/public/index.html', 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(require('os').tmpdir(), 'tp54_gate.html'); fs.writeFileSync(PAGE, html);
let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };
const fakeSample = () => { const N = 600, mn = [], mx = [];
  for (let i = 0; i < N; i++) { const e = Math.exp(-i / 180); mx.push(0.6 * e); mn.push(-0.6 * e); }
  window.onSampleLoaded({ filename: 'Probe One Shot.wav', lengthSamples: 96000, peaksMin: mn, peaksMax: mx, rootMidiNote: 60 }); };
const mids = () => [...document.querySelectorAll('#ti-root-picker, #ti-layer-pads > *, #ti-arm, #ti-mode-toggle > *, #ti-play-mode-toggle > *, #ti-slices-btn, #ti-lib > *, .ti-bpm-display')]
  .map(e => { const r = e.getBoundingClientRect(); return r.height ? +((r.top + r.bottom) / 2).toFixed(1) : null; }).filter(v => v !== null);

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 }); await p.evaluateOnNewDocument(stubSrc + '\nstub();');
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark'));
  await p.evaluate(() => document.getElementById('mix-btn').click()); await sleep(1400);
  await p.evaluate(fakeSample); await sleep(700);

  // ── [1] ONE CENTERLINE (fb275). Max: "these boxes don't follow the center line." ──
  await p.evaluate(() => { const q = document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); if (q[1]) q[1].dispatchEvent(new MouseEvent('click', { bubbles: true })); }); await sleep(800);
  const m = await p.evaluate(mids);
  const spread = Math.max(...m) - Math.min(...m);
  ok(m.length >= 12 && spread === 0, '[1] every control in the hero strip shares ONE centerline — the key, A-D, Arm, the mode pills, Slices, the library and the BPM', 'n=' + m.length + ' spread=' + spread + ' at ' + m[0]);

  // ── [2] "Slices" — the house case and size, and it may not reach the library ──
  const sl = await p.evaluate(() => { const s2 = document.getElementById('ti-slices-btn'), lib = document.getElementById('ti-lib'), pp = document.querySelector('#ti-play-mode-toggle .ti-play-pill');
    const a = s2.getBoundingClientRect(), l = lib.getBoundingClientRect(), q = pp.getBoundingClientRect();
    return { txt: s2.querySelector('.ti-slices-label').textContent, h: +a.height.toFixed(1), ph: +q.height.toFixed(1), fs: getComputedStyle(s2).fontSize, pfs: getComputedStyle(pp).fontSize, gap: +(l.left - a.right).toFixed(1) }; });
  ok(sl.txt === 'Slices' && Math.abs(sl.h - sl.ph) < 0.6 && sl.fs === sl.pfs, '[2] the Slices pill is the play pills\' own size and case, not a shouted 22 px box', JSON.stringify(sl));
  ok(sl.gap > 12, '🚨 [2] and it CLEARS the sample library — MEASURED at 17 px of OVERLAP before tp54 (the library\'s left arrow sat underneath it)', 'gap now ' + sl.gap + ' px');

  // ── [3] the Slices drawer is the house menu ──
  await p.evaluate(() => document.getElementById('ti-slices-btn').dispatchEvent(new MouseEvent('click', { bubbles: true }))); await sleep(700);
  const dr = await p.evaluate(() => { const d = document.getElementById('ti-slicer-drawer'); const c = getComputedStyle(d);
    const pill = d.querySelector('.ti-submode-pill.active'), pc = pill ? getComputedStyle(pill) : null;
    const sld = d.querySelector('#ti-fade-slider');
    return { bg: c.backgroundColor, bf: (c.backdropFilter || c.webkitBackdropFilter || 'none'), rad: c.borderTopLeftRadius,
      pillBg: pc && pc.backgroundColor, pillBorder: pc && pc.borderTopColor, pillTxt: pill && pill.textContent,
      words: [...d.querySelectorAll('.ti-action-label')].map(e => e.textContent.trim()),
      sldH: sld ? getComputedStyle(sld).height : null }; });
  ok(dr.bg === 'rgba(22, 20, 34, 0.72)' && /blur\(20px\)/.test(dr.bf) && dr.rad === '12px', '[3] the Slices drawer wears the two-pane browser\'s OWN glass — one material for every menu in the plugin', JSON.stringify({ bg: dr.bg, bf: dr.bf, rad: dr.rad }));
  ok(dr.pillBg === 'rgba(0, 0, 0, 0)' && /183, 148, 255|b794ff/.test(dr.pillBorder || '') && dr.pillTxt === 'Chop', '[3] a selected pill in it is a purple OUTLINE with nothing filled — Max: "no highlighted fillings"', JSON.stringify({ bg: dr.pillBg, border: dr.pillBorder, txt: dr.pillTxt }));
  ok(dr.words.join(',') === 'Random,Fade', '[3] and its labels lost the colons and the shouting', dr.words.join(','));

  // ── [4] no glow anywhere a thing can be selected ──
  const glow = await p.evaluate(() => ['#ti-layer-pads .ti-layer-pad.active', '#ti-mode-toggle .ti-mode-pill.active', '#ti-play-mode-toggle .ti-play-pill.active', '#ti-slices-btn']
    .map(s2 => { const e = document.querySelector(s2); if (!e) return s2 + ':missing'; const c = getComputedStyle(e); return s2 + '|' + c.boxShadow + '|' + c.textShadow; }));
  ok(glow.every(g => /\|none\|none$/.test(g)), '[4] nothing blooms — Max: "the purple outline with the white stroke has a glow to it, I don\'t want that glow"', glow.join('  '));

  // ── [5] THE PANEL ACTUALLY WORKS (the bar tp53 did not have) ──
  await p.evaluate(() => document.getElementById('ti-slices-btn').dispatchEvent(new MouseEvent('click', { bubbles: true }))); await sleep(400);
  await p.evaluate(() => { const q = document.querySelectorAll('#ti-mode-toggle .ti-mode-pill'); q[0].dispatchEvent(new MouseEvent('click', { bubbles: true })); }); await sleep(700);
  const wf = await p.evaluate(() => { const c = document.getElementById('waveform-canvas'); const r = c.getBoundingClientRect(); return { x: Math.round(r.left + r.width / 2), y: Math.round(r.top + r.height / 2) }; });
  await p.mouse.click(wf.x, wf.y, { button: 'right' }); await sleep(800);
  const painted = await p.evaluate(() => { const pn = document.getElementById('ti-chop-panel');
    return { open: pn.classList.contains('open'),
      rows: [...pn.querySelectorAll('.ov-ad-row')].map(r => r.dataset.h + '=' + r.querySelector('.ov-ad-val').textContent + '@' + r.querySelector('.ov-ad-fill').style.width),
      tiles: [...pn.querySelectorAll('.ov-ctrl .ov-val')].map(e => e.textContent),
      scan: (document.getElementById('scan-pill') || {}).textContent }; });
  ok(painted.open && painted.rows.length === 5 && painted.rows.every(r => /@\d/.test(r)) && painted.tiles.join('') !== '',
     '🚨 [5] THE PANEL PAINTS — every ADSR row has a value AND a filled bar, and the three tiles read. On tp53 every one of these was blank and every control below was dead.', JSON.stringify(painted));

  // drag Attack and watch the number and the engine move together
  const tr = await p.evaluate(() => { const t = document.querySelector('#ti-chop-panel .ov-ad-row[data-h="A"] .ov-ad-track'); const r = t.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top + r.height / 2), w: Math.round(r.width) }; });
  const before = await p.evaluate(() => document.querySelector('#ti-chop-panel .ov-ad-row[data-h="A"] .ov-ad-val').textContent);
  await p.mouse.move(tr.x + 4, tr.y); await p.mouse.down(); await p.mouse.move(tr.x + Math.round(tr.w * 0.6), tr.y, { steps: 8 }); await p.mouse.up(); await sleep(400);
  const after = await p.evaluate(() => ({ v: document.querySelector('#ti-chop-panel .ov-ad-row[data-h="A"] .ov-ad-val').textContent,
                                          w: document.querySelector('#ti-chop-panel .ov-ad-row[data-h="A"] .ov-ad-fill').style.width,
                                          nat: (window.__natCount || {}).setSliceAttackMs || 0 }));
  ok(after.v !== before && parseFloat(after.w) > 50 && after.nat > 1,
     '🚨 [5] AND IT DRAGS — the bar follows the pointer across the track and every step reaches setSliceAttackMs. Before tp54 the throw aborted mousedown before the move listener was attached, so it was click-to-set with no drag at all.',
     JSON.stringify({ before: before, after: after }));

  // ── [6] the stretch modes are WORDS, and the FX chips are gone ──
  const shape = await p.evaluate(() => { const pn = document.getElementById('ti-chop-panel');
    return { modes: [...pn.querySelectorAll('.ov-mode')].map(e => (e.querySelector('.w') || {}).textContent),
             svgs: pn.querySelectorAll('.ov-mode svg').length, fx: pn.querySelectorAll('.ov-fx-chip, .ov-fx, #ti-fx-section').length,
             acts: [...pn.querySelectorAll('.ov-act')].map(e => e.textContent.trim()) }; });
  ok(shape.modes.join(',') === 'Off,Beats,Tone,Text' && shape.svgs === 0, '[6] the stretch modes are the WORDS — Max: "instead of those outdated emblems … Texture abbreviated to Text"', JSON.stringify(shape.modes));
  ok(shape.fx === 0, '[6] and the six FX chips are GONE from the DOM, not hidden (the tp53 lesson)', 'found ' + shape.fx);

  // ── [7] FINE TUNE ──
  const ft = await p.evaluate(() => { const t = document.querySelector('#ti-chop-panel .ov-ad-row[data-h="F"] .ov-ad-track'); const r = t.getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top + r.height / 2), w: Math.round(r.width) }; });
  await p.mouse.click(ft.x + Math.round(ft.w * 0.9), ft.y); await sleep(300);
  const fine = await p.evaluate(() => ({ cents: document.querySelector('#ti-chop-panel .ov-ad-row[data-h="F"] .ov-ad-val').textContent,
                                         semis: document.querySelector('#ti-chop-panel .ov-ctrl[data-ctrl="pitch"] .ov-val').textContent }));
  ok(/^\+[1-9]\d? ¢$/.test(fine.cents) && fine.semis === '+0 st',
     '🚨 [7] FINE TUNE — Max: "we gotta have a way to fine tune chops, not just semitone." The engine always could: Slice::pitchOffsetSemis is a FLOAT and the emblem\'s Math.round was throwing the cents away. The cents move and the semitone stays put.', JSON.stringify(fine));

  ok(errs.length === 0, 'no page errors', errs.join(' | '));

  // ══ [8] SOURCE BAR — REVERSED BY MAX ON 2026-09-20, AND HE IS RIGHT TWICE ══════════════════
  //  tp54 answered his question "does the stem separator work with the capture off?" with: YES, and
  //  therefore the panel must not be greyed. That was a true reading of tp43's code — writeToStemBuffer
  //  sits in the per-layer render with no captureEnabled_ above it, and the STEM rings armed off a
  //  loaded sample, entirely separately from the ~202 MB DAW-capture ring.
  //
  //  What it did not ask is what those rings COST: 600 s x SR x 2ch x float32 x 5 = ~1,058 MB per
  //  instance, paid whether or not the user has capture on. Max, 2026-09-20: "whenever I have capture
  //  off in the settings, the stem capture should just be kind of greyed out ... make sure nothing
  //  ghostly is running in the background taking up memory ... if I duplicate it then it's two times
  //  the memory." So OFF now means off for both rings (tp61), and the bar reverses with it: the write
  //  is still ungated in the render — nothing there needed changing, because totalSize == 0 is what
  //  stops it — and the panel MUST be greyed, because there is no ring behind those buttons.
  //  The memory side is pinned in Tests/stem_memory_gate.py; this is the UI's half of the same law.
  const proc = fs.readFileSync('Source/PluginProcessor.cpp', 'utf8');
  const call = proc.indexOf('writeToStemBuffer ((int) li');
  const ctx  = proc.slice(Math.max(0, call - 2600), call);
  const gated = /captureEnabled_[\s\S]{0,400}$/.test(ctx);
  ok(call > 0 && ! gated, '[8] the per-layer stem WRITE is still ungated in the render — totalSize == 0 is what stops it, so the audio path did not move');
  const armGuard = /ensureStemLayerAllocated \(int layerIdx\)[\s\S]{0,2400}?if \(! captureEnabled_/.test(proc);
  ok(armGuard, '🚨 [8b] AND THE ARM IS GATED ON CAPTURE (tp61, reversing tp54): capture off allocates no stem ring at all — ~1,058 MB per instance');
  const ed = fs.readFileSync('Source/PluginEditor.cpp', 'utf8');
  ok(/body\.ti-capture-off[\s\S]{0,240}stem-buttons/.test(ed),
     '[8c] and the stem buttons grey out to say so — offering an export with no ring behind it is the lie tp54 was trying to avoid');

  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
