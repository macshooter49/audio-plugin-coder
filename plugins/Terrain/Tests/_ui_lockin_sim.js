// THE PAINT IS KING — a headless soak of the whole UI (both UAs). Max, 2026-09-18: "the paint never scratches and the
//   paint never disappears... run simulations... see where the UI will break. And it can never break."
// What it does: instruments the page BEFORE it boots (every rAF callback, every registered painter, every timer, every
//   window/document listener, long tasks), then walks scenarios — panels, every rack card, six Bodes, the Patcher at 100
//   nodes, zoom/viz/fit/dice, menus, card-only windows, a 60 Hz push soak, theme flips — and after every step fingerprints
//   every VISIBLE canvas. A canvas that had ink before a step and is visible-and-blank after it is a SCRATCH. A painter that
//   throws is DEAD PAINT (the registry swallows it silently). Timers/listeners/DOM that only ever grow are LEAKS.
//   node Tests/_ui_lockin_sim.js [index.html]     SIM_UA=win for the Windows arbiter path     SIM_VERBOSE=1 for every canvas
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const SRC = process.argv[2] || (process.cwd() + '/Source/ui/public/index.html');
const WIN = process.env.SIM_UA === 'win';
const VERB = !!process.env.SIM_VERBOSE;
let pass = 0, fail = 0, warns = 0;
const ok = (c, label, detail) => { if (c) { pass++; console.log('  PASS  ' + label); } else { fail++; console.log('  FAIL  ' + label + (detail ? '\n          ' + String(detail).slice(0, 900) : '')); } };
const warn = (label, detail) => { warns++; console.log('  WARN  ' + label + (detail ? '\n          ' + String(detail).slice(0, 600) : '')); };

const stub = () => {
  const states = new Map();
  const mk = (name) => { const props = { start: 0, end: 1, skew: 1, name, label: '', numSteps: 100, interval: 0, parameterIndex: states.size };
    const st = { name, scaledValue: 0, properties: props, getScaledValue: () => st.scaledValue, setScaledValue(v){ st.scaledValue = v; },
      getNormalisedValue(){ return st.scaledValue; }, setNormalisedValue(v){ st.scaledValue = v; (st.__ls || []).forEach(f => { try { f(); } catch (e) {} }); },
      valueChangedEvent: { addListener(f){ (st.__ls = st.__ls || []).push(f); return { remove(){} }; }, removeListener(){} },
      propertiesChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      getChoiceIndex: () => 0, setChoiceIndex(){}, getValue: () => false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} }; return st; };
  const get = (nm) => { if (!states.has(nm)) states.set(nm, mk(nm)); return states.get(nm); };
  window.__natCount = {}; window.__params = { SYN_NOISE_OUT: 1, SYN_NOISE2_OUT: 1 };
  const LIB = { path: '/lib', exists: true, total: 9, cats: { Drums: ['kick.wav', 'snare.wav', 'hat.wav', 'clap.wav'], '808': ['808a.wav', '808b.wav'], Keys: ['k1.wav', 'k2.wav'], Pad: ['p1.wav'] } };
  const nativeFn = (n) => (...a) => new Promise((r) => { window.__natCount[n] = (window.__natCount[n] || 0) + 1;
    if (n === 'setSynParam') { window.__params[String(a[0])] = +a[1]; try { get(String(a[0])).scaledValue = +a[1]; } catch (e) {} return r(0); }
    if (n === 'getSynParam') { const id = String(a[0]); return r(window.__params[id] != null ? window.__params[id] : get(id).scaledValue); }
    if (n === 'getSynParams') { return r(String(a[0]).split(',').map(id => (window.__params[id] != null ? window.__params[id] : get(id).scaledValue)).join(',')); }
    if (n === 'scanSampleFactory') return r(JSON.stringify(LIB));
    if (n === 'getSynthLfoShapes') return r('{"shapes":[]}');
    if (/Json|JSON/.test(n)) return r('{}'); if (/^get|^list|^scan/.test(n)) return r('[]'); r(0); });
  window.Juce = { getSliderState: get, getToggleState: get, getComboBoxState: get, getNativeFunction: nativeFn, backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get(){ return held; }, set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
};

