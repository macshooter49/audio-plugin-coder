// fb635_restale_harness.js (ported from the fb635 sweep investigator's harness) — boots the REAL index.html with a relay-faithful backend that serves a real
// .terrain preset's PARAM values + root-property blobs, then either (fresh) snapshots the page or
// (load) boots preset A and loads preset B exactly as C++ loadPatchFromFile + afterPatchLoad would.
//   node harness.js fresh <B> <out.json>
//   node harness.js load  <A> <B> <out.json>
const path = require('path'), fs = require('fs');
const T = path.join(__dirname, '..');
const puppeteer = require(path.join(__dirname, 'node_modules', 'puppeteer-core'));
const PAGE = process.env.PAGE || (T + '/Source/ui/public/index.html');
const FX = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'fb635_restale_presets.json')));
const ALL = FX.presets;
const RELAYS = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'fb635_relays.json')));

// ── per-param range: precomputed in the fixture from all 50 presets (the investigator's law, run once) ──
function rng(id) { return FX.rng[id] || { start: 0, end: 1, skew: 1, interval: 0 }; }
function pack(name) {
  const P = ALL[name]; if (!P) throw new Error('no preset ' + name);
  const R = {}; for (const k of Object.keys(P.params)) R[k] = rng(k);
  const MOD = (JSON.parse(process.env.MODS || '{}'))[name] || {}; const params = Object.assign({}, P.params, MOD);
  return { name, params, props: P.props, meta: P.man, rng: R };
}

