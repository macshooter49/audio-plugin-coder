// ══════════════════════════════════════════════════════════════════════════════════════════════
//  macros_still_gate.js — fb574: THE MACROS TOGGLE MOVES NOTHING, AND THE MARKS STAY ON THEIR WORDS.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/macros_still_gate.js [page.html]
//
//  Max (2026-09-03, with a screenshot — LFO 1 on WT Pos and on the Chorus Mix): "every time we press
//  macros, it does that padding and that resize again, which then, in turn, fucks up with our LFO
//  visualization... it moved the bottom of the effect. Nothing can move, and it doesn't mess up our
//  visualization whatsoever."
//
//  WHY (measured): the bottom row is a grid whose height is its content's; the voice column swapped
//  its two views with display:none, so the column — and the row, and every rack device at height:100%
//  — took the height of whichever view was showing. And the marks are painted on the frame clock,
//  which is asleep at idle (fb511c: zero frames at idle), so after the click they stayed where the
//  OLD layout had put them while the words moved under them.
//
//  THE BARS
//   0  THE TOGGLE — off → on → off through real clicks on the Macros button.
//   1  STILL — the bottom row, the rack and the first device keep one rect across the three states.
//   2  STILL WORDS — the WT Pos word and the Mix word keep one rect across the three states.
//   3  THE MARKS HOLD AT IDLE — after each click, with NO frame ticked (what the eye sees at idle),
//      both marks sit exactly where they sat: top = word box bottom − 1 + air (fb571's 4.5 px under the ink).
//   4  ONE BOX — the voice view and the macro view share one grid cell (same rect, both in flow); the
//      inactive one is visibility:hidden, never display:none; a routed MACRO's mark shows only while the
//      Macros view shows (visible marks 2 → 3 → 2).
//   5  MONO MOVES NOTHING EITHER — the other three buttons on that row leave the rack where it is.
//   6  THE PILLS GO WITH THEIR VIEW (fb575) — 40 ms after each click the Always pill's computed visibility is the
//      view's (hidden with Macros on, visible with it off), the hidden view is opacity 0, and the pill's transition
//      names colours only — never `all`/visibility (the real WebKit page never ended that transition: measured 3 s+).
//
//  PROOF THE BARS CAN FAIL:  MACROS_STILL_MUTATE=1 (showMacros writes display again)   → 1-4 red
//                            MACROS_STILL_MUTATE=2 (the painter ignores visibility)     → 4 red
//                            MACROS_STILL_MUTATE=3 (the hidden voice view back to display:none) → 1-4 red
//                            MACROS_STILL_MUTATE=4 (the pills back to `transition:.14s` = all)     → 6 red
// ══════════════════════════════════════════════════════════════════════════════════════════════

const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const STUB = () => { const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0);}),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}}; };
async function boot (P) {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });   /* the host's own page box: 820 wide, 656 inner (fb570's vis_diag on the installed AU) */
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  return { b, pg, errs };
}

