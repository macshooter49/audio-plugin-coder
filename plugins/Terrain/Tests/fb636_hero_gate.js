// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb636_hero_gate.js — THE HERO'S LOOPS ARE KILLED, AND THE EDITOR STOPS SPENDING FOR NOTHING.
//
//    node Tests/fb636_hero_gate.js                 HG_MUT=<name>  (every control below must go RED)
//
//  Max: "kill all animation loops on the hero. The hero is not the synth page, but the page behind
//  the synth page... we're getting ready to replace that page with the Terrain patcher."
//
//  The front page is a STILL page now: its canvases are painted once whenever a picture is owed (the
//  page shown, a resize, a theme change, a heal, a hand on a control) and then nothing moves, no
//  painter works per frame and nothing polls for it — while the synth plays and the C++ ships 60
//  frames a second. Around it, the editor stops paying for things nobody reads.
//
//  THE HOST, SIMULATED ON THE MAC PATH: the C++ keepalive stamps the lane every 50 ms (__tiAlive — the
//  page's pushless fallback stands down, fb581) and __tiQuiet is NEVER written (it exists only inside
//  the Windows arbiter). "Playing" = __tiFrame() every 16 ms with __notesActive stamped, exactly what
//  the editor timer does. Natives are counted by name at the stub.
//
//  THE BARS
//   0  the page runs: no page errors on the front page, a popped card window, or the overlay page
//   1  THE FRONT IS STILL WHILE THE SYNTH PLAYS — ~120 shipped frames: 0 terrain renders, 0 canvas ops on
//      #hero / #controls / #footer, the (simulated) meters do not move
//   2  A STILL PICTURE IS NEVER BLANK, AND IT IS ALWAYS THE SAME PICTURE — a resize repaints at once, a
//      wiped hero heals (fb577), a theme change repaints in the new colours and back — identical pixels
//   3  A HAND KEEPS PAINTING (fb591) — a drag repaints the front every gesture frame, and stops with it
//   4  XY AUTO-PLAY KEEPS ITS CLOCK — it WRITES AUDIO (setXYPad): on, it steps on every frame as before;
//      off, it writes nothing and draws nothing
//   5  THE UNDO GLYPH IS CHANGE-GATED — 60 identical tape-loop frames redraw it at most once; a change once
//   6  THE MAC IDLE POLLS REST — at rest 0 getModDrag / getSynthMod / FMIX reads in 3 s; playing, they
//      run at full rate; a floating card brings the drag receiver back AT REST (its drag is elsewhere)
//   7  A POPPED CARD'S DRAG RECEIVER POLLS AT THE PACE OF ITS NEED — slow at rest, every frame mid-drag
//   8  THE HERO SAMPLER'S POLLS EXIST ONLY WHILE THE FRONT SHOWS (the real PluginEditor.cpp overlay,
//      injected as the editor does) — on the synth page 0 natives and no scan-viz rAF; on the front the
//      empty-state prompt polls, glow/scan poll only with a sample, and resume on return; the Mix
//      panel's layer dots poll only while it is open
//   9  THE C++ FRAME — no hero scope string, no capture segment, the dormant harmonograph feed is a
//      comment, the frame preallocates, the 4 Hz mod-state save compares, the popped curve card rests
//      — and the processor's scope RING is untouched (it is the audibility behind uiQuiet)
//
//  PROOF THE BARS CAN FAIL (Tests/fb636_gates.sh runs each one and requires RED):
//     HG_MUT=animate  (the per-frame hero is back)            → 1 red
//     HG_MUT=resize   (no direct repaint after a resize)     → 2 red
//     HG_MUT=theme    (no themechange repaint)               → 2 red
//     HG_MUT=xy       (XY auto-play behind the still latch)  → 4 red
//     HG_MUT=undo     (the undo glyph redrawn every frame)   → 5 red
//     HG_MUT=rest     (the rest flag never rests)            → 6 red
//     HG_MUT=popped   (the drag receiver polls with no card) → 6 red
//     HG_MUT=card     (the card receiver back on every rAF)  → 7 red
//     HG_MUT=overlay  (the sampler polls on every page)      → 8 red
//     HG_MUT=glowrest (the glow/scan polls run at rest)      → 8 red
//     HG_MUT=scan     (the scan-viz rAF re-arms forever)     → 8 red
//     HG_MUT=dots     (the layer dots poll with the Mix panel shut) → 8 red
//     HG_MUT=cpp      (the hero scope string ships again)    → 9 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = path.join(ROOT, 'Source/ui/public/index.html');
const CPP  = path.join(ROOT, 'Source/PluginEditor.cpp');
const PROC = path.join(ROOT, 'Source/PluginProcessor.cpp');
const MUT  = process.env.HG_MUT || '';
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ✓ ' + l + (d ? '\n        ' + d : '')); } else { fail++; console.log('  ✗ ' + l + (d ? '\n        ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