const STUB = (A, relays, host) => {
  const H = window.__H = { cur: A, relays: new Set(relays), L: {}, calls: [], feedT: null };
  H.host = !! host; H.B0 = JSON.parse(JSON.stringify(A.params));   /* fb635 — what the booted preset STORED: the no-write bar diffs against it */
  Math.random = (function () { let s = 12345; return function () { s = (s * 1103515245 + 12345) & 0x7fffffff; return s / 0x7fffffff; }; })();
  const rg = (id) => H.cur.rng[id] || { start: 0, end: 1, skew: 1, interval: 0 };
  const real = (id) => { const v = H.cur.params[id]; return v == null ? rg(id).start : v; };
  const norm = (id) => { const r = rg(id), v = real(id); const p = (v - r.start) / ((r.end - r.start) || 1); return Math.pow(Math.max(0, Math.min(1, p)), r.skew || 1); };
  const denorm = (id, n) => { const r = rg(id); return Math.pow(Math.max(0, Math.min(1, n)), 1 / (r.skew || 1)) * (r.end - r.start) + r.start; };
  H.norm = norm; H.real = real;
  const fire = (id, ev) => { (H.L[id] || []).forEach(f => { try { f(ev); } catch (e) { } }); };
  H.fire = fire;
  const backend = {
    addEventListener(id, fn) { (H.L[id] = H.L[id] || []).push(fn); return [id, fn]; },
    removeEventListener(t) { try { const a = H.L[t[0]]; const i = a.indexOf(t[1]); if (i >= 0) a.splice(i, 1); } catch (e) { } },
    emitEvent(id, ev) {
      if (id.indexOf('__juce__slider') !== 0) return;
      const name = id.slice(14); if (!H.relays.has(name)) return;   // no relay → the real backend never answers
      if (ev.eventType === 'requestInitialUpdate') setTimeout(() => { const r = rg(name);
        fire(id, { eventType: 'propertiesChanged', start: r.start, end: r.end, skew: r.skew, interval: r.interval, name, label: '', numSteps: 100, parameterIndex: 0 });
        fire(id, { eventType: 'valueChanged', value: real(name) }); }, 0);
      else if (ev.eventType === 'valueChanged') { H.cur.params[name] = +ev.value; }
    } };
  const P = () => H.cur.props;
  const cards = () => { try { return JSON.parse(P().cardStates || '{}') || {}; } catch (e) { return {}; } };
  const f0 = (k, d) => { const v = P()[k]; return v == null || v === '' ? d : +v; };
  const NAT = {
    getSynParam: (id) => norm(String(id)),
    setSynParam: (id, n) => { id = String(id); H.cur.params[id] = denorm(id, +n); if (H.relays.has(id)) fire('__juce__slider' + id, { eventType: 'valueChanged', value: H.cur.params[id] }); return 'ok'; },
    getSynthLfoShapes: () => P().lfoShapesJson || '', setSynthLfoShapes: (j) => { P().lfoShapesJson = String(j); return 0; },
    getSynthEnvs: () => P().dynEnvJson || '', setSynthEnvs: (j) => { P().dynEnvJson = String(j); return 0; },
    getSynthMod: () => P().synModJson || '', setSynthMod: (j) => { P().synModJson = String(j); return 0; },
    getModState: () => P().modStateJson || '',
    getArpLanes: () => P().arpLanesJson || '', setArpLanes: (j) => { P().arpLanesJson = String(j); return 0; },
    getCardState: (id) => cards()[String(id)] || '',
    setCardState: (id, j) => { const c = cards(); c[String(id)] = String(j); P().cardStates = JSON.stringify(c); return 0; },
    getMacroNames: () => P().macroNames || '', getPresetPills: () => P().presetPills || '', setPresetPills: (j) => { P().presetPills = String(j); return 'ok'; },
    getDistortionCurves: () => P().dstCurvesJson || '{}', setDistortionCurves: (j) => { P().dstCurvesJson = String(j); return 0; },
    getDstTableSrc: () => f0('dstTableSrc', -1),
    getWaterfallView: () => JSON.stringify({ a: !!f0('wt3dView0', 0), b: !!f0('wt3dView1', 0), c: !!f0('wt3dView2', 0), d: !!f0('wt3dView3', 0) }),
    getNoiseSampleSel: () => P().noiseSampleSel || '',
    getTapeEnabled: () => f0('tapeEnabled', 1), getTapeLoopEnabled: () => f0('tapeLoopEnabled', 1), getGrainEngineEnabled: () => f0('grainEngineEnabled', 1),
    getChorusEnabled: () => f0('chorusEnabled', 1), getDelayEnabled: () => f0('delayEnabled', 1), getWireTubeSat: () => f0('wireTubeSat', 0), getWireSpaceNoise: () => f0('wireSpaceNoise', 0),
    getTapeLinked: () => f0('tapeLinkEnabled', 0), getTapeMachine: () => 0, getTapeLoopRecord: () => 0, getTapeLoopPlay: () => 0,
    getGrainSync: () => ({ enabled: f0('grainSyncEnabled', 0), bpm: 120 }), getXYAutoState: () => ({ enabled: f0('xyAutoEnabled', 0), mode: f0('xyAutoMode', 0), speed: f0('xyAutoSpeed', 0.5) }),
    getXYEnabled: () => 0, getEqPanelOpen: () => 0, getSettings: () => '',
    getPresetMeta: () => JSON.stringify(H.cur.meta || {}),
    listPresets: () => JSON.stringify({ banks: [], caps: { banks: 0, presets: 0, unreadable: 0 }, userRoot: '/tmp', factoryRoot: '/f' }),
    getFavourites: () => '{}', getVocab: () => '', getPoppedCards: () => '',
  };
  H.NAT = NAT;
  const nf = (n) => (...a) => new Promise(r => { H.calls.push(n); const f = NAT[n]; if (f) { try { return r(f(...a)); } catch (e) { return r(0); } }
    if (/getPresets/i.test(n)) return r('[]'); if (/Json|JSON|Names|Lanes|Shapes|Envs|Mod$/.test(n)) return r(''); r(0); });
  window.Juce = { getSliderState: null, getNativeFunction: nf, backend };
  (function () { const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true,
    get() { return held; }, set(v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction }); } }); })();
  window.__JUCE__ = { backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__platform: [], __juce__registeredGlobalEventIds: [],
    __juce__sliders: relays.slice(), __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  window.prompt = () => null; window.confirm = () => false; window.alert = () => { };
  // the C++ frame feed's macro base values (outside the quiet gate)
  H.feedT = setInterval(() => { try { const m = []; for (let i = 1; i <= 9; i++) m.push(norm('SYN_MACRO_' + i)); window.__mvMacroBase = m; } catch (e) { } }, 50);
  // ── loadPatchFromFile + afterPatchLoad, in the order the C++ does it ──
  H.switchTo = function (B) {
    const old = H.cur; H.cur = B; H.B0 = JSON.parse(JSON.stringify(B.params));   /* fb635 — what B stored, before the page touches anything */
    // 1) setStateInformation → APVTS → every relay whose value changed pushes (message thread, synchronous)
    let pushed = 0;
    for (const name of H.relays) { const id = '__juce__slider' + name; if (!(H.L[id] && H.L[id].length)) continue;
      const a = old.params[name], b = B.params[name]; if (a === b) continue; fire(id, { eventType: 'valueChanged', value: real(name) }); pushed++; }
    H.pushed = pushed;
    // 2) the editor timer's version relays fire on the next tick
    setTimeout(() => { try { const dj = P().dstCurvesJson || '{}'; if (window.__crvXApply) window.__crvXApply(JSON.parse(dj)); } catch (e) { }
      try { window.__tiModRestore && window.__tiModRestore(); } catch (e) { } }, 16);
    // 3) afterPatchLoad
    setTimeout(() => {
      ['a', 'b', 'c', 'd'].forEach((l, i) => { try { if (! P()['oscAsset' + i]) window.onOscSampleCleared && window.onOscSampleCleared(l); } catch (e) { }   /* fb635 — the C++ clears only an EMPTY payload */
        try { window.onBlendState && window.onBlendState(l, false); } catch (e) { }
        try { if (P()['wtAsset' + i]) window.onWavetableImported && window.onWavetableImported(l, P()['wtImportName' + i] || ''); else window.onWavetableImportCleared && window.onWavetableImportCleared(l); } catch (e) { } });
      try { if (!P().noiseSampleSel) window.onNoiseSampleCleared && window.onNoiseSampleCleared(); } catch (e) { }
      try { window.onPatchLoaded && window.onPatchLoaded(JSON.stringify(B.meta || {}), H.host ? 'host' : undefined); } catch (e) { H.err = String(e); }   /* fb635 — HOST=1: the editor timer's host announcement */
    }, 0);
  };
};

