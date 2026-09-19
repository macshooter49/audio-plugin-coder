// tp53 — MAX'S 2026-09-19 LIST, the six owed items (the overlay is extracted from PluginEditor.cpp, as tp49's gate does).
//   [1] the Chop footer sits where the Patcher's sits (608), not 16 px above it
//   [2] the sample library IS the two-pane sample browser, and the < > arrows step again (they were NaN since tp50)
//   [3] the chop's right-click panel wears the house glass, the INDEPENDENT row is GONE from the DOM, and the ADSR
//       graph is four house notch sliders with numbers
//   [4] a tall menu on the Patcher stops at the footer's TOP — Max: "look at the word EXTEND"
//   [6] a stretched chop's time-mode letter is transparent white, not purple
//   node Tests/_tp53_list_gate.js
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs'); const path = require('path');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const OUT = require('os').tmpdir();
const cpp = fs.readFileSync(path.join(process.cwd(), 'Source/PluginEditor.cpp'), 'utf8'); const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX('); const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join('');
const html = fs.readFileSync(path.join(process.cwd(), 'Source/ui/public/index.html'), 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(OUT, 'tp53_index.html'); fs.writeFileSync(PAGE, html);
let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };
const rect = s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { t: Math.round(r.top * 10) / 10, b: Math.round(r.bottom * 10) / 10, h: Math.round(r.height * 10) / 10 }; };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument(stubSrc + `\nstub();
    window.__loadPath = []; window.__editLayer = 0;
    (function(){ const g = window.Juce.getNativeFunction; window.Juce.getNativeFunction = function(n){
      if (n === 'loadSampleFromPath') return (pth) => { window.__loadPath.push(String(pth)); return Promise.resolve(0); };
      if (n === 'setEditingLayer') return (i) => { window.__editLayer = +i; return Promise.resolve(0); };
      if (n === 'getLayerHasSample') return () => Promise.resolve(window.__loadPath.length ? 1 : 0);
      if (n === 'listSampleImports') return () => Promise.resolve(JSON.stringify({ files: [], folders: [] }));
      return g(n); }; })();`);
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark'));

  // ── the Patcher, for the reference geometry the Chop page has to match ──
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(800);
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(1600);
  const tp = await p.evaluate(() => ({ body: document.body.className, foot: (function(){ const r = document.getElementById('footer').getBoundingClientRect(); return { t: Math.round(r.top*10)/10, b: Math.round(r.bottom*10)/10 }; })(),
    slider: (function(){ const r = document.getElementById('output-slider').getBoundingClientRect(); return { t: Math.round(r.top*10)/10, b: Math.round(r.bottom*10)/10 }; })() }));
  ok(/tp-open/.test(tp.body) && tp.foot.t === 608 && tp.foot.b === 656, '[ref] the Patcher is up and its footer fills the bottom of the box (608..656)', JSON.stringify(tp));

  // ── [4] a TALL menu on the Patcher must stop at the footer's top, never paint over the output slider ──
  const tall = await p.evaluate(() => {
    const rows = []; for (let i = 0; i < 40; i++) rows.push({ label: 'Row ' + i, onPick: function () {} });
    rows.push({ label: 'Extend', icon: true, onPick: function () {} });
    window.__synShowMenu('', rows, 300, 520);
    const m = document.querySelector('#syn-panel .syn-ctx-menu.act'); const r = m.getBoundingClientRect();
    const f = document.getElementById('footer').getBoundingClientRect();
    return { t: Math.round(r.top), b: Math.round(r.bottom), footTop: Math.round(f.top), scroll: m.scrollHeight > m.clientHeight, maxH: getComputedStyle(m).maxHeight };
  });
  ok(tall.b <= tall.footTop, '[4] a 41-row menu on the Patcher stops AT the footer — its last row never lands on the output slider (Max: "look at the word EXTEND")', JSON.stringify(tall));
  ok(tall.scroll, '[4] and it scrolls inside that space instead of being cut off (the fb275 law)', JSON.stringify(tall));
  await p.evaluate(() => window.__synHideMenu());

  // ── [1] the Chop page's footer must land exactly where the Patcher's does ──
  await p.evaluate(() => document.getElementById('mix-btn').click()); await sleep(1500);
  const chop = await p.evaluate((f) => ({ panel: (function(){ const r = document.getElementById('mix-panel').getBoundingClientRect(); return { t: Math.round(r.top), b: Math.round(r.bottom), h: Math.round(r.height) }; })(),
    foot: (function(){ const r = document.getElementById('footer').getBoundingClientRect(); return { t: Math.round(r.top*10)/10, b: Math.round(r.bottom*10)/10 }; })(),
    slider: (function(){ const r = document.getElementById('output-slider').getBoundingClientRect(); return { t: Math.round(r.top*10)/10, b: Math.round(r.bottom*10)/10 }; })() }), tp);
  ok(chop.foot.t === tp.foot.t && chop.foot.b === tp.foot.b, '[1] the Chop footer sits where the Patcher\'s sits (608..656), not 16 px above it', JSON.stringify({ chop: chop.foot, patcher: tp.foot }));
  ok(chop.slider.t === tp.slider.t, '[1] and its output slider is on the Patcher\'s line to the pixel', JSON.stringify({ chop: chop.slider, patcher: tp.slider }));
  ok(chop.panel.h === 288 && chop.panel.b === 608, '[1] the panel took the 16 px of dead ground back (272 -> 288, ending at the footer)', JSON.stringify(chop.panel));

  // ── [2] the library name opens the REAL two-pane browser, and the arrows step ──
  await p.evaluate(() => document.getElementById('ti-lib-name').dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: 600, clientY: 580 }))); await sleep(900);
  const br = await p.evaluate(() => { const e = document.querySelector('.tpb-panel'); if (!e) return null; const r = e.getBoundingClientRect(); const c = getComputedStyle(e);
    return { w: Math.round(r.width), h: Math.round(r.height), vis: c.visibility, rows: e.querySelectorAll('div').length, txt: (e.textContent || '').slice(0, 60) }; });
  ok(br && br.w === 384 && br.vis === 'visible', '[2] the sample library opens the HOUSE two-pane browser (.tpb-panel, 384 px) — not a menu of its own', JSON.stringify(br));
  await p.evaluate(() => { const c = document.querySelector('.tpb-panel'); if (c) c.remove(); });
  const before = await p.evaluate(() => window.__tiLib().idx.slice());
  await p.evaluate(() => { const n = document.querySelector('#ti-lib .ti-lib-nav[data-d="1"]'); n.dispatchEvent(new MouseEvent('click', { bubbles: true })); }); await sleep(600);
  const after = await p.evaluate(() => ({ idx: window.__tiLib().idx.slice(), label: window.__tiLib().label, loads: window.__loadPath.length }));
  ok(after.idx[0] >= 0 && after.idx[0] !== before[0] && after.loads > 0, '[2] the > arrow steps the library again (libIdx was an ARRAY since tp50 and the arrow added to the whole of it: NaN)', JSON.stringify({ before: before, after: after }));

  // ── [3] the chop's right-click panel: house glass, no INDEPENDENT row, four ADSR sliders ──
  const pn = await p.evaluate(() => {
    const el = document.getElementById('ti-chop-panel'); el.classList.add('open');
    const c = getComputedStyle(el);
    const rows = [...el.querySelectorAll('.ov-ad-row')].map(r => ({ h: r.dataset.h, lab: r.querySelector('.ov-ad-lab').textContent, val: r.querySelector('.ov-ad-val').textContent }));
    const tk = el.querySelector('.ov-ad-track'); const fl = el.querySelector('.ov-ad-fill'); const bar = el.querySelector('.ov-ad-bar');
    const ts = tk ? getComputedStyle(tk, '::before') : null;
    return { bg: c.backgroundColor, bf: (c.backdropFilter || c.webkitBackdropFilter || 'none'), rad: c.borderTopLeftRadius,
      indy: !!el.querySelector('.ov-indy, #ti-fx-indy'), svg: !!el.querySelector('.ov-env-svg, .ov-env-handle'),
      rows: rows, railH: ts ? ts.height : null, railBg: ts ? ts.backgroundColor : null,
      fillBg: fl ? getComputedStyle(fl).backgroundColor : null,
      bar: bar ? { w: getComputedStyle(bar).width, h: getComputedStyle(bar).height, bg: getComputedStyle(bar).backgroundColor } : null,
      chipsDim: (function(){ const g = el.querySelector('.ov-fx-grid'); return g ? getComputedStyle(g).opacity : null; })() };
  });
  ok(pn.bg === 'rgba(22, 20, 34, 0.72)' && /blur\(20px\)/.test(pn.bf) && pn.rad === '12px', '[3] the chop panel wears the HOUSE GLASS — .tpb-panel\'s own tint, blur and radius', JSON.stringify({ bg: pn.bg, bf: pn.bf, rad: pn.rad }));
  ok(!pn.indy, '[3] the INDEPENDENT row is GONE from the DOM, not hidden', JSON.stringify({ indy: pn.indy }));
  // ⚠️ tp54 INVERTED THIS BAR. It asserted the six FX chips were LIVE (tp53 un-greyed them when the
  //    INDEPENDENT pill went). Max then asked for the chips themselves: "you can remove those pointless
  //    effects at the bottom of that chop engine right-click menu." Gone from the DOM, so the claim is now
  //    their ABSENCE — and _tp54_chop_pass_gate [6] owns it.
  ok(pn.chipsDim === null, '[3] the six FX chips are gone from the chop menu (tp54 — the bar that asserted they were live now asserts they are not there)', JSON.stringify({ grid: pn.chipsDim }));
  // ⚠️ tp54 widened this: a FIFTH row, Fine (cents), joined the four. Max: "we gotta have a way to fine
  //    tune chops, not just semitone." The count is asserted exactly so a row appearing or vanishing reds it.
  ok(!pn.svg && pn.rows.length === 5 && pn.rows.map(r => r.h).join('') === 'ADSRF' && pn.rows[0].lab === 'Attack' && pn.rows[2].lab === 'Sustain' && pn.rows[4].lab === 'Fine',
     '[3] the ADSR graph is FIVE labelled slider rows with numbers — Attack/Decay/Sustain/Release and tp54\'s Fine; the curve and its four dots are gone', JSON.stringify(pn.rows));
  ok(pn.railH === '2px' && pn.railBg === 'rgba(255, 255, 255, 0.16)' && pn.fillBg === 'rgb(255, 255, 255)' && pn.bar.w === '2px' && pn.bar.h === '9px',
     '[3] and they are the HOUSE notch: a 2 px rail at .16 white, a white fill, a 2x9 white bar', JSON.stringify({ rail: pn.railH, railBg: pn.railBg, fill: pn.fillBg, bar: pn.bar }));

  // ── [6] the time-mode letter is transparent white ──
  const wl = await p.evaluate(() => { const d = document.createElement('div'); d.className = 'ti-slice-warp-letter'; d.textContent = 'T';
    document.getElementById('hero').appendChild(d); const c = getComputedStyle(d); const o = { color: c.color, bg: c.backgroundColor }; d.remove(); return o; });
  // ⚠️ tp54 REVERSED THIS BAR, and Max reversed it. tp53 read his "transparent white" as the LETTER and
  //    kept the dark chip behind it; the chip was the thing he was pointing at: "you can make this purple
  //    again, but I just don't want that gray highlight box around it — no boxes, just a purple letter."
  ok(wl.color === 'rgb(167, 139, 250)' && wl.bg === 'rgba(0, 0, 0, 0)',
     '[6] a stretched chop\'s time-mode letter is PURPLE with no box behind it at all', JSON.stringify(wl));

  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