const MUT = +(process.env.MACROS_STILL_MUTATE || 0);
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) sub("if(mbtn) mbtn.classList.toggle('on',on); if(window.__tiFrame) window.__tiFrame(); }catch(_){ } }", "if(mbtn) mbtn.classList.toggle('on',on); mv.style.display=on?'grid':'none'; vv.style.display=on?'none':'flex'; if(window.__tiFrame) window.__tiFrame(); }catch(_){ } }");
  if (MUT === 2) sub("if(getComputedStyle(lb).visibility==='hidden'){ u.style.display='none'; return; }", "if(false){ u.style.display='none'; return; }");
  if (MUT === 3) sub("#syn-panel .voice-meta.vm-macros-active .vm-voiceview{ visibility:hidden;", "#syn-panel .voice-meta.vm-macros-active .vm-voiceview{ display:none;");
  if (MUT === 4) sub("#syn-panel .ribbon-row.rr-new .voice-toggle{ transition:color .14s, border-color .14s, background-color .14s; }", "#syn-panel .ribbon-row.rr-new .voice-toggle{ transition:.14s; }");
  const p = path.join(os.tmpdir(), 'macros_still_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}

(async () => {
  const P = mutatedPage();
  const { b, pg, errs } = await boot(P);
  console.log('\n══ fb574 — THE MACROS TOGGLE MOVES NOTHING · THE MARKS STAY ON THEIR WORDS ══');
  console.log('   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  await pg.evaluate(() => { window.__fxAdd('cho'); window.__fx4Tick(); }); await sleep(500);
  const setup = await pg.evaluate(() => {
    const wtLab = [].slice.call(document.querySelectorAll('#syn-panel .knob-label')).filter(l => l.textContent.trim() === 'WT Pos')[0];
    const g = wtLab && window.__ctlDestAt(wtLab.closest('[data-syn]')); const wt = g ? g.dest : null;
    const mixCell = [].slice.call(document.querySelectorAll('#syn-panel .fxr-dev .fxr-knob[data-mod-dest]')).filter(c => { const l = c.querySelector('.fxr-lab'); return l && l.textContent.trim() === 'Mix'; })[0];
    const mix = mixCell ? +mixCell.getAttribute('data-mod-dest') : null;
    const mac = (window.__MACRO_DEST || 1878);
    if (wt != null) window.__tiAddRoute(0, 1, wt); if (mix != null) window.__tiAddRoute(0, 1, mix); window.__tiAddRoute(0, 1, mac);
    return { wt, mix, mac };
  });
  console.log('   routes (LFO 1 →): ' + JSON.stringify(setup));
  const tick = () => pg.evaluate(() => { for (let k = 0; k < 3; k++) window.__ulTick(); });
  const MEAS = () => pg.evaluate(() => {
    const R = el => { if (!el) return null; const r = el.getBoundingClientRect(); return { l:+r.left.toFixed(2), t:+r.top.toFixed(2), r:+r.right.toFixed(2), b:+r.bottom.toFixed(2), w:+r.width.toFixed(2), h:+r.height.toFixed(2) }; };
    const W = lab => { if (!lab) return null; const rg = document.createRange(); rg.selectNodeContents(lab); return R({ getBoundingClientRect: () => rg.getBoundingClientRect() }); };
    const vis = [].slice.call(document.querySelectorAll('.sm-ul')).filter(u => u.style.display !== 'none');
    const near = w => { if (!w) return null; let best = null; vis.forEach(u => { const r = u.getBoundingClientRect(); if (Math.abs(r.left - w.l) < 1.5 && r.top >= w.b - 3 && r.top <= w.b + 12) best = R(u); }); return best; };
    const sp = document.getElementById('syn-panel'), vm = document.querySelector('#syn-panel .voice-meta'), vv = document.querySelector('#syn-panel .vm-voiceview'), mv = document.getElementById('vm-macroview');
    const wtLab = [].slice.call(document.querySelectorAll('#syn-panel .knob-label')).filter(l => l.textContent.trim() === 'WT Pos')[0];
    const mixLab = [].slice.call(document.querySelectorAll('#syn-panel .fxr-dev .fxr-lab')).filter(l => l.textContent.trim() === 'Mix')[0];
    const macLab = document.querySelector('#syn-panel .vm-ml');
    const cs = e => e ? getComputedStyle(e) : {};
    return { on: vm.classList.contains('vm-macros-active'), row: R(document.querySelector('#syn-panel .ribbon-row.rr-new')), rack: R(document.getElementById('fxr-rack')), dev: R(document.querySelector('#syn-panel .fxr-dev')),
      vm: R(vm), vv: R(vv), mv: R(mv), panel: R(sp), docH: document.documentElement.scrollHeight,
      vis: { vv: cs(vv).visibility, mv: cs(mv).visibility, vvD: cs(vv).display, mvD: cs(mv).display },
      words: { wt: W(wtLab), mix: W(mixLab), mac: W(macLab) }, air: { wt: window.__markAir ? window.__markAir(wtLab) : null, mix: window.__markAir ? window.__markAir(mixLab) : null },
      marks: { wt: near(W(wtLab)), mix: near(W(mixLab)), mac: near(W(macLab)) }, visMarks: vis.length };
  });
  await tick(); const M0 = await MEAS();
  const PILL = () => pg.evaluate(() => { const vv = document.querySelector('#syn-panel .vm-voiceview'), p = [].slice.call(document.querySelectorAll('#syn-panel .vm-voiceview .voice-toggle')).filter(b => b.textContent.trim() === 'Always')[0];
    const cs = getComputedStyle(p); return { vis: cs.visibility, viewOpacity: getComputedStyle(vv).opacity, transition: cs.transition }; });
  await pg.click('#syn-panel .vm-macros-btn'); await sleep(40); const P1 = await PILL(); await sleep(310); const M1s = await MEAS(); await tick(); const M1 = await MEAS();
  await pg.click('#syn-panel .vm-macros-btn'); await sleep(40); const P2 = await PILL(); await sleep(310); const M2s = await MEAS(); await tick(); const M2 = await MEAS();
  await pg.click('#syn-panel .vm-4 [data-syn-toggle="SYN_MONO"]'); await sleep(300); const MM = await MEAS(); await pg.click('#syn-panel .vm-4 [data-syn-toggle="SYN_MONO"]'); await sleep(200);
  const same = (a, b, tol = 0.02) => !!a && !!b && ['l', 't', 'r', 'b'].every(k => Math.abs(a[k] - b[k]) <= tol);
  const fb = r => r ? r.b.toFixed(2) : 'null', ft = r => r ? r.t.toFixed(2) : 'null';
  chk(M0.on === false && M1.on === true && M2.on === false, '0  THE TOGGLE: off → on → off through real clicks on the Macros button', `${M0.on} → ${M1.on} → ${M2.on}`);
  chk(same(M0.row, M1.row) && same(M0.row, M2.row) && same(M0.rack, M1.rack) && same(M0.rack, M2.rack) && same(M0.dev, M1.dev) && same(M0.dev, M2.dev),
      '1  STILL: the bottom row, the rack and the first device keep one rect across off → on → off', `row bottom ${fb(M0.row)} → ${fb(M1.row)} → ${fb(M2.row)} · device bottom ${fb(M0.dev)} → ${fb(M1.dev)} → ${fb(M2.dev)}`);
  chk(same(M0.words.wt, M1.words.wt) && same(M0.words.wt, M2.words.wt) && same(M0.words.mix, M1.words.mix) && same(M0.words.mix, M2.words.mix),
      '2  STILL WORDS: the WT Pos word and the Mix word keep one rect across the three states', `Mix word bottom ${fb(M0.words.mix)} → ${fb(M1.words.mix)} → ${fb(M2.words.mix)} · WT Pos ${fb(M0.words.wt)} → ${fb(M1.words.wt)} → ${fb(M2.words.wt)}`);
  const held = S => !!S.marks.wt && !!S.marks.mix && same(S.marks.wt, M0.marks.wt) && same(S.marks.mix, M0.marks.mix);
  const airOK = S => !!S.marks.wt && !!S.marks.mix && Math.abs(S.marks.wt.t - (S.words.wt.b - 1 + S.air.wt)) < 0.06 && Math.abs(S.marks.mix.t - (S.words.mix.b - 1 + S.air.mix)) < 0.06;
  chk(airOK(M0) && held(M1s) && airOK(M1s) && held(M2s) && airOK(M2s),
      '3  THE MARKS HOLD AT IDLE: after each click, with no frame ticked, both marks sit where they sat — 4.5 px under the ink of an unmoved word',
      `Mix mark top ${ft(M0.marks.mix)} → ${ft(M1s.marks.mix)} (stale) → ${ft(M2s.marks.mix)} (stale) · Mix word bottom ${fb(M0.words.mix)} → ${fb(M1s.words.mix)} → ${fb(M2s.words.mix)} · air ${M0.air.mix}/${M0.air.wt}`);
  chk(M0.vis.vv === 'visible' && M0.vis.mv === 'hidden' && M0.vis.mvD !== 'none' && M1.vis.mv === 'visible' && M1.vis.vv === 'hidden' && M1.vis.vvD !== 'none' && same(M0.vv, M0.mv) && same(M1.vv, M1.mv) && M0.visMarks === 2 && M1.visMarks === 3 && M2.visMarks === 2,
      '4  ONE BOX: both views share one cell (same rect, both in flow), the inactive one is visibility:hidden; the routed macro\'s mark shows only with the Macros view (2 → 3 → 2)',
      `off: vv ${M0.vis.vvD}/${M0.vis.vv} mv ${M0.vis.mvD}/${M0.vis.mv} · on: vv ${M1.vis.vvD}/${M1.vis.vv} mv ${M1.vis.mvD}/${M1.vis.mv} · view rects off ${JSON.stringify(M0.vv)} vs ${JSON.stringify(M0.mv)} · marks ${M0.visMarks} → ${M1.visMarks} → ${M2.visMarks}`);
  chk(same(M0.row, MM.row) && same(M0.dev, MM.dev), '5  MONO MOVES NOTHING EITHER', `row bottom ${fb(M0.row)} → ${fb(MM.row)} · device bottom ${fb(M0.dev)} → ${fb(MM.dev)}`);
  chk(P1.vis === 'hidden' && P1.viewOpacity === '0' && P2.vis === 'visible' && P2.viewOpacity === '1' && !/\ball\b|visibility/.test(P1.transition),
      '6  THE PILLS GO WITH THEIR VIEW: 40 ms after each click the Always pill is the view\'s visibility, the hidden view is opacity 0, the pill transitions colours only',
      `on: ${P1.vis}/opacity ${P1.viewOpacity} · off: ${P2.vis}/opacity ${P2.viewOpacity} · transition "${P1.transition}"`);
  console.log(`   record: row h ${M0.row.h} (macros on ${M1.row.h}) · panel bottom ${M0.panel.b} of 656 · doc ${M0.docH} · voice view h ${M0.vv ? M0.vv.h : '-'} · macro view h ${M1.mv ? M1.mv.h : '-'} (in the off state ${M0.mv ? M0.mv.h : '-'})`);
  if (errs.length) console.log('   page errors: ' + errs.join(' | '));
  console.log(`\n══ RESULT: ${pass} pass, ${fail} FAIL ══`);
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