// ── the sources, the mutations, the pages ─────────────────────────────────────────────────────
let page = fs.readFileSync(PAGE, 'utf8'), cpp = fs.readFileSync(CPP, 'utf8');
function sub (src, from, to) {
  const n = src.split(from).length - 1;
  if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + from.slice(0, 100)); process.exit(2); }
  console.log('   mutation ' + MUT + ' applied'); return src.replace(from, to);
}
function extractOverlay (c) {   /* the editor splices this string in before </body> (PluginEditor.cpp: html.replace ("</body>", heroOverlay + "</body>")) */
  const a = c.indexOf('const juce::String heroOverlay = '), b = c.indexOf('html = html.replace ("</body>", heroOverlay', a);
  if (a < 0 || b < 0) { console.error('the heroOverlay string was not found in PluginEditor.cpp'); process.exit(2); }
  let out = '', m; const re = /R"TIHX\(([\s\S]*?)\)TIHX"/g, seg = c.slice(a, b);
  while ((m = re.exec(seg)) !== null) out += m[1];
  return out;
}
let overlay = extractOverlay(cpp);
if (MUT === 'animate') page = sub(page, "  if (window.__heroDrawn && ! (window.__tiGestureLive && window.__tiGestureLive())) return;   /* fb636 — still */", '  /* mutated */');
if (MUT === 'resize')  page = sub(page, "  if (!currentActivePanel && !window.__cardOnly) { try { renderTerrain(HERO_STILL_T); } catch (e) {} }", '');
if (MUT === 'theme')   page = sub(page, "window.addEventListener('themechange', () => {\n  window.__heroDrawn = false;", "window.addEventListener('themechange-mutated', () => {\n  window.__heroDrawn = false;");
if (MUT === 'xy')      page = sub(page, '  if (xyLive || animate.__xyWas) updateXYAuto(time);', '');
if (MUT === 'undo')    page = sub(page, '  if (k === updateUndoButton.__k) return;', '');
if (MUT === 'rest')    page = sub(page, '  window.__tiRest = function(){', '  window.__tiRest = function(){ return false;');
if (MUT === 'popped')  page = sub(page, 'if(!anyPc){ if(window.__tiModIn!==undefined) window.__tiModIn=undefined; clear(); return; }', '');
if (MUT === 'card')    page = sub(page, '    if(marked||Date.now()-lastMove<1000||!(window.__tiRest&&window.__tiRest())) requestAnimationFrame(tick);\n    else setTimeout(function(){ requestAnimationFrame(tick); },50); }', '    requestAnimationFrame(tick); }');
if (MUT === 'overlay') overlay = sub(overlay, "    if (window.__cardOnly || window.__uiParked) return false;   // fb636 (M3) — a closed editor shows nothing\n    try { return (typeof currentActivePanel === 'undefined') || ! currentActivePanel; } catch (_) { return true; }", '    return true;');
if (MUT === 'glowrest') overlay = sub(overlay, 'function tiGlowNeed () { return tiHeroHasSample() && tiAwake(); }', 'function tiGlowNeed () { return tiHeroHasSample(); }');
if (MUT === 'scan')    overlay = sub(overlay, '    _scanRafId = (tiFrontShown() && tiScanBusy()) ? requestAnimationFrame(tickScanViz) : null;', '    _scanRafId = requestAnimationFrame(tickScanViz);');
if (MUT === 'dots')    overlay = sub(overlay, "      var panel = document.getElementById('mix-panel');\n      if (! panel || ! panel.classList.contains('open')) return;\n      // Light up dots", '      // Light up dots');
if (MUT === 'cpp')     cpp = cpp + '\n        js << "try{if(window.updateVisualization){" << "window.updateVisualization(" << grainCount << "," << scopeData << ");}}catch(e){}";\n';
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'fb636_hero_'));
const PLAIN = path.join(tmp, 'index.html');   /* the page lives beside nothing it loads relatively — everything in it is inline */
const WITH  = path.join(tmp, 'index_overlay.html');
fs.writeFileSync(PLAIN, page);
fs.writeFileSync(WITH, page.split('</body>').join(overlay + '</body>'));   /* juce::String::replace replaces every occurrence — so does this */

