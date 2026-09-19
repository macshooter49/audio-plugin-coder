// tp49 — THE CHOP PAGE WEARS THE HOUSE (page, the C++ overlay extracted from PluginEditor.cpp). Max: rename MIX → CHOP; the buttons are
//   the flow tiles' (purple outline, white word); the key box and BPM transparent; SEQ / SYNC / play / the dice and the XY tracker gone;
//   One-Shot; the sample library with arrows; the ARM in the top right; leaving the page must not leave traces.
//   node Tests/_tp49_chop_gate.js   (extracts the overlay itself; no argument needed)
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments')); const path = require('path'); const OUT = require('os').tmpdir();
const cpp = fs.readFileSync(path.join(__dirname, '../Source/PluginEditor.cpp'), 'utf8'); const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX('); const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join(''); const html = fs.readFileSync(path.join(__dirname, '../Source/ui/public/index.html'), 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(OUT, 'tp49_chop_index.html'); fs.writeFileSync(PAGE, html);
let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };
(async () => { const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 }); await p.evaluateOnNewDocument(stubSrc + '\nstub(); window.__params.__bpm = 128;'); const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('mix-btn').click(); }); await sleep(1500);
  
  const st = await p.evaluate(() => { const q = s => document.querySelector(s); const vis = e => { if (!e) return false; const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && getComputedStyle(e).display !== 'none'; };
    return { chopOpen: document.body.classList.contains('chop-open'), btn: q('#mix-btn').textContent, arm: vis(q('#ti-arm')), lib: vis(q('#ti-lib')), seq: vis(q('#ti-seq-pill')), sync: vis(q('.ti-seq-sync')), play: vis(q('.ti-seq-play')), dice: vis(q('#hero .scope-controls-left')), lock: vis(q('#ti-bpm-lock')),
      words: [...document.querySelectorAll('#mix-panel .trigger-pill, .ti-play-pill, .ti-mode-pill, #mix-panel .mix-strip-knob-label')].map(e => e.textContent.trim()).slice(0, 12), pillBg: getComputedStyle(q('.ti-mode-pill.active')).backgroundColor, pillBorder: getComputedStyle(q('.ti-mode-pill.active')).borderTopColor, rootBg: getComputedStyle(q('#ti-root-picker')).backgroundColor, panelBg: getComputedStyle(q('#mix-panel')).backgroundColor }; });
  ok(st.btn === 'CHOP' && st.chopOpen, '[1] the header says CHOP and opening it marks the page (body.chop-open)', JSON.stringify(st));
  ok(st.arm && st.lib && !st.seq && !st.sync && !st.play && !st.dice && !st.lock, '[2] the Arm and the sample library are there; SEQ, SYNC, the play button, the dice and the BPM lock are gone', JSON.stringify(st));
  ok(st.words.indexOf('One-Shot') >= 0 && st.words.indexOf('Pitch') >= 0 && st.words.indexOf('Jitter') >= 0 && st.words.indexOf('1-SHOT') < 0 && st.words.indexOf('PAN') < 0, '[3] the words wear the house case (One-Shot, Pitch, Jitter)', st.words.join(','));
  ok(st.pillBg === 'rgba(0, 0, 0, 0)' && /183, 148, 255/.test(st.pillBorder) && st.rootBg === 'rgba(0, 0, 0, 0)' && st.panelBg === 'rgb(26, 26, 46)', '[4] a selected pill is a purple outline with nothing filled; the key box is transparent; the panel is the house tone', JSON.stringify({ pillBg: st.pillBg, pillBorder: st.pillBorder, rootBg: st.rootBg, panelBg: st.panelBg }));
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(700);
  const sw = await p.evaluate(() => ({ mixOpen: document.getElementById('mix-panel').classList.contains('open'), mixBtnActive: document.getElementById('mix-btn').classList.contains('active'), chopOpen: document.body.classList.contains('chop-open'), synOpen: !document.getElementById('syn-panel').classList.contains('hidden'), controls: document.getElementById('controls').style.display }));
  ok(!sw.mixOpen && !sw.mixBtnActive && !sw.chopOpen && sw.synOpen, '[5] SYN from the Chop page: the page closes and the button drops (no traces — Max: "it leaves traces of itself behind")', JSON.stringify(sw));
  await p.evaluate(() => { document.getElementById('mix-btn').click(); }); await sleep(600); await p.evaluate(() => { window.__tiChopArm(true); }); await sleep(200);
  const arm = await p.evaluate(() => ({ on: document.getElementById('ti-arm').classList.contains('on'), txt: document.getElementById('ti-arm').textContent.trim(), setCalls: window.__natCount['setTiArmed'] || 0 }));
  ok(arm.on && arm.txt === 'Armed' && arm.setCalls === 1, '[6] the Arm lights, reads Armed and writes setTiArmed', JSON.stringify(arm));
  const dots = await p.evaluate(() => { const d = document.querySelector('#mix-panel .layer-status-dot'); const cs = d ? getComputedStyle(d) : null; return cs ? { bg: cs.backgroundColor, bw: cs.borderTopWidth } : null; });
  ok(dots && dots.bg === 'rgba(0, 0, 0, 0)' && dots.bw !== '0px', '[7] the layer A-D status row is outlined, not filled', JSON.stringify(dots));
  const bf = await p.evaluate(() => ['#ti-root-picker', '.ti-bpm-display', '#ti-bottom-right-cluster'].map(q => { const e = document.querySelector(q); const cs = e ? getComputedStyle(e) : null; return cs ? (cs.backdropFilter || cs.webkitBackdropFilter || 'none') + '|' + cs.backgroundColor : 'missing'; }));
  ok(bf.every(x => /^none\|rgba\(0, 0, 0, 0\)$/.test(x)), '[8] the key box and BPM have no fill and no blur behind them (no box)', bf.join(' , '));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0); })();
