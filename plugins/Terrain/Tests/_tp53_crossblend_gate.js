// tp53 — EFGH CROSS-BLEND WITH ABCD, the page half (the DSP half is Tests/au_crossblend.cpp).
//   Max: "EFGH cannot cross-blend with ABCD — wanted: the LFO blend menu's scroll list, A through H."
//   node Tests/_tp53_crossblend_gate.js
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs'); const path = require('path');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const cpp = fs.readFileSync('Source/PluginEditor.cpp', 'utf8'); const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX('); const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join('');
const html = fs.readFileSync('Source/ui/public/index.html', 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(require('os').tmpdir(), 'tp53_xb.html'); fs.writeFileSync(PAGE, html);
let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };

// open a carrier's Blend-1 menu and read back its Source list + what a pick writes
const openBlend = (osc) => {
  const dev = document.getElementById('osc-' + osc + '-device');
  const pill = dev && dev.querySelector('.back-only .blend-pills .blend-pill');
  if (!pill) return { err: 'no pill on osc-' + osc };
  window.__xbWrites = [];
  if (!window.__xbHooked) { window.__xbHooked = 1; const real = window.__setSynParam;
    window.__setSynParam = function (id, v) { window.__xbWrites.push(id + '=' + v); return real ? real.apply(this, arguments) : undefined; }; }
  pill.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 300, clientY: 260, button: 2 }));
  const ctl = document.querySelector('#syn-panel .syn-ctx-menu.act');
  const row = ctl && [...ctl.querySelectorAll('.syn-ctx-item')].find(e => /Blend mode/.test(e.textContent));
  if (!row) return { err: 'no Blend mode row' };
  row.click();                                                     // -> the family menu
  const fam = document.querySelector('#syn-panel .syn-ctx-menu.act');
  const fm  = fam && [...fam.querySelectorAll('.syn-ctx-item')].find(e => e.textContent.trim().indexOf('FM') === 0);
  if (!fm) return { err: 'no FM row' };
  fm.click();                                                      // arm FM -> the Source section appears
  const m = document.querySelector('#syn-panel .syn-ctx-menu.act');
  const scroll = m && m.querySelector('.bp-lfo-scroll');
  return { sections: [...m.querySelectorAll('.syn-ctx-section')].map(e => e.textContent.trim()),
           scrolls:  m ? m.querySelectorAll('.bp-lfo-scroll').length : 0,
           srcRows:  scroll ? [...scroll.children].map(e => e.textContent.trim()) : [],
           maxH:     scroll ? getComputedStyle(scroll).maxHeight : null,
           overflow: scroll ? getComputedStyle(scroll).overflowY : null,
           writes:   window.__xbWrites.slice() };
};
const pickSrc = (label) => {
  const m = document.querySelector('#syn-panel .syn-ctx-menu.act');
  const scroll = m && m.querySelector('.bp-lfo-scroll');
  const row = scroll && [...scroll.children].find(e => e.textContent.trim() === label);
  if (!row) return { err: 'no row ' + label };
  window.__xbWrites = [];
  row.click();
  return { writes: window.__xbWrites.slice() };
};

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 }); await p.evaluateOnNewDocument(stubSrc + '\nstub();');
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark'));
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(1200);

  /*  [0] 🚨 THE TWO SIDES MUST AGREE ON THE COUNT — fb373's law, and it bites twice here because
      NSRC both WRITES (v / (NSRC-1)) and READS BACK (x (NSRC-1)). Read off DISK, from the two files
      that actually ship, so a rewrite of either reds this bar instead of silently renumbering every
      saved blend source. */
  const proc  = fs.readFileSync('Source/PluginProcessor.cpp', 'utf8');
  const block = (proc.match(/const juce::StringArray srcs\s*\{[\s\S]*?\};/) || [''])[0];
  const cppN  = (block.match(/"/g) || []).length / 2;
  const jsN   = (function () { const m = /var NSRC\s*=\s*(\d+)/.exec(fs.readFileSync('Source/ui/public/index.html', 'utf8')); return m ? +m[1] : -1; })();
  ok(cppN === 21 && jsN === 21, '[0] the C++ srcs StringArray and the page\'s NSRC are both 21 — 17 frozen + the other bank\'s four, appended', JSON.stringify({ cpp: cppN, js: jsN }));
  ok(/"Osc A\/E", "Osc B\/F", "Osc C\/G", "Osc D\/H"/.test(block),
     '[0] and the four are named for the PAIR, because both banks share one list: from A the 17th is E, from E it is A', block.slice(-120));

  // [1] OSC E — the carrier Max was standing on. Its sources must be A through H, not E..H.
  const e = await p.evaluate(openBlend, 'e');
  ok(!e.err && e.srcRows.length === 7 && e.srcRows.join(',') === 'Osc A,Osc B,Osc C,Osc D,Osc F,Osc G,Osc H',
     '[1] from OSC E the sources are A through H (itself skipped — that is Self)', JSON.stringify(e.srcRows || e));
  ok(e.scrolls >= 2 && e.maxH === '68px' && e.overflow === 'auto',
     '[1] and they are the LFO list\'s OWN scroll window — 3 rows visible, wheel for the rest (Max: "the LFO blend menu\'s scroll list")', JSON.stringify({ scrolls: e.scrolls, maxH: e.maxH, overflow: e.overflow }));

  // [2] picking a CROSS source writes the appended index, not a bank-mate's
  const eA = await p.evaluate(pickSrc, 'Osc A');
  const wrote = (eA.writes || []).filter(w => /SYN_OSC_E_WSLOT1_SRC/.test(w));
  const norm = wrote.length ? parseFloat(wrote[wrote.length - 1].split('=')[1]) : -1;
  ok(Math.abs(norm - 17 / 20) < 1e-6, '[2] from E, "Osc A" writes source index 17 (the other bank\'s first) — 17/20 normalised', JSON.stringify({ wrote: wrote, norm: norm }));
  const eF = await p.evaluate(async () => { const r = (function () { const m = document.querySelector('#syn-panel .syn-ctx-menu.act'); const sc = m && m.querySelector('.bp-lfo-scroll'); const row = sc && [...sc.children].find(x => x.textContent.trim() === 'Osc F'); window.__xbWrites = []; if (row) row.click(); return window.__xbWrites.slice(); })(); return r; });
  const wroteF = (eF || []).filter(w => /SYN_OSC_E_WSLOT1_SRC/.test(w));
  const normF = wroteF.length ? parseFloat(wroteF[wroteF.length - 1].split('=')[1]) : -1;
  ok(Math.abs(normF - 1 / 20) < 1e-6, '[2] and a BANK-MATE still writes its plain 0..3 index — F from E is 1, not 18', JSON.stringify({ wrote: wroteF, norm: normF }));
  await p.evaluate(() => window.__synHideMenu && window.__synHideMenu());

  // [3] OSC A — the same list, read from the other side; the pair index means E there
  const a = await p.evaluate(openBlend, 'a');
  ok(!a.err && a.srcRows.join(',') === 'Osc B,Osc C,Osc D,Osc E,Osc F,Osc G,Osc H',
     '[3] from OSC A the sources are A through H too (itself skipped)', JSON.stringify(a.srcRows || a));
  const aE = await p.evaluate(pickSrc, 'Osc E');
  const wroteE = (aE.writes || []).filter(w => /SYN_OSC_A_WSLOT1_SRC/.test(w));
  const normE = wroteE.length ? parseFloat(wroteE[wroteE.length - 1].split('=')[1]) : -1;
  ok(Math.abs(normE - 17 / 20) < 1e-6, '[3] 🚨 THE INDEX IS BANK-RELATIVE — "Osc E" from A writes the SAME 17 that "Osc A" wrote from E. One list, both banks (and it is why the choice is named "Osc A/E")', JSON.stringify({ wrote: wroteE, norm: normE }));

  // [4] the pill's face must name the letter it is actually reading
  const face = await p.evaluate(() => { const d = document.getElementById('osc-a-device'); const pl = d.querySelector('.back-only .blend-pills .blend-pill'); return pl.textContent.trim(); });
  ok(/\(E\)$/.test(face), '[4] the pill reads FM(E) on A — the face names the other bank\'s letter, not "?" and not "A"', face);

  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