// ── what the page SHOWS: every element's classes, own text, key attributes, and inline geometry ──
const SNAP = () => {
  const out = {};
  const key = (el) => { const parts = []; let e = el;
    while (e && e !== document.body) { if (e.id) { parts.unshift('#' + e.id); break; } { const cx = (e.getAttribute('class') || '').match(/\b([a-z]+-ext)\b/); if (cx && /\bti-card\b/.test(e.getAttribute('class'))) { parts.unshift('@' + cx[1]); break; } }
      const p = e.parentElement; const i = p ? Array.prototype.indexOf.call(p.children, e) : 0;
      const c = (e.getAttribute('class') || '').split(/\s+/).filter(x => x && !/^(act|on|off|active|sel|show|hidden|dim|open|cur|lit|live)$/.test(x))[0] || '';
      parts.unshift(e.tagName.toLowerCase() + (c ? '.' + c : '') + ':' + i); e = p; }
    return parts.join('>'); };
  const all = document.body.querySelectorAll('*');
  for (const el of all) {
    const tn = el.tagName; if (tn === 'SCRIPT' || tn === 'STYLE') continue;
    const k = key(el); const r = {};
    const cls = el.getAttribute('class'); if (cls) r.c = cls.split(/\s+/).sort().join(' ');
    let t = ''; for (const n of el.childNodes) if (n.nodeType === 3) t += n.nodeValue; t = t.trim(); if (t) r.t = t.slice(0, 80);
    for (const a of ['d', 'data-v', 'aria-pressed', 'x1', 'x2', 'y1', 'y2', 'cx', 'cy', 'points', 'transform', 'data-mode', 'data-eng', 'fill', 'stroke', 'opacity', 'stroke-dasharray', 'stroke-dashoffset', 'width', 'height']) { const v = el.getAttribute(a); if (v != null) r[a] = String(v).slice(0, 120); }
    if (el.style && el.style.cssText) r.s = el.style.cssText.slice(0, 200);
    if (tn === 'INPUT' || tn === 'SELECT') r.v = String(el.value);
    if (tn === 'CANVAS' && el.width * el.height > 0 && el.width * el.height < 400000) { try { const x = el.getContext('2d'); if (x) { const d = x.getImageData(0, 0, el.width, el.height).data; let h = 0, nz = 0; for (let i = 0; i < d.length; i += 16) { h = (h * 31 + d[i] + d[i + 1] * 3 + d[i + 3] * 7) | 0; if (d[i + 3]) nz++; } r.cv = h + '/' + nz; } } catch (e) { } }
    out[k] = r;
  }
  const js = {};
  const T = (n, f) => { try { js[n] = JSON.stringify(f()); } catch (e) { js[n] = 'ERR ' + e.message; } };
  T('routes', () => window.__tiRoutes && window.__tiRoutes());
  T('macros', () => [0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => window.__macroVal && Math.round(window.__macroVal(i))));
  T('bend', () => window.__bendRange && window.__bendRange());
  T('lfoTab', () => window.__lfoCurTab && window.__lfoCurTab());
  T('sectionEnabled', () => state.sectionEnabled);
  T('chorusEnabled', () => state.chorusEnabled); T('delayEnabled', () => state.delayEnabled);
  T('xy', () => [state.xyAutoPlay, state.xyAutoMode, state.xyAutoSpeed]); T('grainSync', () => state.grainSyncEnabled); T('tapeLinked', () => state.tapeLinked);
  T('pills', () => { const o = {}; ['arp', 'gli', 'rbn', 'chop', 'lfo', 'crv'].forEach(k => o[k] = window.__tiPills && window.__tiPills.get(k)); return o; });
  T('pushed', () => window.__H.pushed);
  return { dom: out, js };
};