// ── the instruments (installed before any page script) ─────────────────────────────────────────────────────────────
const instrument = () => {
  const S = window.__sim = { rafErr: [], painterErr: {}, timerErr: [], ivl: new Map(), longTasks: [], lis: {}, lisN: 0, pt: {}, passes: 0, rafN: 0 };
  const snip = (f) => ((f && f.name) || '') + ':' + String(f).replace(/\s+/g, ' ').slice(0, 150);
  const oR = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = function(cb){ return oR(function(ts){ S.rafN++; try { return cb(ts); } catch (e) { S.rafErr.push({ m: String(e && e.message).slice(0, 160), f: snip(cb) }); throw e; } }); };
  let reg = null;
  Object.defineProperty(window, '__tiFrameReg', { configurable: true, get(){ return reg; }, set(f){
    reg = typeof f !== 'function' ? f : function(name, fn){ return f(name, function(ts){ const t0 = performance.now(); try { return fn(ts); }
      catch (e) { const r = S.painterErr[name] = S.painterErr[name] || { n: 0, m: '' }; r.n++; r.m = String((e && e.stack) || e).replace(/\s+/g, ' ').slice(0, 260); throw e; }
      finally { const dt = performance.now() - t0; const p = S.pt[name] = S.pt[name] || { n: 0, max: 0, sum: 0 }; p.n++; p.sum += dt; if (dt > p.max) p.max = dt; } }); }; } });
  const oI = window.setInterval.bind(window), oCI = window.clearInterval.bind(window), oT = window.setTimeout.bind(window);
  window.setInterval = function(cb, ms){ const w = typeof cb === 'function' ? function(){ try { return cb.apply(this, arguments); } catch (e) { S.timerErr.push({ m: String(e && e.message).slice(0, 160), f: snip(cb) }); throw e; } } : cb;
    const id = oI(w, ms); S.ivl.set(id, { ms, f: snip(cb) }); return id; };
  window.clearInterval = function(id){ S.ivl.delete(id); return oCI(id); };
  window.setTimeout = function(cb, ms){ const w = typeof cb === 'function' ? function(){ try { return cb.apply(this, arguments); } catch (e) { S.timerErr.push({ m: String(e && e.message).slice(0, 160), f: snip(cb) }); throw e; } } : cb; return oT(w, ms); };
  const oA = EventTarget.prototype.addEventListener;
  EventTarget.prototype.addEventListener = function(t, f, o){ if (this === window || this === document || this === document.body) { const k = (this === window ? 'window' : this === document ? 'document' : 'body') + ':' + t; S.lis[k] = (S.lis[k] || 0) + 1; S.lisN++; } return oA.apply(this, arguments); };
  try { new PerformanceObserver(l => l.getEntries().forEach(e => S.longTasks.push({ t: e.startTime | 0, d: e.duration | 0 }))).observe({ entryTypes: ['longtask'] }); } catch (e) {}
  // the canvas fingerprint: every visible canvas → ink count on a 24×24 downsample + a cheap colour checksum
  window.__simCanvases = function(){
    const tmp = document.createElement('canvas'); tmp.width = 24; tmp.height = 24; const tc = tmp.getContext('2d', { willReadFrequently: true });
    const out = [], seen = {};
    document.querySelectorAll('canvas').forEach((c) => {
      const r = c.getBoundingClientRect(); let vis = r.width > 0 && r.height > 0;
      try { vis = vis && c.checkVisibility({ opacityProperty: true, visibilityProperty: true }); } catch (e) {}
      if (vis) { const pr = document.getElementById('tp-page'); const inTp = pr && pr.contains(c); if (inTp) { const pb = pr.getBoundingClientRect(); vis = r.right > pb.left && r.left < pb.right && r.bottom > pb.top && r.top < pb.bottom; } }
      let ink = -1, sum = 0;
      if (vis && c.width > 0 && c.height > 0) { try { tc.clearRect(0, 0, 24, 24); tc.drawImage(c, 0, 0, 24, 24); const d = tc.getImageData(0, 0, 24, 24).data; let n = 0; for (let k = 3; k < d.length; k += 4) if (d[k] > 8) { n++; sum = (sum * 31 + d[k - 3] + d[k - 2] * 3 + d[k - 1] * 7) >>> 0; } ink = n; } catch (e) { ink = -2; } }
      const dev = c.closest('.fxr-dev'), node = c.closest('.tp-node'), idp = c.parentElement ? c.parentElement.closest('[id]') : null;
      let key = (c.id || '') + '|' + (idp ? idp.id : '') + '|' + String(c.className || '').slice(0, 30) + '|' + (dev ? 'dev' + dev.getAttribute('data-dev') + ':' + (dev.getAttribute('data-core') || '') : '') + '|' + (node ? (node.dataset.key || node.id || '') : '');
      seen[key] = (seen[key] || 0) + 1; key += '#' + seen[key];
      out.push({ key, w: c.width, h: c.height, cw: Math.round(r.width), ch: Math.round(r.height), vis, ink, sum });
    });
    return out;
  };
  window.__simCounts = function(){ return { dom: document.getElementsByTagName('*').length, ivl: S.ivl.size, lis: S.lisN, raf: S.rafN, passes: S.passes, lt: S.longTasks.length, ltMax: S.longTasks.reduce((m, e) => Math.max(m, e.d), 0), rafErr: S.rafErr.length, timerErr: S.timerErr.length, painterErr: Object.keys(S.painterErr).length }; };
};

