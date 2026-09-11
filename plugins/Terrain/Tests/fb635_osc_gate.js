// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb635_osc_gate.js — fb635: THE BACK OSCILLATORS KEEP THEIR ONE-SHOT PICTURE.
//
//    node Tests/fb635_osc_gate.js             # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//
//  Max: "oscillators B and D are on the back side of A and C. A and C load up their one shots correctly,
//  but B and D seem to forget them in terms of the UI. Not the DSP, but the UI ... whenever I switch to B
//  and D, they're gone and there's no way for me to get them back ... make it to where they're always
//  healing."
//
//  THE MECHANISM (fb590's trap, on the slot letter). A preset load pushes all four one-shot payloads; B and
//  D are display:none behind A and C, so setLoaded() marks their views .loaded while drawPeaks() returns on
//  the zero-size box, and the reopen retry skips anything .loaded. The slot letter only toggled osc-hidden —
//  nothing ever painted the peaks B and D were holding.
//
//  THE BARS (engines set to Sample through the parameter, payloads pushed through the C++'s own door,
//  window.onOscSampleLoaded, while B and D are hidden — exactly a preset load with the fronts showing)
//   1  THE LETTER FLIP PAINTS B AND D — click A's letter, then C's: the revealed B and D canvases carry ink
//   2  ANY REVEAL PAINTS — a back oscillator revealed WITHOUT the letter (its class removed, as any future
//      path might) paints within a frame: the canvas heals itself, not the button
//   3  THE FRONTS STILL PAINT — flipping back to A and C: both inked
//
//  MUTATION CONTROLS
//    OG_MUT=noheal   both fb635 heals removed (the shipped fb634 page)   → [1] [2] RED
//    OG_MUT=noro     only the canvas observer removed                     → [2] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE0 = path.join(ROOT, 'Source/ui/public/index.html');
const MUT = process.env.OG_MUT || '';
let pass = 0, fail = 0;
const chk = (ok, l, d) => { ok ? pass++ : fail++; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${l}\n        ${d}`); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
function page () {
  if (! MUT) return PAGE0;
  let s = fs.readFileSync(PAGE0, 'utf8');
  const cut = (a, b) => { const i = s.indexOf(a), j = s.indexOf(b, i); if (i < 0 || j < 0 || s.indexOf(a, i + 1) >= 0) { console.log('  MUTATION anchor not unique: ' + a.slice(0, 60)); process.exit(2); } s = s.slice(0, i) + s.slice(j); };
  cut("        /* fb635 — THE PICTURE HEALS ITSELF WHEN IT BECOMES VISIBLE.", "        SAMP[osc] = { st: st, data: data,");
  if (MUT === 'noheal') cut("            /* fb635 — a revealed back oscillator heals its face NOW", "          });\n        });\n      });\n    } catch (eOscToggle)");
  const f = path.join(os.tmpdir(), 'fb635_osc_' + MUT + '.html'); fs.writeFileSync(f, s); return f;
}
const WARP_N = (() => {   // the warp lane's cardinality, read from the C++ (all_menus.js's own extraction)
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s);
  if (! m) throw new Error('warp cardinality not found in PluginProcessor.cpp');
  return +m[1];
})();
const STUB = (cfg) => {
  window.__emits = []; window.__natives = [];
  const CH = cfg.choice; const states = new Map();
  const mk = (name) => { const n = CH[name] || 0;
    const props = n ? { start:0, end:n-1, skew:1, name, label:'', numSteps:n, interval:1, parameterIndex:states.size }
                    : { start:0, end:1, skew:1, name, label:'', numSteps:100, interval:0, parameterIndex:states.size };
    const st = { get scaledValue(){ return st.norm; },   // the front page reads .scaledValue as a PROPERTY (state.outputGain) — without it init() throws
      name, norm:(name === 'SYN_BEND_RANGE' ? 2/24 : 0), properties:props,   // the bend range's registered default (2 st) — the only non-zero default the bars read back
      getScaledValue(){ return n ? Math.round(st.norm*(n-1)) : st.norm; },
      setScaledValue(v){ st.norm = n ? v/(n-1) : v; },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = n ? Math.round(v*(n-1))/(n-1) : v;
        window.__emits.push({ name, norm:+(+v).toFixed(6) }); (st.__ls||[]).forEach(f => { try { f(); } catch(e){} }); },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex(){ return Math.round(st.norm*(n-1)); }, setChoiceIndex(i){ st.norm = i/(n-1); },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st; };
  const get = (name) => { if (! states.has(name)) states.set(name, mk(name)); return states.get(name); };
  window.__stubState = get;
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (/getPresets/i.test(nm)) return r('[]'); if (/getSynthMod$/.test(nm)) return r('[]');
    if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue()));   // the relay-free read-back, from the same stub state the writes land in
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  window.__errStacks = []; window.addEventListener('error', (e) => { try { window.__errStacks.push(String((e.error && e.error.stack) || e.message).slice(0, 400)); } catch(x){} });
};


(async () => {
  console.log('══ fb635 OSC GATE — the back oscillators keep their one-shot picture ══   mutation: ' + (MUT || '(none)'));
  const P = page();
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  const choice = {}; ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; choice['SYN_OSC_' + o + '_SAMPLE_LOOP_MODE'] = 5; });
  await pg.evaluateOnNewDocument(STUB, { choice });
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 }); await sleep(2400);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch (e) {} dispatchEvent(new Event('resize')); });
  await sleep(900);
  // a preset load, as the C++ does it: all four engines to Sample, all four payloads pushed, fronts showing
  const st0 = await pg.evaluate(async () => {
    ['A','B','C','D'].forEach(o => window.__stubState('SYN_OSC_' + o + '_ENGINE').setNormalisedValue(1 / 6));
    await new Promise(r => setTimeout(r, 400));
    const N = 400, mn = [], mx = []; for (let i = 0; i < N; i++) { const e = Math.exp(-i / 90) * (0.55 + 0.4 * Math.sin(i * 0.37)); mn.push(-Math.abs(e)); mx.push(Math.abs(e)); }
    ['a','b','c','d'].forEach(o => window.onOscSampleLoaded(o, { peaksMin: mn, peaksMax: mx, filename: 'Vbo - rhodes ' + o + '.wav', numSamples: 96000, sampleRate: 48000 }));
    await new Promise(r => setTimeout(r, 300));
    const dev = o => document.getElementById('osc-' + o + '-device');
    return ['a','b','c','d'].map(o => o + ':' + (dev(o).classList.contains('osc-hidden') ? 'hidden' : 'shown') + (dev(o).classList.contains('engine-sample') ? '/sample' : '/' + dev(o).className.replace(/.*engine-(\w+).*/, '$1')));
  });
  const inked = o => pg.evaluate((o) => { const cv = document.querySelector('.sample-view[data-osc="' + o + '"] canvas'); if (! cv) return 'no-canvas';
    const r = cv.getBoundingClientRect(); if (r.width < 3) return 'no-box'; return window.__tiCvInked(cv) ? 'inked' : 'BLANK'; }, o);
  const letter = o => pg.evaluate((o) => { const l = document.querySelector('#osc-' + o + '-device .osc-letter'); if (! l) return false; l.click(); return true; }, o);
  console.log('  after the load: ' + st0.join(' · ') + ' · A ' + await inked('a') + ' · C ' + await inked('c'));

  // [1] the letter flip
  const fl = [await letter('a'), await letter('c')]; await sleep(250);
  const b1 = await inked('b'), d1 = await inked('d');
  chk(fl[0] && fl[1] && b1 === 'inked' && d1 === 'inked', '[1] THE LETTER FLIP PAINTS B AND D — the back oscillators show the one-shot they were holding',
    `letters clicked ${fl.join('/')} · B ${b1} · D ${d1}`);
  // [3] back to the fronts
  await letter('b'); await letter('d'); await sleep(250);
  const a3 = await inked('a'), c3 = await inked('c');
  // [2] a reveal without the letter: clear B and D's canvases while hidden (a purged backing store), then reveal by class
  const b2 = await pg.evaluate(async () => {
    ['b','d'].forEach(o => { const cv = document.querySelector('.sample-view[data-osc="' + o + '"] canvas'); cv.width = 300; cv.height = 150; });
    ['a','b'].forEach(o => document.getElementById('osc-' + o + '-device').classList.toggle('osc-hidden'));
    ['c','d'].forEach(o => document.getElementById('osc-' + o + '-device').classList.toggle('osc-hidden'));
    await new Promise(r => setTimeout(r, 300));
    const one = o => { const cv = document.querySelector('.sample-view[data-osc="' + o + '"] canvas'); const r = cv.getBoundingClientRect(); return r.width < 3 ? 'no-box' : (window.__tiCvInked(cv) ? 'inked' : 'BLANK'); };
    return { b: one('b'), d: one('d') }; });
  chk(b2.b === 'inked' && b2.d === 'inked', '[2] ANY REVEAL PAINTS — B and D revealed by their class alone (no letter) repaint from the peaks they hold',
    `B ${b2.b} · D ${b2.d}`);
  chk(a3 === 'inked' && c3 === 'inked', '[3] THE FRONTS STILL PAINT — flipping back, A and C carry their pictures', `A ${a3} · C ${c3}`);
  if (errs.length) console.log('  page errors: ' + errs.slice(0, 3).join(' | '));
  console.log(`\n  ${pass} passed, ${fail} FAILED`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