(async () => {
  const [mode, a1, a2, a3] = process.argv.slice(2);
  const A = pack(mode === 'fresh' ? a1 : a1), B = mode === 'load' ? pack(a2) : null, OUT = mode === 'load' ? a3 : a2;
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage(); await p.setViewport({ width: 1100, height: 760, deviceScaleFactor: 1 });
  const errors = []; p.on('pageerror', e => errors.push(String(e.message || e).slice(0, 200)));
  await p.evaluateOnNewDocument(STUB, A, RELAYS, process.env.HOST === '1');
  await p.goto('file://' + PAGE + '?page=1', { waitUntil: 'load', timeout: 90000 });
  const W = ms => new Promise(r => setTimeout(r, ms));
  await W(1600);
  await p.evaluate(() => { window.__tiForceActive = true; const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
  await W(Number(process.env.BOOTW || 6000));
  if (process.env.PRE) await p.evaluate(new Function(process.env.PRE));
  if (B) { await p.evaluate((B) => window.__H.switchTo(B), B); await W(Number(process.env.LOADW || 6000)); }
  if (process.env.POST) await p.evaluate(new Function(process.env.POST));
  const snap = await p.evaluate(SNAP); snap.errors = errors;
  if (process.env.EXTRA) { try { snap.extra = await p.evaluate(new Function('return (' + process.env.EXTRA + ')')); } catch (e) { snap.extra = 'ERR ' + e.message; } console.log('EXTRA', JSON.stringify(snap.extra).slice(0, 4000)); }
  if (process.env.SHOT) await p.screenshot({ path: process.env.SHOT });
  fs.writeFileSync(OUT, JSON.stringify(snap));
  console.log('wrote', OUT, Object.keys(snap.dom).length, 'elements · errors', errors.length, errors.slice(0, 3).join(' | '));
  await b.close();
})().catch(e => { console.error(e); process.exit(1); });