(async () => {
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox', '--disable-gpu-vsync'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  if (WIN) await p.setUserAgent('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36');
  await p.evaluateOnNewDocument(instrument); await p.evaluateOnNewDocument(stub);
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 200))); p.on('console', m => { if (m.type() === 'error') errs.push('console: ' + m.text().slice(0, 200)); });
  console.log('UA ' + (WIN ? 'Windows (arbiter)' : 'Mac'));
  await p.goto('file://' + SRC + '?page=1', { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); ['a', 'b', 'c', 'd'].forEach(o => { const e = document.getElementById('osc-' + o + '-device'); if (e) e.classList.remove('osc-off'); }); const t = document.getElementById('tape-toggle'); if (t && t.classList.contains('off')) t.click(); });
  await sleep(600);

  const snap = () => p.evaluate(() => window.__simCanvases());
  const counts = () => p.evaluate(() => window.__simCounts());
  const heap = async () => { const m = await p.metrics(); return Math.round(m.JSHeapUsedSize / 1048576); };
  let prev = null, prevCounts = await counts(), lastErr = 0;
  const scratches = [], deadPainters = {};
  async function step(label, fn, opts) {
    opts = opts || {};
    const before = opts.against || prev || await snap();
    const t0 = Date.now(); await fn(); const wall = Date.now() - t0; await sleep(opts.wait == null ? 1400 : opts.wait);
    await p.evaluate(() => { try { window.__tiWake && window.__tiWake(); } catch (e) {} }); await sleep(200);   // the repair the real page gets from any gesture; a canvas still blank after it is blank for real
    const after = await snap(); const c = await counts();
    const vis = after.filter(x => x.vis), blank = vis.filter(x => x.ink === 0), bad = vis.filter(x => x.ink === -2);
    const byKey = {}; before.forEach(x => byKey[x.key] = x);
    const scr = blank.filter(x => byKey[x.key] && byKey[x.key].vis && byKey[x.key].ink >= 6);   /* a faint rest picture (the noise cloud at rest: 2 of 576) flickers around the 24×24 sampler's threshold; a picture that was THERE is ≥ 6 */
    const newErrs = errs.slice(lastErr); lastErr = errs.length;
    const painterErr = await p.evaluate(() => window.__sim.painterErr);
    Object.keys(painterErr).forEach(k => { if (!deadPainters[k]) deadPainters[k] = { at: label, ...painterErr[k] }; });
    const d = { dom: c.dom - prevCounts.dom, ivl: c.ivl - prevCounts.ivl, lis: c.lis - prevCounts.lis, lt: c.lt - prevCounts.lt };
    console.log(`\n▶ ${label}   ${wall} ms   canvases ${vis.length} visible, ${blank.length} blank${scr.length ? ', ' + scr.length + ' SCRATCHED' : ''}   Δdom ${d.dom} Δivl ${d.ivl} Δlis ${d.lis} longtasks ${d.lt} (max ${c.ltMax} ms)   heap ${await heap()} MB   errors ${newErrs.length}`);
    if (VERB) vis.forEach(x => console.log('     ' + (x.ink === 0 ? 'BLANK ' : '      ') + x.key + ' ' + x.w + 'x' + x.h + ' ink ' + x.ink));
    else if (blank.length) console.log('     blank: ' + blank.map(x => x.key).slice(0, 12).join(' , '));
    if (scr.length) { scr.forEach(x => scratches.push(label + ' :: ' + x.key)); console.log('     SCRATCHED: ' + scr.map(x => x.key).join(' , ')); }
    if (bad.length) console.log('     unreadable: ' + bad.map(x => x.key).join(' , '));
    if (newErrs.length) console.log('     errors: ' + newErrs.slice(0, 6).join(' | '));
    prev = after; prevCounts = c;
    return { after, c, d, newErrs, scr, blank, wall };
  }
  const panel = async (w) => { await p.evaluate(w => window.setActivePanel(w), w); };

  // ── 1 · panels, three laps: paint present on lap 1 must be present on lap 3 ───────────────────────────────────────
  const lapSnaps = {};
  const PANELS = ['syn', 'eq', 'dly', 'mod', 'tp', null];
  for (let lap = 1; lap <= 3; lap++) for (const w of PANELS) {
    const r = await step(`panel ${w || 'front'} lap ${lap}`, () => panel(w), { wait: 700, against: lapSnaps[w] });
    if (lap === 1) lapSnaps[w] = r.after;
  }
  ok(scratches.length === 0, '[1] three laps around the panels: nothing that was painted on lap 1 is blank on lap 3', scratches.join('\n          '));

  // ── 2 · the rack: every card kind, then dice, then a churn cycle (add all / remove all ×3) ─────────────────────────
  await panel('syn'); await sleep(500); prev = await snap(); prevCounts = await counts();
  const CORES = ['reverb', 'delay', 'saturate', 'granular', 'tape', 'flt', 'cho', 'fla', 'pha', 'eqz', 'wid', 'cmp', 'ott', 'bod', 'utl', 'spl'];
  let r2 = await step('rack: one of every card (16) + flow chain', async () => { await p.evaluate((C) => { C.forEach(c => { try { window.__fxrAdd(c); } catch (e) {} }); try { window.__flowSetChain(['glitch', 'chop', 'arp']); } catch (e) {} }, CORES); }, { wait: 2500 });
  const rackCanv = r2.after.filter(x => x.vis && /dev\d/.test(x.key) && !/fx-spec/.test(x.key));   /* the audio layer (fb442) has no ink without signal — by design */
  ok(rackCanv.length > 0 && rackCanv.every(x => x.ink > 0), '[2] every visible rack-card canvas has ink after the add', rackCanv.filter(x => x.ink === 0).map(x => x.key).join(' , '));
  const s0 = scratches.length;
  await step('rack: dice ×12 (fx + mod)', async () => { for (let i = 0; i < 12; i++) { await p.evaluate(() => { try { window.__fxrDice(); } catch (e) {} try { window.__modDice(); } catch (e) {} }); await sleep(120); } }, { wait: 1500 });
  ok(scratches.length === s0, '[2] dice ×12 scratched nothing', scratches.slice(s0).join(' , '));
  const churn = [];
  for (let k = 0; k < 3; k++) {
    const r = await step(`rack churn ${k + 1}: remove all, add all`, async () => { await p.evaluate((C) => { let n = 0; while (window.__fxrDevs && window.__fxrDevs().length && n++ < 40) window.__fxrRemove(0); C.forEach(c => { try { window.__fxrAdd(c); } catch (e) {} }); }, CORES); }, { wait: 1800 });
    churn.push(r);
  }
  const c1 = churn[0].c, c3 = churn[2].c;
  ok(c3.dom - c1.dom <= 40, '[2] rack churn: the DOM does not grow (≤ 40 nodes over two more cycles)', `dom ${c1.dom} → ${c3.dom}`);
  ok(c3.ivl - c1.ivl <= 1, '[2] rack churn: no interval leaks', `ivl ${c1.ivl} → ${c3.ivl}`);
  ok(c3.lis - c1.lis <= 4, '[2] rack churn: no window/document listener leaks (≤ 4 over two cycles)', `lis ${c1.lis} → ${c3.lis}`);

  // ── 3 · six Bodes, six Splitters: every picture inked, the Bodes distinct once their shifts differ ────────────────
  await step('rack: six Bodes + six Splitters', async () => { await p.evaluate(() => { let n = 0; while (window.__fxrDevs().length && n++ < 40) window.__fxrRemove(0); for (let i = 0; i < 6; i++) { try { window.__fxrAdd('bod'); } catch (e) {} } for (let i = 0; i < 6; i++) { try { window.__fxrAdd('spl'); } catch (e) {} } }); }, { wait: 2500 });
  const bodes = await p.evaluate(() => window.__fxrDevs().map(d => ({ core: d.core })));
  const bodCards = bodes.filter(x => x.core === 'bod'), splCards = bodes.filter(x => x.core === 'spl');
  ok(bodCards.length === 6 && splCards.length === 6, '[3] six Bodes and six Splitters exist', JSON.stringify({ bod: bodCards.length, spl: splCards.length }));
  const bodSpl = await p.evaluate(() => window.__fxrDevs().map((d, i) => (d.core === 'bod' || d.core === 'spl') ? 'dev' + i + ':' : null).filter(Boolean));
  const visCards = (await snap()).filter(x => x.vis && bodSpl.some(k => x.key.indexOf('|' + k) >= 0) && !/fx-spec/.test(x.key));
  const svgPics = await p.evaluate(() => [...document.querySelectorAll('.fxr-core[data-core="bod"], .fxr-core[data-core="spl"]')].map(c => { const r = c.getBoundingClientRect(); const paths = [...c.querySelectorAll('svg *')].filter(el => /^(path|line|rect|circle|polyline)$/i.test(el.tagName) && el.getBoundingClientRect().width + el.getBoundingClientRect().height > 2).length;   /* the Bode's rail and node, the Splitter's lanes: lines and rects, not paths */ return { core: c.getAttribute('data-core'), vis: r.width > 0 && r.height > 0, paths }; }));
  ok(svgPics.length === 12 && svgPics.every(x => !x.vis || x.paths > 0) && visCards.every(x => x.ink > 0), '[3] every visible Bode/Splitter picture is drawn (its SVG has a path; its audio canvas is signal-only)', JSON.stringify({ svgPics: svgPics.filter(x => x.vis && !x.paths), blank: visCards.filter(x => x.ink === 0).map(x => x.key) }));

  // ── 4 · the Patcher at 100 nodes ───────────────────────────────────────────────────────────────────────────────────
  await p.evaluate(() => { let n = 0; while (window.__fxrDevs().length && n++ < 40) window.__fxrRemove(0); });
  await panel('tp'); await sleep(1200); await p.evaluate(() => { try { window.__tpFit(); } catch (e) {} }); await sleep(400); prev = await snap(); prevCounts = await counts();
  const fill = await p.evaluate(async () => {
    const cat = window.__tpCatalog(); const keys = cat.map(x => x.k || x.key || x.id).filter(Boolean); const added = [], failed = [];
    let i = 0;
    for (let round = 0; round < 8 && window.__tpNodes().length < 100; round++) for (const k of keys) { if (window.__tpNodes().length >= 100) break; const before = window.__tpNodes().length; try { window.__tpAdd(k, 80 + (i % 10) * 180, 80 + Math.floor(i / 10) * 150); } catch (e) { failed.push(k + ':' + e.message); } i++; if (window.__tpNodes().length > before) added.push(k); await new Promise(r => setTimeout(r, 15)); }
    return { keys: keys.length, nodes: window.__tpNodes().length, added: added.length, failed: failed.slice(0, 8), sample: keys.slice(0, 40) };
  });
  console.log('   catalog ' + fill.keys + ' keys → ' + fill.nodes + ' nodes (' + fill.added + ' adds)' + (fill.failed.length ? '  failed: ' + fill.failed.join(' ') : ''));
  const r4 = await step(`patcher: ${fill.nodes} nodes`, async () => {}, { wait: 2500 });
  ok(fill.nodes >= 60, '[4] the Patcher holds a big canvas (≥ 60 nodes from the catalog)', 'nodes ' + fill.nodes);
  const tpCanv = r4.after.filter(x => x.vis && x.key.split('|')[4] && !x.key.endsWith('|'));
  const timing = await p.evaluate(() => { const T = (f, n) => { let mx = 0, sum = 0; for (let i = 0; i < n; i++) { const t0 = performance.now(); f(); const dt = performance.now() - t0; sum += dt; if (dt > mx) mx = dt; } return { avg: +(sum / n).toFixed(2), max: +mx.toFixed(2) }; };
    return { repaint: T(() => window.__tiRepaint(), 20), derive: T(() => window.__tpDerive(), 20), layout: T(() => { try { window.__tpLayout && window.__tpLayout(); } catch (e) {} }, 5) }; });
  console.log('   timing: painter pass ' + JSON.stringify(timing.repaint) + ' ms · derive ' + JSON.stringify(timing.derive) + ' ms · layout ' + JSON.stringify(timing.layout) + ' ms');
  ok(timing.repaint.max < 50, '[4] a full painter pass at ' + fill.nodes + ' nodes stays under 50 ms (max ' + timing.repaint.max + ')');
  ok(timing.derive.max < 30, '[4] derive() at ' + fill.nodes + ' nodes stays under 30 ms (max ' + timing.derive.max + ')');
  // a drag across the canvas: 60 pointermoves, count long tasks and frames
  const drag = await p.evaluate(async () => { const pg = document.getElementById('tp-page').getBoundingClientRect(); const ns = window.__tpNodes().map(q => window.__tpNodeByKey(q.key)).filter(x => x && x.wrap); const n = ns.find(x => { const r = x.wrap.getBoundingClientRect(); return r.width > 20 && r.left > pg.left && r.top > pg.top + 40 && r.right < pg.right && r.bottom < pg.bottom; }) || ns.find(x => x.wrap.getBoundingClientRect().width > 0); if (!n) return null; const r = n.wrap.getBoundingClientRect(); return { x: r.left + 4, y: r.top + Math.min(r.height - 4, 30), key: n.key }; });
  const ltBefore = (await counts()).lt, rafBefore = (await counts()).raf;
  const t0d = Date.now();
  if (drag) { await p.mouse.move(drag.x, drag.y); await p.mouse.down(); for (let i = 1; i <= 60; i++) { await p.mouse.move(drag.x + i * 2, drag.y + i, { steps: 1 }); await sleep(8); } await p.mouse.up(); }
  const dragMs = Date.now() - t0d; const cAfterDrag = await counts();
  const dragLt = await p.evaluate((t) => window.__sim.longTasks.filter(e => e.t > performance.now() - t), dragMs + 50);
  console.log('   drag: ' + dragMs + ' ms wall, ' + (cAfterDrag.raf - rafBefore) + ' rAF callbacks, ' + dragLt.length + ' long tasks' + (dragLt.length ? ' (max ' + Math.max(...dragLt.map(e => e.d)) + ' ms)' : ''));
  ok(drag && dragLt.filter(e => e.d > 100).length === 0, '[4] dragging a node across ' + fill.nodes + ' nodes never blocks the thread > 100 ms', JSON.stringify(dragLt.slice(0, 6)));
  const s4 = scratches.length;
  await step('patcher: zoom 0.5', () => p.evaluate(() => { const v = window.__tpView(); window.__tpSetView(v.x, v.y, 0.5); }), { wait: 1200 });
  await step('patcher: zoom 2.0', () => p.evaluate(() => { const v = window.__tpView(); window.__tpSetView(v.x, v.y, 2.0); }), { wait: 1200 });
  await step('patcher: fit', () => p.evaluate(() => window.__tpFit()), { wait: 1200 });
  await step('patcher: viz on', () => p.evaluate(() => { try { window.__tpViz(true); } catch (e) {} }), { wait: 1200 });
  await step('patcher: viz off', () => p.evaluate(() => { try { window.__tpViz(false); } catch (e) {} }), { wait: 1200 });
  await step('patcher: dice ×8', async () => { for (let i = 0; i < 8; i++) { await p.evaluate(() => { try { window.__tpDice(); } catch (e) {} }); await sleep(250); } }, { wait: 2000 });
  ok(scratches.length === s4, '[4] zoom / fit / viz / dice on the big canvas scratched nothing', scratches.slice(s4).join('\n          '));
  const hs = [];
  for (let k = 0; k < 3; k++) { await step(`patcher soak ${k + 1}: syn → tp, fit, dice ×3`, async () => { await panel('syn'); await sleep(400); await panel('tp'); await sleep(600); await p.evaluate(() => { try { window.__tpFit(); } catch (e) {} for (let i = 0; i < 3; i++) { try { window.__tpDice(); } catch (e) {} } }); await sleep(800); await p.evaluate(() => { try { window.__fxrClear(); window.__flowSetChain([]); } catch (e) {} }); }, { wait: 1500 }); hs.push({ h: await heap(), c: await counts() }); }   /* the rolled patch is emptied before counting: a bigger roll is not a leak */
  ok(hs[2].c.dom - hs[0].c.dom <= 150, '[4] soak: the DOM does not grow across three syn→tp laps (≤ 150; the rolled flow cards and their chips vary by ~100)', `dom ${hs[0].c.dom} → ${hs[2].c.dom}`);
  ok(hs[2].c.ivl - hs[0].c.ivl <= 1, '[4] soak: no interval leaks across the laps', `ivl ${hs[0].c.ivl} → ${hs[2].c.ivl}`);
  ok(hs[2].c.lis - hs[0].c.lis <= 6, '[4] soak: no window/document listener leaks across the laps (≤ 6)', `lis ${hs[0].c.lis} → ${hs[2].c.lis}`);
  ok(hs[2].h - hs[0].h <= 25, '[4] soak: the heap does not climb (≤ 25 MB over two more laps)', `heap ${hs[0].h} → ${hs[2].h} MB`);

  // ── 5 · a 60 Hz push soak with notes on, then a held pointer: the painters must stay cheap and never throw ─────────
  const soak = await p.evaluate(async () => { window.__sim.pt = {}; window.__notesActive = 1; window.__notesActiveT = Date.now(); const t0 = performance.now(); let n = 0;
    await new Promise(res => { const iv = setInterval(() => { window.__notesActiveT = Date.now(); try { window.__tiFrame(); } catch (e) {} if (++n >= 300) { clearInterval(iv); res(); } }, 16); });
    window.__notesActive = 0; const dt = performance.now() - t0; const pt = window.__sim.pt; const worst = Object.keys(pt).map(k => [k, +pt[k].max.toFixed(1), +(pt[k].sum / Math.max(1, pt[k].n)).toFixed(2)]).sort((a, b) => b[1] - a[1]).slice(0, 8);
    return { ms: Math.round(dt), passes: Object.values(pt).reduce((m, x) => Math.max(m, x.n), 0), worst }; });
  console.log('   push soak: 300 frames in ' + soak.ms + ' ms, ' + soak.passes + ' painter passes; worst painters [name, max ms, avg ms]: ' + JSON.stringify(soak.worst));
  const worstMax = soak.worst.length ? soak.worst[0][1] : 0;
  ok(worstMax < 40, '[5] no single painter takes 40 ms at ' + fill.nodes + ' nodes under a 60 Hz push (worst ' + worstMax + ' ms)');
  ok(soak.passes >= 100, '[5] the push lane actually painted (≥ 100 passes for 300 pushes; got ' + soak.passes + ')');

  // ── 6 · menus: every opener measured (main-thread block + time to visible) ────────────────────────────────────────
  await panel('syn'); await sleep(600);
  const menus = await p.evaluate(async () => {
    const isVis = (e) => { const r = e.getBoundingClientRect(); return r.width > 4 && r.height > 4; };
    const T = async (label, open, sel) => { const before = new Set(document.body.children); const t0 = performance.now(); let err = ''; try { await open(); } catch (e) { err = e.message; } const sync = performance.now() - t0; let vis = false, tries = 0;
      while (tries++ < 60) { if (sel && [...document.querySelectorAll(sel)].some(isVis)) { vis = true; break; } if ([...document.body.children].some(c => !before.has(c) && isVis(c))) { vis = true; break; } await new Promise(r => setTimeout(r, 25)); }
      const total = performance.now() - t0; document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })); document.body.click(); await new Promise(r => setTimeout(r, 150)); return { label, sync: +sync.toFixed(1), total: +total.toFixed(0), vis, err }; };
    const out = [];
    out.push(await T('preset browser', () => document.getElementById('preset-name').click(), '#tp-q'));
    out.push(await T('wavetable menu (A)', () => window.openWtSelectMenu && window.openWtSelectMenu('a', { clientX: 200, clientY: 200, preventDefault(){}, stopPropagation(){} }), '.wt-menu, .mv-menu, .wtm'));
    out.push(await T('sample browser (A)', () => window.openSampleBrowser && window.openSampleBrowser('a', { clientX: 200, clientY: 200, preventDefault(){}, stopPropagation(){} }), '.samp-br, .sbr, .mv-menu'));
    try { window.__flowSetChain(['chop']); } catch (e) {} await new Promise(r => setTimeout(r, 300));
    out.push(await T('flow card (chop)', () => window.__openFlowCard && window.__openFlowCard('chop'), '.ti-card'));
    window.__openFlowCard('chop'); await new Promise(r => setTimeout(r, 250));
    out.push(await T('flow card preset menu', async () => { const pn = document.querySelector('.ti-card .pn'); if (!pn) throw new Error('no .pn'); pn.click(); }, '.mv-menu, .samp-menu'));
    out.push(await T('curve card', () => window.__openCrvCard && window.__openCrvCard(), '.ti-card.crv, .crv-card'));
    return out; });
  menus.forEach(m => console.log('   menu ' + m.label.padEnd(20) + ' sync ' + String(m.sync).padStart(7) + ' ms   visible ' + (m.vis ? 'yes' : 'NO ') + ' after ' + m.total + ' ms' + (m.err ? '   ERR ' + m.err : '')));
  ok(menus.every(m => m.sync < 80), '[6] no menu opener blocks the thread for 80 ms', JSON.stringify(menus.filter(m => m.sync >= 80)));
  const s6 = scratches.length; await step('menus closed', async () => {}, { wait: 600 });
  ok(scratches.length === s6, '[6] opening and closing the menus scratched nothing', scratches.slice(s6).join(' , '));

  // ── 6b · the popped card pages boot on their own (no lane: the fallback clock) and paint
  for (const card of ['chop', 'arp', 'gli', 'lfo', 'mod', 'crv']) {
    const q = await b.newPage(); await q.setViewport({ width: 420, height: 320, deviceScaleFactor: 2 }); if (WIN) await q.setUserAgent(await p.evaluate(() => navigator.userAgent));
    await q.evaluateOnNewDocument(instrument); await q.evaluateOnNewDocument(stub); const qe = []; q.on('pageerror', e => qe.push(e.message.slice(0, 160)));
    await q.goto('file://' + SRC + '?card=' + card, { waitUntil: 'load' }); await sleep(2200);
    const st = await q.evaluate(() => { const cs = window.__simCanvases().filter(c => c.vis); return { canv: cs.length, blank: cs.filter(c => c.ink === 0).map(c => c.key).slice(0, 6), pe: Object.keys(window.__sim.painterErr), raf: window.__sim.rafErr.length, lane: window.__tiLaneSeen ? window.__tiLaneSeen() : null, passes: window.__sim.passes, cardOnly: window.__cardOnly }; });
    ok(qe.length === 0 && st.pe.length === 0 && st.raf === 0 && st.cardOnly === card, `[6b] card page ?card=${card} boots clean (${st.canv} canvases, ${st.blank.length} blank)`, JSON.stringify({ qe, st }));
    if (st.blank.length) console.log('     card ' + card + ' blank: ' + st.blank.join(' , '));
    await q.close();
  }

  // ── 7 · theme flips on every panel ────────────────────────────────────────────────────────────────────────────────
  const s7 = scratches.length;
  for (const w of ['syn', 'tp', null]) { await panel(w); await sleep(500); prev = await snap(); await step(`theme flip ×4 on ${w || 'front'}`, async () => { for (let i = 0; i < 4; i++) { await p.evaluate(i => document.documentElement.setAttribute('data-theme', i % 2 ? 'dark' : 'light'), i); await sleep(250); } await p.evaluate(() => document.documentElement.setAttribute('data-theme', 'dark')); }, { wait: 1200 }); }
  ok(scratches.length === s7, '[7] theme flips scratched nothing', scratches.slice(s7).join(' , '));

  // ── 8 · the verdicts that span every step ─────────────────────────────────────────────────────────────────────────
  const dead = Object.keys(deadPainters);
  ok(dead.length === 0, '[8] no registered painter ever threw (the registry swallows it — dead paint is silent)', dead.map(k => k + ' @ ' + deadPainters[k].at + ' ×' + deadPainters[k].n + ': ' + deadPainters[k].m).join('\n          '));
  const rafErr = await p.evaluate(() => window.__sim.rafErr.slice(0, 10)), timerErr = await p.evaluate(() => window.__sim.timerErr.slice(0, 10));
  ok(rafErr.length === 0, '[8] no rAF callback ever threw (a throw kills a self-rearming loop for good)', rafErr.map(e => e.m + ' in ' + e.f).join('\n          '));
  ok(timerErr.length === 0, '[8] no timer callback ever threw', timerErr.map(e => e.m + ' in ' + e.f).join('\n          '));
  ok(errs.length === 0, '[8] no page errors across the whole run', errs.slice(0, 12).join('\n          '));
  ok(scratches.length === 0, '[8] THE PAINT IS KING: no canvas that had ink went blank at any step', scratches.join('\n          '));
  const ivl = await p.evaluate(() => { const m = {}; window.__sim.ivl.forEach(v => { const k = v.ms + 'ms ' + v.f.slice(0, 70); m[k] = (m[k] || 0) + 1; }); return Object.entries(m).sort((a, b) => b[1] - a[1]).slice(0, 12); });
  console.log('\n   live intervals at the end: ' + (await counts()).ivl + '  top: ' + ivl.map(e => e[1] + '× ' + e[0]).join(' || '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed, ${warns} warnings`); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL', e); process.exit(1); });
