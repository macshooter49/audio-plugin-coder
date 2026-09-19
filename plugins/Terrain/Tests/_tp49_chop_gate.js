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
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 }); await p.evaluateOnNewDocument(stubSrc + `\nstub();
    window.__loadPath = []; window.__editLayer = 0;
    (function(){ const g = window.Juce.getNativeFunction; window.Juce.getNativeFunction = function(n){
      if (n === 'loadSampleFromPath') return (pth) => { window.__loadPath.push(window.__editLayer + ':' + String(pth).split('/').pop()); return Promise.resolve(0); };
      if (n === 'setEditingLayer') return (i) => { window.__editLayer = +i; return Promise.resolve(0); };
      if (n === 'getLayerHasSample') return (i) => Promise.resolve(window.__loadPath.some(x => x.indexOf((i == null ? window.__editLayer : i) + ':') === 0) ? 1 : 0);
      return g(n); }; })();`); const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('mix-btn').click(); }); await sleep(1500);
  
  const st = await p.evaluate(() => { const q = s => document.querySelector(s); const vis = e => { if (!e) return false; const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && getComputedStyle(e).display !== 'none'; };
    return { chopOpen: document.body.classList.contains('chop-open'), btn: q('#mix-btn').textContent, arm: vis(q('#ti-arm')), lib: vis(q('#ti-lib')), seq: vis(q('#ti-seq-pill')), sync: vis(q('.ti-seq-sync')), play: vis(q('.ti-seq-play')), dice: vis(q('#hero .scope-controls-left')), lock: vis(q('#ti-bpm-lock')),
      words: [...document.querySelectorAll('#mix-panel .trigger-pill, .ti-play-pill, .ti-mode-pill, #mix-panel .mix-strip-knob-label')].map(e => e.textContent.trim()).slice(0, 12), pillBg: getComputedStyle(q('.ti-mode-pill.active')).backgroundColor, pillBorder: getComputedStyle(q('.ti-mode-pill.active')).borderTopColor, rootBg: getComputedStyle(q('#ti-root-picker')).backgroundColor, panelBg: getComputedStyle(q('#mix-panel')).backgroundColor,
      ground: ['#plugin', '#header', '#footer', '#mix-panel', '#hero'].map(s2 => getComputedStyle(q(s2)).backgroundColor) }; });
  ok(st.btn === 'CHOP' && st.chopOpen, '[1] the header says CHOP and opening it marks the page (body.chop-open)', JSON.stringify(st));
  ok(st.arm && st.lib && !st.seq && !st.sync && !st.play && !st.dice && !st.lock, '[2] the Arm and the sample library are there; SEQ, SYNC, the play button, the dice and the BPM lock are gone', JSON.stringify(st));
  /* tp55 — "Jitter" is not a word on this page any more: the knob became a real VIBRATO (depth +
     rate), because a one-shot random detune did nothing audible on a lone one-shot. The bar still
     asks the same question — sentence case, not the old shouting — of the words that are there. */
  ok(st.words.indexOf('One-Shot') >= 0 && st.words.indexOf('Pitch') >= 0 && st.words.indexOf('Vib') >= 0 && st.words.indexOf('Rate') >= 0 && st.words.indexOf('Jitter') < 0 && st.words.indexOf('1-SHOT') < 0 && st.words.indexOf('PAN') < 0, '[3] the words wear the house case (One-Shot, Pitch, Vib, Rate — and Jitter is gone)', st.words.join(','));
  ok(st.pillBg === 'rgba(0, 0, 0, 0)' && /183, 148, 255/.test(st.pillBorder) && st.rootBg === 'rgba(0, 0, 0, 0)', '[4] a selected pill is a purple outline with nothing filled, and the key box is transparent', JSON.stringify({ pillBg: st.pillBg, pillBorder: st.pillBorder, rootBg: st.rootBg }));
  // tp52 — ONE GROUND: #plugin carries the house tone and the header, the footer, the waveform and the panel all wear it (Max: "header & FOOTER ARE NOT the same color")
  ok(st.ground[0] === 'rgb(26, 26, 46)' && st.ground.slice(1).every(c => c === 'rgba(0, 0, 0, 0)'), '[4] header, footer, waveform and panel all wear ONE ground (#1A1A2E on #plugin, nothing painted over it)', JSON.stringify(st.ground));
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
  // [9] tp50 — the sample library is PER LAYER (Max: "add a one-shot to A, a different one to B, another to C and D, and it doesn't affect the other ABCD")
  const pad = (L) => p.evaluate(L => { const el = document.querySelector('#ti-layer-pads .ti-layer-pad[data-layer-idx="' + L + '"]'); el.dispatchEvent(new MouseEvent('click', { bubbles: true })); }, L);
  const step = async (n) => { for (let i = 0; i < n; i++) { await p.evaluate(() => window.__tiLibStep(1)); await sleep(140); } };
  const fake = () => p.evaluate(() => { const h = document.getElementById('hero'); h.classList.add('has-sample'); h.classList.remove('empty-state'); });
  await fake(); await step(1); await pad(1); await sleep(300); await fake(); await step(3); await pad(2); await sleep(300); await fake(); await step(2); await sleep(250);
  const lay = await p.evaluate(() => window.__tiLib()); const loads = await p.evaluate(() => window.__loadPath);
  const names = lay.names;
  ok(names[0] && names[1] && names[2] && !names[3] && names[0] !== names[1] && names[1] !== names[2] && lay.idx[0] === 0 && lay.idx[1] === 2 && lay.idx[2] === 1,
     '[9] each layer keeps its OWN place in the library and its own sample name (A, B and C differ; D untouched)', JSON.stringify(lay));
  ok(loads.length > 0 && loads.every(x => /^[0-3]:/.test(x)) && loads.filter(x => x[0] === '1').length === 3 && loads.filter(x => x[0] === '2').length === 2 && loads.filter(x => x[0] === '3').length === 0,
     '[9] every load landed on the layer that was selected (three into B, two into C, none into D)', loads.join(' , '));
  await pad(0); await sleep(400);
  const back = await p.evaluate(() => window.__tiLib());
  ok(back.layer === 0 && back.label === names[0] && back.names.join('|') === names.join('|'),
     '[10] coming back to a layer shows ITS sample again — no name is wiped by the switch', JSON.stringify(back));
  // tp52 — the glass boxes are gone, the sliders are the house notch (white, 2 px), M/S are small, ARM sits with the pads
  const skin = await p.evaluate(() => { const g = s => { const e = document.querySelector(s); if (!e) return null; const c = getComputedStyle(e); const r = e.getBoundingClientRect(); return { bg: c.backgroundColor, bw: c.borderTopWidth, w: Math.round(r.width), h: Math.round(r.height) }; };
    return { strip: g('.mix-strip'), trig: g('#mix-trigger-area'), morph: g('.morph-track'), fill: g('.morph-fill'), fader: g('.mix-strip-fader'), ffill: g('.mix-strip-fader-fill'), ms: g('.mix-strip-btn'), pad: g('.ti-layer-pad'),
      armIn: (document.getElementById('ti-arm') || {}).parentElement ? document.getElementById('ti-arm').parentElement.id : null, stem: g('.stem-header'), libW: g('#ti-lib-name') }; });
  ok(skin.strip.bg === 'rgba(0, 0, 0, 0)' && skin.strip.bw === '0px' && skin.trig.bg === 'rgba(0, 0, 0, 0)' && skin.trig.bw === '0px',
     '[9] no glass boxes: the mixer strips and the two right-hand tiles are the page itself', JSON.stringify({ strip: skin.strip, trig: skin.trig }));
  ok(skin.morph.h === 2 && skin.fader.w === 2 && skin.fill.bg === 'rgb(255, 255, 255)' && skin.ffill.bg === 'rgb(255, 255, 255)',
     '[9] the sliders are the house notch — 2 px and WHITE, the Layer morph included', JSON.stringify({ morph: skin.morph, fader: skin.fader, fill: skin.fill.bg, ffill: skin.ffill.bg }));
  ok(skin.pad.w === 16 && skin.pad.h === 16 && skin.ms.w <= 16 && skin.ms.h <= 14 && skin.armIn === 'ti-layer-pads' && (!skin.stem || skin.stem.w === 0),
     '[9] A-D wear the synth page\'s chip, M/S are small, ARM sits beside D and the STEMS word is gone', JSON.stringify({ pad: skin.pad, ms: skin.ms, armIn: skin.armIn, stem: skin.stem }));
  // ⚠️ tp54 NARROWED THE BOX 124 -> 96, and the reason is the thing this bar was written to prevent: it
  //    never actually happened. MEASURED on the real plugin, the Slices pill ran 507..567 while #ti-lib
  //    starts at 550 — a 17 px overlap with the library's left arrow underneath it. A fixed name box stops
  //    a long NAME from growing into its neighbours; it cannot stop the centred pill group from growing
  //    into the box. The 28 px this gives back is what buys the clearance (_tp54_chop_pass_gate [2]).
  ok(skin.libW.w === 96, '[9] the sample library name is a fixed box, so a long name can never reach Slices or the BPM (96 px since tp54 — the pill group needed the room)', JSON.stringify(skin.libW));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0); })();