// ── the stub host ─────────────────────────────────────────────────────────────────────────────
const STUB = () => {
  window.__nc = {}; window.__ncArg = {}; window.__fmix = 0;
  window.__stub = { has: 0, md: '{"l":0,"p":2,"c":0,"s":0,"lx":0,"ly":0,"in":0}' };
  const states = {};
  const mk = (id) => states[id] || (states[id] = (function () {
    const L = []; let v = null;
    /* scaledValue too: init() reads it (updateOutputDisplay → formatOutput); without it init() throws before it
       registers the front painter, and every bar below would be measuring a page that never painted */
    return { get scaledValue () { return v != null ? v : 0.5; },
      getScaledValue: () => (v != null ? v : 0.5), getNormalisedValue: () => (v != null ? v : 0.5),
      setScaledValue (x) { v = x; L.slice().forEach(f => { try { f(); } catch (e) {} }); },
      setNormalisedValue (x) { v = x; L.slice().forEach(f => { try { f(); } catch (e) {} }); },
      getChoiceIndex: () => 0, setChoiceIndex () {}, getValue: () => false, setValue () {}, sliderDragStarted () {}, sliderDragEnded () {},
      valueChangedEvent: { addListener (f) { L.push(f); return { remove () {} }; }, removeListener () {} },
      propertiesChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} },
      properties: { start: 0, end: 1, interval: 0, name: '', label: '', numSteps: 100, choices: [], parameterIndex: 0 } }; })());
  const ans = (n, a) => {
    if (n === 'getLayerHasSample') return !! window.__stub.has;
    if (n === 'getSliceGlowLevels') return [];
    if (n === 'getLayerVoiceActivity') return [false, false, false, false];
    if (n === 'getScanPosition') return -1;
    if (n === 'getScanWindowBounds') return { start: 0, end: 1 };
    if (n === 'getModDrag') return window.__stub.md;
    if (n === 'getSynthMod') return '[]';
    if (n === 'getPoppedCards') return '';
    if (/getPresets/i.test(n)) return '[]';
    if (/getWaterfallView/i.test(n)) return '{}';
    if (/Json|JSON|getOscWavetable|SamplePayload/i.test(n)) return '{}';
    return 0;
  };
  const nf = (n) => (...a) => {
    window.__nc[n] = (window.__nc[n] | 0) + 1;
    if (a.length) window.__ncArg[n] = (window.__ncArg[n] | 0) + 1;
    if (n === 'getSynParam' && /_F[12]MIX$/.test(String(a[0]))) window.__fmix++;
    return Promise.resolve(ans(n, a));
  };
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk, getNativeFunction: nf,
                  backend: { addEventListener () {}, removeEventListener () {}, emitEvent () {} } };
  (function () { const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get () { return held; },
    set (v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState,
                                                  getToggleState: mine.getToggleState, getComboBoxState: mine.getComboBoxState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '',
    __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function () {}; Element.prototype.releasePointerCapture = function () {};
  setInterval(() => { try { window.__tiAlive && window.__tiAlive(); } catch (e) {} }, 50);   /* the C++ keepalive: a lane that exists, at rest */
  /* the scan-viz draw loop's own frames, by name */
  window.__tickScan = 0; const oR = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = function (cb) { return oR(function (ts) { if (cb && cb.name === 'tickScanViz') window.__tickScan++; return cb(ts); }); };
};
async function open (b, file, q) {
  const pg = await b.newPage(); await pg.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + file + (q || ''), { waitUntil: 'load', timeout: 60000 });
  await sleep(2500);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot')); });
  return { pg, errs };
}
/* "the synth is playing": what the editor timer does every tick while the output is audible */
const play = (pg, ms, notes) => pg.evaluate((ms, notes) => new Promise(res => {
  const t0 = performance.now(); let n = 0;
  const iv = setInterval(() => { if (notes) { window.__notesActive = 1; window.__notesActiveT = Date.now(); }
    try { window.__tiFrame(); } catch (e) {} n++;
    if (performance.now() - t0 >= ms) { clearInterval(iv); window.__notesActive = 0; res(n); } }, 16); }), ms, notes);

(async () => {
  const b = await puppeteer.launch({ executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
                                     headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  console.log('\n══ fb636 — THE HERO IS STILL · THE EDITOR STOPS SPENDING FOR NOTHING ══' + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  // ═══ PAGE A — the front page ═══════════════════════════════════════════════════════════════
  const A = await open(b, PLAIN), pg = A.pg;
  const pre = await pg.evaluate(() => ({ mac: window.__tiQuiet === undefined, panel: (typeof currentActivePanel !== 'undefined') ? currentActivePanel : 'undef',
                                         lane: !!(window.__tiLaneSeen && window.__tiLaneSeen()), rest: typeof window.__tiRest }));
  console.log('   host: Mac path (__tiQuiet never written) = ' + pre.mac + ' · panel ' + pre.panel + ' · lane seen ' + pre.lane + ' · __tiRest ' + pre.rest);
  await pg.evaluate(() => {
    const C = CanvasRenderingContext2D.prototype; window.__fops = 0;
    ['stroke', 'fill', 'fillRect', 'strokeRect', 'drawImage', 'clearRect', 'fillText'].forEach(m => { const o = C[m];
      C[m] = function (...a) { try { const cv = this.canvas; if (cv && cv.closest && cv.closest('#hero, #controls, #footer')) window.__fops++; } catch (e) {} return o.apply(this, a); }; });
    ['renderTerrain', 'updateAnimatedIcons', 'drawTapeReels', 'drawUndoIcon'].forEach(n => { const f = window[n]; window['__c_' + n] = 0;
      window[n] = function (...a) { window['__c_' + n]++; return f.apply(this, a); }; });
    window.__probe = 0; window.__tiFrameReg('zz-probe', () => { window.__probe++; });
    setActivePanel(null);
  });
  await sleep(900);

  // 1 — still while the synth plays
  await pg.evaluate(() => { window.__fops = 0; window.__c_renderTerrain = 0; window.__c_updateAnimatedIcons = 0; window.__probe = 0;
    window.__w0 = (window.__tiWakes | 0) + (window.__tiHeals | 0); window.__mh0 = (document.getElementById('meter-l') || {}).style ? document.getElementById('meter-l').style.height : ''; });
  const shipped = await play(pg, 2000, true);
  const R1 = await pg.evaluate(() => ({ probe: window.__probe, fops: window.__fops, terr: window.__c_renderTerrain, icons: window.__c_updateAnimatedIcons,
    wakes: (window.__tiWakes | 0) + (window.__tiHeals | 0) - window.__w0, mh0: window.__mh0, mh1: document.getElementById('meter-l') ? document.getElementById('meter-l').style.height : '' }));
  chk(R1.probe >= 60 && R1.terr <= R1.wakes && R1.icons <= R1.wakes && (R1.wakes > 0 || R1.fops === 0) && R1.mh0 === R1.mh1,
      '1  THE FRONT IS STILL WHILE THE SYNTH PLAYS: every shipped frame reaches the painters, and the front paints nothing',
      shipped + ' frames shipped, ' + R1.probe + ' painter passes · terrain renders ' + R1.terr + ' · knob-icon passes ' + R1.icons + ' · front canvas ops ' + R1.fops +
      ' · meter ' + JSON.stringify(R1.mh0) + '→' + JSON.stringify(R1.mh1) + (R1.wakes ? ' · (' + R1.wakes + ' wake/heal events in the window)' : ''));

  // 2 — never blank, always the same picture
  /* "The same picture" is judged by the DRAWING, not the pixels: Chrome moves a canvas off the GPU after enough pixel
     readbacks (a heuristic, and a loaded machine changes when it trips), and the two rasterisers differ by a few % of
     pixels — a pixel hash flaked on exactly that. So the terrain canvas's 2D calls are recorded (every path point,
     every fill/stroke with the style in force, rounded), the log restarting at each full clear, i.e. per render.
     Ink (any alpha on the canvas) stays a pixel readback: blank is blank on either rasteriser. */
  await pg.evaluate(() => {
    const C = CanvasRenderingContext2D.prototype; window.__tlog = []; const r = (v) => (typeof v === 'number') ? v.toFixed(3) : (v && v.width !== undefined ? 'img' + v.width : String(v));
    ['moveTo', 'lineTo', 'arc', 'stroke', 'fill', 'fillRect', 'drawImage', 'clearRect'].forEach(m => { const o = C[m];
      C[m] = function (...a) { try { if (this.canvas && this.canvas.id === 'terrain-canvas') {
          if (m === 'clearRect' && a[0] === 0 && a[1] === 0) window.__tlog = [];
          window.__tlog.push(m + '(' + a.map(r).join(',') + ')' + ((m === 'stroke' || m === 'fill' || m === 'fillRect' || m === 'drawImage')
            ? '[' + this.strokeStyle + '|' + this.fillStyle + '|' + this.globalAlpha.toFixed(3) + '|' + this.lineWidth + ']' : '')); } } catch (e) {}
        return o.apply(this, a); }; }); });
  const INK = () => pg.evaluate(() => { const c = document.getElementById('terrain-canvas'); if (!c || !c.width) return { n: -1, h: 0, len: 0 };
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0;
    for (let i = 3; i < d.length; i += 4) if (d[i] > 8) n++;
    const s = window.__tlog.join(';'); let h = 2166136261; for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619) >>> 0;
    return { n, h, len: window.__tlog.length }; });
  /* fb636 (merge) — the motion clock's wind-down tail can still be running frames after bar 1's notes, and any frame paints
     the cleared latch — which would hide a missing direct repaint. The check starts from TRUE rest: nothing winding. */
  for (let i = 0; i < 100 && await pg.evaluate(() => !!(window.__tiWindBusy && window.__tiWindBusy())); i++) await sleep(50);
  await sleep(200);
  await pg.evaluate(() => window.dispatchEvent(new Event('resize'))); await sleep(40);
  const I0 = await INK();
  await pg.evaluate(() => { window.__tlog = []; window.dispatchEvent(new Event('resize')); }); await sleep(40); const I1 = await INK();
  await pg.evaluate(() => { window.__tlog = []; const c = document.getElementById('terrain-canvas'); c.width = c.width; }); const Iw = await INK();
  await sleep(1400); const I2 = await INK();
  /* a theme change as setTheme ends it: the new palette current, then 'themechange' (setTheme itself throws in this stub
     host — updateAllKnobs reads params the stub never filled — before it reaches its own dispatch) */
  const theme = (nm) => pg.evaluate((nm) => { currentTheme = THEMES[nm]; window.dispatchEvent(new CustomEvent('themechange', { detail: { theme: nm } })); }, nm);
  await theme('dark'); await sleep(200); const I3 = await INK();
  await theme('light'); await sleep(200); const I4 = await INK();
  chk(I0.n > 0 && I1.n > 0 && I1.h === I0.h && Iw.n === 0 && I2.n > 0 && I2.h === I0.h && I3.n > 0 && I3.h !== I0.h && I4.h === I0.h,
      '2  A STILL PICTURE IS NEVER BLANK, AND IT IS ALWAYS THE SAME PICTURE: resize → at once · wiped → healed · dark → new colours · light → the same pixels',
      'ink ' + I0.n + ' · after resize ' + I1.n + (I1.h === I0.h ? ' (same)' : ' (DIFFERENT)') + ' · wiped ' + Iw.n + ' → ' + I2.n + (I2.h === I0.h ? ' (same)' : ' (DIFFERENT)') +
      ' · dark ' + I3.n + (I3.h !== I0.h ? ' (re-coloured)' : ' (NOT repainted)') + ' · light again ' + (I4.h === I0.h ? 'identical' : 'DIFFERENT'));

  // 3 — a hand keeps painting, and stops with it
  await sleep(600); await pg.evaluate(() => { window.__c_renderTerrain = 0; });
  const hr = await pg.evaluate(() => { const r = document.getElementById('hero').getBoundingClientRect(); return { x: r.left + r.width * 0.5, y: r.top + r.height * 0.5 }; });
  await pg.mouse.move(hr.x, hr.y); await pg.mouse.down();
  for (let i = 1; i <= 12; i++) { await pg.mouse.move(hr.x + i * 5, hr.y - i * 3); await sleep(45); }
  await pg.mouse.up();
  const dragR = await pg.evaluate(() => window.__c_renderTerrain);
  await sleep(1200); await pg.evaluate(() => { window.__c_renderTerrain = 0; }); await sleep(1200);
  const afterR = await pg.evaluate(() => window.__c_renderTerrain);
  chk(dragR >= 10 && afterR === 0, '3  A HAND KEEPS PAINTING (fb591): a ~600 ms drag repaints the front on every gesture frame, and the painting stops with the hand',
      dragR + ' front paints during the drag · ' + afterR + ' in the 1.2 s after it settled');

  // 4 — XY auto-play keeps its clock (it writes audio)
  const xy0 = await pg.evaluate(() => ({ e: state.xyEnabled, p: state.xyAutoPlay, m: state.xyAutoMode }));
  await pg.evaluate(() => { state.xyEnabled = true; state.xyAutoPlay = true; state.xyAutoMode = 1; window.__nc.setXYPad = 0; });
  const nOn = await play(pg, 1000, true);
  const onR = await pg.evaluate(() => ({ w: window.__nc.setXYPad | 0, act: document.getElementById('hero').classList.contains('xy-active'), cx: document.getElementById('crosshair-v').style.left }));
  await pg.evaluate(() => { state.xyAutoPlay = false; window.__tiRepaint(); window.__nc.setXYPad = 0; });
  await play(pg, 1000, true);
  const offR = await pg.evaluate(() => ({ w: window.__nc.setXYPad | 0, act: document.getElementById('hero').classList.contains('xy-active') }));
  await pg.evaluate((x) => { state.xyEnabled = x.e; state.xyAutoPlay = x.p; state.xyAutoMode = x.m; window.__tiRepaint(); }, xy0);
  chk(onR.w >= nOn * 0.6 && onR.act && offR.w === 0 && ! offR.act,
      '4  XY AUTO-PLAY KEEPS ITS CLOCK: on, setXYPad (the modulation source) is written on every frame as before; off, nothing is written and .xy-active clears',
      'on: ' + onR.w + ' setXYPad writes over ' + nOn + ' frames (crosshair at ' + onR.cx + ') · off: ' + offR.w + ' writes, xy-active ' + offR.act);

  // 5 — the undo glyph is change-gated
  const U = await pg.evaluate(() => { window.__c_drawUndoIcon = 0;
    for (let i = 0; i < 60; i++) window.updateTapeLoopState(false, false, false, 0.5, false, -1);
    const a = window.__c_drawUndoIcon;
    for (let i = 0; i < 10; i++) window.updateTapeLoopState(false, false, true, 0.5, true, -1);
    const b = window.__c_drawUndoIcon - a;
    return { a, b, cls: document.getElementById('btn-undo') ? document.getElementById('btn-undo').classList.contains('has-undo') : null }; });
  chk(U.a <= 1 && U.b === 1 && U.cls !== false, '5  THE UNDO GLYPH IS CHANGE-GATED: 60 identical tape-loop frames redraw it at most once; a change redraws it exactly once',
      'identical ×60 → ' + U.a + ' redraws · hasUndo flips ×10 → ' + U.b + ' · has-undo class ' + U.cls);

  // 6 — the Mac idle polls rest
  await sleep(1800);
  await pg.evaluate(() => { window.__nc = {}; window.__fmix = 0; });
  await sleep(3000);
  const rest = await pg.evaluate(() => ({ md: window.__nc.getModDrag | 0, sm: window.__nc.getSynthMod | 0, fm: window.__fmix, flag: window.__tiRest() }));
  await pg.evaluate(() => { window.__nc = {}; window.__fmix = 0; });
  await play(pg, 3100, true);
  const live = await pg.evaluate(() => ({ sm: window.__nc.getSynthMod | 0, fm: window.__fmix }));
  await sleep(1800);
  await pg.evaluate(() => { window.__poppedCards = window.__poppedCards || {}; window.__poppedCards.mod = 1; window.__nc = {}; });
  await sleep(1000);
  const pc = await pg.evaluate(() => ({ md: window.__nc.getModDrag | 0, flag: window.__tiRest() }));
  await pg.evaluate(() => { delete window.__poppedCards.mod; window.__tiModIn = 0; });
  await sleep(300); await pg.evaluate(() => { window.__nc = {}; }); await sleep(1000);
  const gone = await pg.evaluate(() => ({ md: window.__nc.getModDrag | 0, modIn: window.__tiModIn }));
  chk(rest.flag === true && rest.md === 0 && rest.sm === 0 && rest.fm === 0 && live.fm >= 8 && live.sm >= 1 && pc.md >= 20 && gone.md === 0 && gone.modIn === undefined,
      '6  THE MAC IDLE POLLS REST: at rest nothing polls; playing they run at full rate; a floating card brings the drag receiver back at rest, and its end stops it',
      'rest 3 s: getModDrag ' + rest.md + ' · getSynthMod ' + rest.sm + ' · FMIX reads ' + rest.fm + ' (__tiRest ' + rest.flag + ')' +
      ' · playing 3.1 s: getSynthMod ' + live.sm + ' · FMIX reads ' + live.fm +
      ' · a card floating, at rest (__tiRest ' + pc.flag + '): getModDrag ' + pc.md + '/s · none floating: ' + gone.md + '/s, __tiModIn ' + gone.modIn);
  A.errs.length && console.log('   page errors (front): ' + A.errs.slice(0, 3).join(' | '));
  const errsA = A.errs.length;
  await pg.close();

  // ═══ PAGE B — a popped card window (the same page, ?card=arp) ════════════════════════════════
  const Bw = await open(b, PLAIN, '?card=arp'), pb = Bw.pg;
  await pb.evaluate(() => { window.__nc = {}; }); await sleep(2000);
  const cIdle = await pb.evaluate(() => window.__nc.getModDrag | 0);
  await pb.evaluate(() => { window.__stub.md = '{"l":1,"p":0,"c":0,"s":1,"lx":5,"ly":5,"in":0}'; }); await sleep(400);
  await pb.evaluate(() => { window.__nc = {}; }); await sleep(1000);
  const cDrag = await pb.evaluate(() => window.__nc.getModDrag | 0);
  await pb.evaluate(() => { window.__stub.md = '{"l":1,"p":1,"c":0,"s":2,"lx":5,"ly":5,"in":0}'; }); await sleep(1500);
  await pb.evaluate(() => { window.__nc = {}; }); await sleep(1000);
  const cAfter = await pb.evaluate(() => window.__nc.getModDrag | 0);
  chk(cIdle > 0 && cIdle <= 56 && cDrag >= 40 && cAfter <= 28,
      '7  A POPPED CARD\'S DRAG RECEIVER POLLS AT THE PACE OF ITS NEED: every 50 ms at rest (it was every frame), every frame while a drag is live, slower again after',
      'rest 2 s: ' + cIdle + ' getModDrag (every frame was ~120) · a live drag, 1 s: ' + cDrag + ' · after the drop, 1 s: ' + cAfter +
      '   (the quick-flick drop law is Tests/flowmod_gesture.js bar 7)');
  Bw.errs.length && console.log('   page errors (card): ' + Bw.errs.slice(0, 3).join(' | '));
  const errsB = Bw.errs.length;
  await pb.close();

  // ═══ PAGE C — the page as the editor serves it: index.html + the heroOverlay ═════════════════
  const Cw = await open(b, WITH), pc2 = Cw.pg;
  const ov = await pc2.evaluate(() => ({ pads: document.querySelectorAll('#ti-layer-pads .ti-layer-pad').length, mix: !!document.getElementById('mix-panel') }));
  const cnt = () => pc2.evaluate(() => ({ glow: window.__nc.getSliceGlowLevels | 0, act: window.__nc.getLayerVoiceActivity | 0,
    hasAll: window.__nc.getLayerHasSample | 0, hasArg: window.__ncArg.getLayerHasSample | 0, scan: window.__nc.getScanPosition | 0, tick: window.__tickScan }));
  const zero = () => pc2.evaluate(() => { window.__nc = {}; window.__ncArg = {}; window.__tickScan = 0; });
  await pc2.evaluate(() => setActivePanel(null)); await sleep(600);
  await zero(); await sleep(2000); const Ca = await cnt();                                            // front, no sample
  await pc2.evaluate(() => { window.__stub.has = 1; }); await sleep(400); await zero(); await sleep(2000); const Cr = await cnt();   // front, a sample, AT REST: parked (fb636)
  await pc2.evaluate(() => { window.__tiForceActive = true; window.dispatchEvent(new Event('tiactive')); }); await sleep(200);    // the edge out of rest
  await zero(); await sleep(2000); const Cb = await cnt();                                                                         // front, a sample, awake
  await pc2.evaluate(() => setActivePanel('syn')); await sleep(300); await zero(); await sleep(2000); const Cc = await cnt();        // the synth page
  const padsLit = await pc2.evaluate(() => document.querySelectorAll('#ti-layer-pads .ti-layer-pad.playing').length);
  await pc2.evaluate(() => setActivePanel(null)); await sleep(400); await zero(); await sleep(1000); const Cd = await cnt();          // back on the front
  await pc2.evaluate(() => { window.__uiParked = 1; }); await sleep(300); await zero(); await sleep(1000); const Cp = await cnt();   // the editor closed (M3)
  await pc2.evaluate(() => { window.__uiParked = 0; window.dispatchEvent(new Event('tiactive')); }); await sleep(300);
  await pc2.evaluate(() => { window.__tiForceActive = false; }); await sleep(300); await zero(); await sleep(1000); const Cf = await cnt();   // rest again
  await pc2.evaluate(() => { const p = document.getElementById('mix-panel'); if (p) p.classList.add('open'); }); await sleep(300); await zero(); await sleep(1000);
  const Ce = await cnt();
  await pc2.evaluate(() => { const p = document.getElementById('mix-panel'); if (p) p.classList.remove('open'); });
  const hasNoArg = (o) => o.hasAll - o.hasArg;
  chk(ov.pads === 4 && ov.mix &&
      hasNoArg(Ca) >= 12 && Ca.glow === 0 && Ca.tick <= 2 &&
      Cr.glow === 0 && Cr.act === 0 && Cb.glow >= 60 && Cb.act >= 60 && Cb.tick <= 2 &&
      Cp.glow === 0 && Cp.act === 0 && hasNoArg(Cp) === 0 && Cf.glow === 0 && Cf.act === 0 &&
      Cc.glow === 0 && Cc.act === 0 && Cc.hasAll === 0 && Cc.scan === 0 && Cc.tick === 0 && padsLit === 0 &&
      Cd.glow >= 30 && hasNoArg(Cd) >= 5 && Ca.hasArg === 0 && Ce.hasArg >= 8,
      '8  THE HERO SAMPLER\'S POLLS EXIST ONLY WHILE THE FRONT SHOWS, EACH ONLY FOR ITS NEED (the real overlay from PluginEditor.cpp)',
      'overlay: ' + ov.pads + ' pads, mix panel ' + ov.mix +
      '\n        front, no sample, 2 s: empty-state polls ' + hasNoArg(Ca) + ' · glow ' + Ca.glow + ' · scan-viz frames ' + Ca.tick +
      '\n        front, a sample, AT REST, 2 s: glow ' + Cr.glow + ' · pad activity ' + Cr.act + ' (parked: no note, no hand, no frame)' +
      '\n        front, a sample, awake, 2 s:  glow ' + Cb.glow + ' · pad activity ' + Cb.act + ' · scan-viz frames ' + Cb.tick + ' (nothing scans)' +
      '\n        SYNTH page, 2 s:       glow ' + Cc.glow + ' · activity ' + Cc.act + ' · has-sample ' + Cc.hasAll + ' · scan ' + Cc.scan + ' · scan-viz frames ' + Cc.tick + ' · pads lit ' + padsLit +
      '\n        front again, 1 s:      glow ' + Cd.glow + ' · empty-state polls ' + hasNoArg(Cd) +
      '\n        editor closed (__uiParked), 1 s: glow ' + Cp.glow + ' · activity ' + Cp.act + ' · empty-state polls ' + hasNoArg(Cp) +
      '\n        rest again, 1 s:       glow ' + Cf.glow + ' · activity ' + Cf.act +
      '\n        Mix panel shut → layer-dot reads ' + Ca.hasArg + ' · open, 1 s → ' + Ce.hasArg);
  Cw.errs.length && console.log('   page errors (overlay): ' + Cw.errs.slice(0, 3).join(' | '));
  const errsC = Cw.errs.length;
  await pc2.close();
  chk(errsA + errsB + errsC === 0, '0  THE PAGE RUNS: no page errors on the front page, a popped card window, or the page with the overlay',
      'front ' + errsA + ' · card ' + errsB + ' · overlay ' + errsC);

  // ═══ 9 — the C++ frame ══════════════════════════════════════════════════════════════════════
  const proc = fs.readFileSync(PROC, 'utf8');
  const has = (re) => re.test(cpp);
  const C9 = {
    scope:    ! cpp.includes('"window.updateVisualization("'),
    capture:  ! cpp.includes('"window.updateCaptureState("'),
    reso:     ! has(/^\s*js << "try\{if\(window\.__terrainReso/m) && has(/^\s*\/\/\s*js << "try\{if\(window\.__terrainReso/m),
    prealloc: has(/js\.preallocateBytes \(\(size_t\) juce::jmax \(4096, lastFrameBytes_/) && has(/lastFrameBytes_ = \(int\) js\.getNumBytesAsUTF8\(\)/),
    modcmp:   has(/json\.isNotEmpty\(\) && json == audioProcessor\.modStateJson/),
    crv:      has(/else if \(\+\+crvQuietCtr_ >= 20\)/),
    ring:     (proc.match(/scopeBuffer\[/g) || []).length >= 1,
  };
  chk(Object.values(C9).every(Boolean), '9  THE C++ FRAME: no hero scope string, no capture segment, the harmonograph feed a comment, one allocation per frame, the mod-state save compares, the curve card rests — the scope RING stays',
      JSON.stringify(C9));

  console.log('\n  ' + pass + ' pass, ' + fail + ' fail' + (MUT ? '   (mutation: ' + MUT + ')' : ''));
  await b.close();
  try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) {}
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
