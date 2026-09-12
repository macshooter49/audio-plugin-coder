// fb636_envknob_gate.js — THE FILTER'S ENV KNOB IS NEVER DEAD (bugC-b). Boots the REAL index.html with a relay-faithful
// backend (fb635's 817 relays, Tests/fixtures/fb635_relays.json) that ECHOES every page write the way JUCE's
// WebSliderParameterAttachment does (the value lands, then comes back as a valueChanged on the next task — the ONLY way a
// SliderState's listeners fire), serves two of Max's presets in their saved tree's child order
// (Tests/fixtures/fb636_envknob_presets.json), and drives the filter's Env controls with a REAL mouse.
//   node Tests/fb636_envknob_gate.js [page.html]      (default: Source/ui/public/index.html; ~1 min)
// The law: a HAND's first real drag (> 2 px) of a filter's Env knob or its fk-em emblem, on a filter no envelope feeds,
// routes Env 2 (FLT) there with the knob's value as depth — DEPTH first, then DEST, the knob never snapped. Nothing else
// ever routes: not a load, not host automation, not a press or a double-click (a wheel cannot move this knob at all, so [e]
// has no wheel check — it could not go red). A busy Env 2 is never re-targeted.
// [a2t] (fb636 review) repeats [a2] with every relay push in its own task — the knob→depth mirror must wait for the load to settle.
//   ENVK_MUT=knob    wireSlotKnob never claims the route                               → [c] RED
//   ENVK_MUT=emblem  the fk-em emblem never claims the route                           → [g] RED
//   ENVK_MUT=order   DEST first through setDest (the knob snaps to the old depth)      → [c] RED
//   ENVK_MUT=steal   a busy Env 2 is re-targeted to this filter                        → [f] RED
//   ENVK_MUT=down    the route is claimed on pointerdown, before any move              → [e] RED
//   ENVK_MUT=listen  the knob's valueChanged mirror claims the route (load/automation) → [b] RED (then [e] [c] [g] [h] [a2] [a2t]:
//                    the claimed route disturbs every later bar; [a] stays GREEN — the boot writes nothing even so)
//   ENVK_MUT=slot    the emblem claims for Filter 1 whatever slot it edits             → [h] RED
//   ENVK_MUT=hint    a busy Env 2 shows no hint                                        → [f] RED
//   ENVK_MUT=eager   the knob→depth mirror runs on the knob's own event again (pre-fb636) → [a2] RED (reversed order)
//   ENVK_MUT=task    the mirror runs one task later (the first fb636 page), not at the settle → [a2t] RED
//   ENVK_MUT=burst   the settle ignores the other relays (a bare FK_SETTLE_MS debounce)     → [a2t] RED
// Exit 0 = every bar green · 1 = a bar red · 2 = a mutation anchor missing or a crash (the control tested nothing).
const fs = require('fs'), path = require('path'), os = require('os');
const puppeteer = require(path.join(__dirname, 'node_modules', 'puppeteer-core'));
process.on('uncaughtException', e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
process.on('unhandledRejection', e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
const PAGE0 = path.resolve(process.argv[2] || process.env.PAGE || path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html'));
const MUT = process.env.ENVK_MUT || '';
const PAGE = (function () { if (!MUT) return PAGE0;
  let s = fs.readFileSync(PAGE0, 'utf8');
  const sub = (a, b) => { if (s.split(a).length !== 2) { console.log('  MUTATION anchor not unique: ' + a.slice(0, 80)); process.exit(2); } s = s.replace(a, b); };
  if (MUT === 'knob')   sub("if (first && base === 'ENV' && window.__envEnsureFilterRoute) window.__envEnsureFilterRoute (activeFilterSlot);", '');
  else if (MUT === 'emblem') sub("if(e.type==='env'&&window.__envEnsureFilterRoute) window.__envEnsureFilterRoute(e.id.indexOf('SYN_FILTER2_')===0?2:1);", '');
  else if (MUT === 'order')  sub("try{ sp.setNormalisedValue(nv(sf)); }catch(e){}\n          try{ sd.setNormalisedValue(want/7); }catch(e){}",
                                 "setDest('FLT', want);\n          try{ sp.setNormalisedValue(nv(sf)); }catch(e){}");
  else if (MUT === 'steal')  sub('if(cur!==0){ ', 'if(false){ ');
  else if (MUT === 'down')   sub("dragStartN = proxy.getNormalisedValue();", "dragStartN = proxy.getNormalisedValue(); if (base === 'ENV' && window.__envEnsureFilterRoute) window.__envEnsureFilterRoute (activeFilterSlot);");
  else if (MUT === 'listen') sub('function syncDepthNow(f){ var id=envRoutedToFilter(f); if(!id) return;', 'function syncDepthNow(f){ var id=envRoutedToFilter(f); if(!id){ ensureFilterRoute(f); return; }');
  else if (MUT === 'slot')   sub("window.__envEnsureFilterRoute(e.id.indexOf('SYN_FILTER2_')===0?2:1);", 'window.__envEnsureFilterRoute(1);');
  else if (MUT === 'hint')   sub("window.__tiToast('No envelope feeds Filter '", "(function(){})('No envelope feeds Filter '");
  else if (MUT === 'eager' || MUT === 'task') {
    const a = s.indexOf('function syncDepthFromFilterKnob(f){'), e = s.indexOf('}, FK_SETTLE_MS); })(); }', a);
    if (a < 0 || e < 0 || s.split('function syncDepthFromFilterKnob(f){').length !== 2) { console.log('  MUTATION anchor not found: syncDepthFromFilterKnob'); process.exit(2); }
    s = s.slice(0, a) + (MUT === 'eager' ? 'function syncDepthFromFilterKnob(f){ syncDepthNow(f); }'
      : 'function syncDepthFromFilterKnob(f){ if(_fkSyncDue[f]) return; _fkSyncDue[f]=true; setTimeout(function(){ _fkSyncDue[f]=false; syncDepthNow(f); }, 0); }')
      + s.slice(e + '}, FK_SETTLE_MS); })(); }'.length); }
  else if (MUT === 'burst')  sub('function fkNoteRelay(nm){ if(_fkSeen && !_fkSeen[nm]){ _fkSeen[nm]=1; _fkSeenN++; } }', 'function fkNoteRelay(nm){ }');
  else { console.log('  unknown ENVK_MUT ' + MUT); process.exit(2); }
  const f = path.join(os.tmpdir(), 'fb636_envknob_' + MUT + '.html'); fs.writeFileSync(f, s); return f; })();

const FX = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'fb636_envknob_presets.json')));
const RNG635 = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'fb635_restale_presets.json'))).rng;
const RELAYS = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'fb635_relays.json')));
// the link's own ranges from the layout (PluginProcessor.cpp: envDestChoices 0..7 · depth/-knob -1..+1) — fb635's table was
// measured from what the bank HOLDS (DEST 0..2), which would mis-normalise Filter 2 / Mod / Pitch.
const RNG = Object.assign({}, RNG635);
['2', '3', '4', '5'].forEach(k => { RNG['SYN_ENV' + k + '_DEST'] = { start: 0, end: 7, skew: 1, interval: 1 }; RNG['SYN_ENV' + k + '_DEPTH'] = { start: -1, end: 1, skew: 1, interval: 0 }; });
RNG.SYN_FILTER1_ENV = RNG.SYN_FILTER2_ENV = { start: -1, end: 1, skew: 1, interval: 0 };
const LINK = ['SYN_ENV2_DEST', 'SYN_ENV3_DEST', 'SYN_ENV4_DEST', 'SYN_ENV5_DEST', 'SYN_ENV2_DEPTH', 'SYN_ENV3_DEPTH', 'SYN_ENV4_DEPTH', 'SYN_ENV5_DEPTH', 'SYN_FILTER1_ENV', 'SYN_FILTER2_ENV'];
const preset = (name, mods) => { const P = FX.presets[name]; if (!P) throw new Error('no preset ' + name);
  const list = P.map(([k, v]) => [k, (mods && k in mods) ? mods[k] : v]); return { name, list }; };

const STUB = (A, relays, rng, link) => {
  const H = window.__H = { params: {}, L: {}, writes: [], seq: 0, relays: new Set(relays), link };
  A.list.forEach(([k, v]) => { H.params[k] = v; });
  Math.random = (function () { let s = 12345; return function () { s = (s * 1103515245 + 12345) & 0x7fffffff; return s / 0x7fffffff; }; })();
  const rg = (id) => rng[id] || { start: 0, end: 1, skew: 1, interval: 0 };
  const real = (id) => { const v = H.params[id]; return v == null ? rg(id).start : v; };
  const norm = (id) => { const r = rg(id), v = real(id); const p = (v - r.start) / ((r.end - r.start) || 1); return Math.pow(Math.max(0, Math.min(1, p)), r.skew || 1); };
  const denorm = (id, n) => { const r = rg(id); return Math.pow(Math.max(0, Math.min(1, n)), 1 / (r.skew || 1)) * (r.end - r.start) + r.start; };
  H.real = real;
  const fire = (id, ev) => { (H.L[id] || []).forEach(f => { try { f(ev); } catch (e) { } }); };
  // the HOST moves a parameter (automation, a preset load's replaceState): the relay pushes the new value to the page
  H.host = (name, v) => { H.params[name] = v; if (H.relays.has(name)) fire('__juce__slider' + name, { eventType: 'valueChanged', value: v }); };
  const backend = {
    addEventListener(id, fn) { (H.L[id] = H.L[id] || []).push(fn); return [id, fn]; },
    removeEventListener(t) { try { const a = H.L[t[0]]; const i = a.indexOf(t[1]); if (i >= 0) a.splice(i, 1); } catch (e) { } },
    emitEvent(id, ev) {
      if (id.indexOf('__juce__slider') !== 0) return;
      const name = id.slice(14); if (!H.relays.has(name)) return;
      if (ev.eventType === 'requestInitialUpdate') setTimeout(() => { const r = rg(name);
        fire(id, { eventType: 'propertiesChanged', start: r.start, end: r.end, skew: r.skew, interval: r.interval, name, label: '', numSteps: 100, parameterIndex: 0 });
        fire(id, { eventType: 'valueChanged', value: real(name) }); }, 0);
      else if (ev.eventType === 'valueChanged') {
        const old = real(name), v = +ev.value; H.writes.push({ id: name, v, n: H.seq++ }); H.params[name] = v;
        // WebSliderParameterAttachment: setValueAsPartOfGesture → parameterValueChanged → sendFrontEndValue — the echo
        // (only when the parameter really changed: callIfParameterValueChanged)
        if (old !== v) setTimeout(() => fire(id, { eventType: 'valueChanged', value: real(name) }), 0);
      }
    } };
  const nfT = { getSynParam: (id) => norm(String(id)),
    setSynParam: (id, n) => { id = String(id); H.host(id, denorm(id, +n)); return 'ok'; },
    listPresets: () => JSON.stringify({ banks: [], caps: { banks: 0, presets: 0, unreadable: 0 }, userRoot: '/tmp', factoryRoot: '/f' }),
    getGrainSync: () => ({ enabled: 0, bpm: 120 }), getXYAutoState: () => ({ enabled: 0, mode: 0, speed: 0.5 }),
    getWaterfallView: () => JSON.stringify({ a: false, b: false, c: false, d: false }), getDstTableSrc: () => -1,
    getDistortionCurves: () => '{}', getFavourites: () => '{}' };
  const nf = (n) => (...a) => new Promise(r => { const f = nfT[n]; if (f) { try { return r(f(...a)); } catch (e) { return r(0); } }
    if (/getPresets/i.test(n)) return r('[]'); if (/Json|JSON|Names|Lanes|Shapes|Envs|Mod$|State|Meta|Pills|Vocab|Sel/.test(n)) return r(''); r(0); });
  window.Juce = { getSliderState: null, getNativeFunction: nf, backend };
  (function () { const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true,
    get() { return held; }, set(v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction }); } }); })();
  window.__JUCE__ = { backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '', __juce__platform: [], __juce__registeredGlobalEventIds: [],
    __juce__sliders: relays.slice(), __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  window.prompt = () => null; window.confirm = () => false; window.alert = () => { };
  // loadPatchFromFile: replaceState applies B's PARAMs in the tree's child order (juce_AudioProcessorValueTreeState.cpp) and
  // every relay whose value changed pushes on the message thread, synchronously; rev=true feeds them BACKWARDS (the order
  // must never matter). A PARAM the tree does not carry keeps its value (JUCE 8). Then afterPatchLoad announces the load.
  H.load = (B, rev) => { const L = rev ? B.list.slice().reverse() : B.list; let pushed = 0;
    L.forEach(([k, v]) => { if (H.params[k] === v) return; H.host(k, v); pushed++; });
    setTimeout(() => { try { window.onPatchLoaded && window.onPatchLoaded(JSON.stringify({ name: B.name })); } catch (e) { } }, 0);
    return pushed; };
  // fb636 review — the same load with EVERY RELAY PUSH IN ITS OWN TASK (a setTimeout between pushes). WKWebView runs each
  // evaluateJavascript the native side sends as a separate task, so a page timer booked by one push can run before the next
  // push of the same load; H.load's single synchronous burst could never show that. onPatchLoaded lands in its own task after
  // the last push. Resolves with the push count and the push index of each Env-link parameter.
  H.loadTasks = (B, rev) => new Promise(done => {
    const L = (rev ? B.list.slice().reverse() : B.list).filter(([k, v]) => H.params[k] !== v); let i = 0; const at = {};
    const step = () => {
      if (i < L.length) { const [k, v] = L[i]; if (link.indexOf(k) >= 0) at[k] = i; H.host(k, v); i++; setTimeout(step, 0); return; }
      try { window.onPatchLoaded && window.onPatchLoaded(JSON.stringify({ name: B.name })); } catch (e) { }
      done({ pushed: L.length, at }); };
    setTimeout(step, 0); });
};

const W = ms => new Promise(r => setTimeout(r, ms));
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };
const r4 = x => (x == null ? 'null' : (+x).toFixed(4));

(async () => {
  console.log('══ fb636 ENV-KNOB GATE — A HAND ROUTES ENV 2, NOTHING ELSE DOES ══   page: ' + PAGE + (MUT ? '   mutation: ' + MUT : ''));
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const errors = [];
  async function boot(A) {
    const p = await b.newPage(); await p.setViewport({ width: 1400, height: 900, deviceScaleFactor: 1 });
    p.on('pageerror', e => errors.push(String(e.message || e).slice(0, 200)));
    await p.evaluateOnNewDocument(STUB, A, RELAYS, RNG, LINK);
    await p.goto('file://' + PAGE + '?page=1', { waitUntil: 'load', timeout: 90000 });
    await W(1500);
    await p.evaluate(() => { window.__tiForceActive = true; const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
    await W(Number(process.env.BOOTW || 4500));
    return p;
  }
  const mark = (p) => p.evaluate(() => window.__H.writes.length);
  const since = (p, m) => p.evaluate((m) => window.__H.writes.slice(m), m);
  const val = (p, id) => p.evaluate((id) => window.__H.real(id), id);
  const vals = (p) => p.evaluate(() => { const o = {}; window.__H.link.forEach(k => { o[k] = window.__H.real(k); }); return o; });
  const host = (p, id, v) => p.evaluate((id, v) => window.__H.host(id, v), id, v);
  const linkW = (w, ids) => w.filter(x => (ids || LINK).indexOf(x.id) >= 0);
  const fmtW = (w) => w.map(x => x.id.replace('SYN_', '') + '=' + r4(x.v)).join(' ') || 'none';
  // the controls: side 'back' = the back panel's Env knob (wireSlotKnob), 'emblem' = the front fk-em canvas
  async function target(p, side, slot) {
    return p.evaluate((side, slot) => {
      const dev = document.getElementById('filter-device'); if (!dev) return { err: 'no #filter-device' };
      const pill = document.getElementById(slot === 2 ? 'filter-pill-2' : 'filter-pill-1'); if (pill && !pill.classList.contains('act')) pill.click();
      dev.classList.toggle('swapped', side === 'back');
      let el = null;
      if (side === 'back') { const k = [].slice.call(dev.querySelectorAll('.flt-back-knobs .knob')).filter(k => { const l = k.querySelector('.knob-label'); return l && l.textContent === 'Env'; })[0]; el = k && k.querySelector('.knob-ring'); }
      else el = dev.querySelector('.fk-em canvas[data-em="env"]');
      if (!el) return { err: 'no ' + side + ' Env control' };
      el.scrollIntoView({ block: 'center', inline: 'center' });
      const r = el.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2, hit = document.elementFromPoint(x, y);
      return { x, y, w: r.width, h: r.height, hit: !!(hit && (hit === el || el.contains(hit))), act2: !!(document.getElementById('filter-pill-2') || {}).classList && document.getElementById('filter-pill-2').classList.contains('act') };
    }, side, slot);
  }
  // a hand's drag, dys = the pointer's successive offsets (up = positive); returns the writes after each step
  async function drag(p, t, dys) {
    const steps = []; await p.mouse.move(t.x, t.y); await p.mouse.down(); await W(40);
    for (const dy of dys) { const m = await mark(p); await p.mouse.move(t.x, t.y - dy); await W(60); steps.push({ dy, w: await since(p, m) }); }
    await p.mouse.up(); await W(250); return steps;
  }
  const toast = (p) => p.evaluate(() => { const t = document.querySelector('.mv-toast'); return t ? { txt: t.textContent, on: t.style.opacity === '1' } : null; });
  const clearToast = (p) => p.evaluate(() => { const t = document.querySelector('.mv-toast'); if (t) { t.textContent = ''; t.style.opacity = '0'; } });
  const resetAYN = async (p) => { for (const [k, v] of [['SYN_ENV2_DEST', 0], ['SYN_ENV2_DEPTH', 4.74974513053894e-8], ['SYN_FILTER1_ENV', -0.08799995481967926], ['SYN_FILTER2_ENV', 4.74974513053894e-8]]) await host(p, k, v); await W(300); };
  const AYN = preset('All You Need'), PAULO = preset('Paulo');
  const V0 = -0.08799995481967926;   // All You Need's stored Filter 1 Env

  // ── BOOT 1: All You Need (no envelope feeds either filter) ──
  let p = await boot(AYN);
  const w0 = await since(p, 0), v0 = await vals(p);
  const front4 = await p.evaluate(() => !!document.querySelector('#syn-panel .device.filter .filter-knobs .knob:nth-child(4)'));
  gate(linkW(w0).length === 0 && Math.abs(v0.SYN_FILTER1_ENV - V0) < 1e-9 && v0.SYN_ENV2_DEST === 0,
    '[a] ALL YOU NEED BOOTS WITH ZERO WRITES to the Env link (ENV2-5 DEST/DEPTH, FILTER1/2_ENV) — its -0.088 is not mirrored into a route',
    `link writes: ${fmtW(linkW(w0))} · every write on boot: ${w0.length} · stored F1 Env ${r4(v0.SYN_FILTER1_ENV)} ENV2 DEST ${v0.SYN_ENV2_DEST} · front .filter-knobs Env knob present: ${front4} (the fk-em emblems replace it)`);
  let m = await mark(p);
  for (const v of [0.3, -0.5, 0.75, V0]) { await host(p, 'SYN_FILTER1_ENV', v); await W(80); }
  for (const v of [0.4, V0 === 0 ? 0.1 : -0.2, 4.74974513053894e-8]) { await host(p, 'SYN_FILTER2_ENV', v); await W(80); }
  await W(300);
  const wb = linkW(await since(p, m));
  gate(wb.length === 0, '[b] HOST AUTOMATION of SYN_FILTER1_ENV / SYN_FILTER2_ENV on an unrouted patch writes nothing (no DEST, no DEPTH)', `link writes: ${fmtW(wb)}`);
  // [e] a press with no move · a double-click (reset to centre) · a wheel — none of them is a hand's drag
  let t = await target(p, 'back', 1); if (t.err) { console.log('  ANCHOR ' + t.err); process.exit(2); }
  m = await mark(p);
  await p.mouse.move(t.x, t.y); await p.mouse.down(); await W(80); await p.mouse.up(); await W(200);
  const wPress = linkW(await since(p, m)); m = await mark(p);
  await p.mouse.move(t.x, t.y - 2); await p.mouse.down(); await p.mouse.move(t.x, t.y - 4); await W(40); await p.mouse.up(); await W(200);   // 2 px: under the threshold
  const wSmall = linkW(await since(p, m)); await W(450); m = await mark(p);
  await p.evaluate((x, y) => { const el = document.elementFromPoint(x, y); el && el.dispatchEvent(new MouseEvent('dblclick', { bubbles: true, cancelable: true, clientX: x, clientY: y })); }, t.x, t.y);
  await W(250);
  const wDbl = linkW(await since(p, m));
  // fb636 review — no WHEEL sub-check: the back Env knob has no wheel handler (a wheel writes nothing, not even the knob),
  // so that check was green by construction and no control could redden it.
  const e2 = await vals(p);
  gate(wPress.length === 0 && wSmall.filter(x => /ENV[2-5]_/.test(x.id)).length === 0 && wDbl.filter(x => /ENV[2-5]_/.test(x.id)).length === 0
       && wDbl.some(x => x.id === 'SYN_FILTER1_ENV' && Math.abs(x.v) < 1e-9) && e2.SYN_ENV2_DEST === 0,
    '[e] A PRESS, A 2 px NUDGE AND A DOUBLE-CLICK (it DID reset the knob to centre) route nothing',
    `press ${fmtW(wPress)} · 2px ${fmtW(wSmall)} · dblclick ${fmtW(wDbl)} · ENV2 DEST now ${e2.SYN_ENV2_DEST}`);
  // [c] the back Env knob, Filter 1, from All You Need's stored -0.088: the first REAL move routes Env 2 → Filter 1
  await resetAYN(p); t = await target(p, 'back', 1);
  let st = await drag(p, t, [1, 6, 20, 45]);
  let after = await vals(p); await W(300); after = await vals(p);
  const k6 = V0 + 2 * (6 / 200);   // wireSlotKnob: n = n0 + dy/200 on the bipolar -1..+1 knob
  const s1 = st[0].w, s6 = st[1].w;
  const iD = s6.findIndex(x => x.id === 'SYN_ENV2_DEPTH'), iT = s6.findIndex(x => x.id === 'SYN_ENV2_DEST');
  const kn6 = s6.filter(x => x.id === 'SYN_FILTER1_ENV').map(x => x.v);
  const knAll = [].concat(...st.map(s => s.w)).filter(x => x.id === 'SYN_FILTER1_ENV').map(x => x.v);
  const destW = [].concat(...st.map(s => s.w)).filter(x => x.id === 'SYN_ENV2_DEST');
  gate(s1.filter(x => /ENV[2-5]_/.test(x.id)).length === 0 && iD >= 0 && iT > iD && Math.abs(s6[iD].v - k6) < 1e-6 && kn6.length && Math.abs(kn6[0] - k6) < 1e-6
       && Math.abs(s6[iT].v - 2) < 1e-9 && destW.length === 1 && knAll.every(v => Math.abs(v) > 0.01) && after.SYN_ENV2_DEST === 2 && Math.abs(after.SYN_ENV2_DEPTH - after.SYN_FILTER1_ENV) < 1e-6
       && Math.abs(after.SYN_FILTER1_ENV - (V0 + 2 * 45 / 200)) < 1e-6 && Math.abs(after.SYN_FILTER2_ENV - 4.74974513053894e-8) < 1e-12,
    '[c] A DRAG ON THE BACK ENV KNOB (Filter 1, no route) routes Env 2 → Filter 1 at the FIRST REAL MOVE: DEPTH = the moved knob (from All You Need\'s -0.088), THEN DEST, once; the knob never snaps; the depth follows the rest of the drag',
    `1px: ${fmtW(s1)} · 6px: ${fmtW(s6)} (want DEPTH ${r4(k6)} before DEST=2) · knob writes ${knAll.map(r4).join(',')} · after: DEST ${after.SYN_ENV2_DEST} DEPTH ${r4(after.SYN_ENV2_DEPTH)} F1 ${r4(after.SYN_FILTER1_ENV)} F2 ${r4(after.SYN_FILTER2_ENV)} · hit ${t.hit}`);
  // [g] the fk-em emblem, Filter 1
  await resetAYN(p); t = await target(p, 'emblem', 1);
  st = await drag(p, t, [1, 5, 30]); await W(300); after = await vals(p);
  const g5 = st[1].w, gD = g5.findIndex(x => x.id === 'SYN_ENV2_DEPTH'), gT = g5.findIndex(x => x.id === 'SYN_ENV2_DEST'), gk = g5.filter(x => x.id === 'SYN_FILTER1_ENV').map(x => x.v);
  gate(st[0].w.filter(x => /ENV[2-5]_/.test(x.id)).length === 0 && gD >= 0 && gT > gD && gk.length && Math.abs(g5[gD].v - gk[gk.length - 1]) < 1e-6 && Math.abs(gk[0] - V0) > 1e-4 && Math.abs(gk[0] - V0) < 0.1
       && after.SYN_ENV2_DEST === 2 && Math.abs(after.SYN_ENV2_DEPTH - after.SYN_FILTER1_ENV) < 1e-6 && Math.abs(after.SYN_FILTER2_ENV - 4.74974513053894e-8) < 1e-12,
    '[g] A DRAG ON THE fk-em ENV EMBLEM (Filter 1) routes Env 2 → Filter 1 at its first real move (> 2 px), DEPTH before DEST, from the stored -0.088',
    `1px: ${fmtW(st[0].w)} · 5px: ${fmtW(g5)} · after: DEST ${after.SYN_ENV2_DEST} DEPTH ${r4(after.SYN_ENV2_DEPTH)} F1 ${r4(after.SYN_FILTER1_ENV)} · hit ${t.hit}`);
  // [h] slot 2: the emblem and the back knob route to FILTER 2 and leave Filter 1 alone
  await resetAYN(p); t = await target(p, 'emblem', 2);
  st = await drag(p, t, [1, 5, 30]); await W(300); const h1 = await vals(p);
  await resetAYN(p); t = await target(p, 'back', 2);
  const st2 = await drag(p, t, [1, 6, 30]); await W(300); const h2 = await vals(p);
  gate(t.act2 && h1.SYN_ENV2_DEST === 3 && Math.abs(h1.SYN_ENV2_DEPTH - h1.SYN_FILTER2_ENV) < 1e-6 && Math.abs(h1.SYN_FILTER1_ENV - V0) < 1e-9
       && h2.SYN_ENV2_DEST === 3 && Math.abs(h2.SYN_ENV2_DEPTH - h2.SYN_FILTER2_ENV) < 1e-6 && Math.abs(h2.SYN_FILTER1_ENV - V0) < 1e-9,
    '[h] ON SLOT 2 the emblem (its slot from e.id) and the back knob route Env 2 → FILTER 2, and Filter 1\'s Env is untouched',
    `emblem: DEST ${h1.SYN_ENV2_DEST} DEPTH ${r4(h1.SYN_ENV2_DEPTH)} F2 ${r4(h1.SYN_FILTER2_ENV)} F1 ${r4(h1.SYN_FILTER1_ENV)} · knob: DEST ${h2.SYN_ENV2_DEST} DEPTH ${r4(h2.SYN_ENV2_DEPTH)} F2 ${r4(h2.SYN_FILTER2_ENV)} F1 ${r4(h2.SYN_FILTER1_ENV)}`);
  await p.close();

  // ── BOOT 2: Env 2 is BUSY (→ Filter 2, then Pitch, then Mod 1): turning Filter 1's Env routes nothing and says why ──
  p = await boot(preset('All You Need', { SYN_ENV2_DEST: 3 }));
  const busy = [];
  for (const [d, side] of [[3, 'back'], [3, 'emblem'], [7, 'back'], [5, 'emblem']]) {
    if ((await val(p, 'SYN_ENV2_DEST')) !== d) { await host(p, 'SYN_ENV2_DEST', d); await W(200); }
    await clearToast(p); t = await target(p, side, 1); m = await mark(p);
    await drag(p, t, [1, 6, 25]); await W(200);
    const w = await since(p, m), tt = await toast(p), v = await vals(p);
    busy.push({ d, side, w: w.filter(x => ['SYN_ENV2_DEST', 'SYN_ENV2_DEPTH', 'SYN_FILTER2_ENV', 'SYN_ENV3_DEST', 'SYN_ENV4_DEST', 'SYN_ENV5_DEST'].indexOf(x.id) >= 0),
                moved: w.some(x => x.id === 'SYN_FILTER1_ENV'), tt, dest: v.SYN_ENV2_DEST });
  }
  gate(busy.every(x => x.w.length === 0 && x.moved && x.dest === x.d && x.tt && x.tt.on && /Env 2/.test(x.tt.txt) && /Filter 1/.test(x.tt.txt)),
    '[f] ENV 2 BUSY (Filter 2 · Pitch · Mod 1): Filter 1\'s knob and emblem still turn, but neither SYN_ENV2_DEST/DEPTH nor SYN_FILTER2_ENV (nor any other env) is written — the route hint shows instead',
    busy.map(x => `dest ${x.d} ${x.side}: writes ${fmtW(x.w)} · knob moved ${x.moved} · hint "${x.tt && x.tt.txt}" ${x.tt && x.tt.on ? 'on' : 'off'}`).join(' · '));
  await p.close();

  // ── BOOT 3: Paulo (Env 2 → Filter 1 already) ──
  p = await boot(PAULO);
  const pw0 = linkW(await since(p, 0)); m = await mark(p);
  for (const v of [0.3, -0.5, 4.74974513053894e-8]) { await host(p, 'SYN_FILTER1_ENV', v); await W(120); }
  await W(300);
  const pwb = linkW(await since(p, m));
  gate(pw0.length === 0 && pwb.filter(x => /_DEST$/.test(x.id)).length === 0,
    '[b2] PAULO boots with zero link writes, and automating its (routed) Filter 1 Env writes NO DEST (the existing mirror may move the depth)',
    `boot: ${fmtW(pw0)} · automation: ${fmtW(pwb)}`);
  t = await target(p, 'back', 1); m = await mark(p);
  await drag(p, t, [1, 6, 30]); await W(300);
  const dw = await since(p, m), dv = await vals(p);
  gate(dw.filter(x => /_DEST$/.test(x.id)).length === 0 && dv.SYN_ENV2_DEST === 2 && Math.abs(dv.SYN_ENV2_DEPTH - dv.SYN_FILTER1_ENV) < 1e-6,
    '[d] ENV 2 ALREADY → FILTER 1 (Paulo): the drag leaves every DEST untouched; the depth follows the knob as before',
    `writes ${fmtW(linkW(dw))} · DEST ${dv.SYN_ENV2_DEST} DEPTH ${r4(dv.SYN_ENV2_DEPTH)} F1 ${r4(dv.SYN_FILTER1_ENV)}`);
  await p.close();

  // ── BOOTS 4-5: preset transitions Paulo ⇄ All You Need, relays pushed in the TREE's order and REVERSED ──
  const trans = [];
  for (const rev of [false, true]) {
    p = await boot(PAULO);
    for (const [B, lbl] of [[AYN, 'Paulo→All You Need'], [PAULO, 'All You Need→Paulo']]) {
      m = await mark(p); const pushed = await p.evaluate((B, rev) => window.__H.load(B, rev), B, rev); await W(900);
      const w = await since(p, m), v = await vals(p);
      trans.push({ lbl: lbl + (rev ? ' (reversed)' : ' (tree order)'), pushed, env2: w.filter(x => /^SYN_ENV2_/.test(x.id)), link: linkW(w),
                   dest: v.SYN_ENV2_DEST, depth: v.SYN_ENV2_DEPTH, want: B.list.filter(([k]) => k === 'SYN_ENV2_DEPTH')[0][1] });
    }
    await p.close();
  }
  gate(trans.every(x => x.env2.length === 0 && Math.abs(x.depth - x.want) < 1e-9),
    '[a2] PAULO ⇄ ALL YOU NEED, relays in the tree\'s order AND reversed: no ENV2 write, and ENV2_DEPTH stays what the loaded preset stored',
    trans.map(x => `${x.lbl}: ${x.pushed} pushed · ENV2 writes ${fmtW(x.env2)} · link writes ${fmtW(x.link)} · depth ${r4(x.depth)} (stored ${r4(x.want)})`).join('\n        '));

  // ── BOOTS 6-7: the same transitions, EACH RELAY PUSH IN ITS OWN TASK (H.loadTasks) ──
  const transT = [];
  for (const rev of [false, true]) {
    p = await boot(PAULO);
    for (const [B, lbl] of [[AYN, 'Paulo→All You Need'], [PAULO, 'All You Need→Paulo']]) {
      m = await mark(p); const r = await p.evaluate((B, rev) => window.__H.loadTasks(B, rev), B, rev); await W(900);
      const w = await since(p, m), v = await vals(p);
      transT.push({ lbl: lbl + (rev ? ' (reversed)' : ' (tree order)'), pushed: r.pushed, at: r.at, env2: w.filter(x => /^SYN_ENV2_/.test(x.id)), link: linkW(w),
                    depth: v.SYN_ENV2_DEPTH, want: B.list.filter(([k]) => k === 'SYN_ENV2_DEPTH')[0][1] });
    }
    await p.close();
  }
  const atS = (a) => Object.keys(a).map(k => k.replace('SYN_', '') + '#' + a[k]).join(' ') || 'no link push';
  gate(transT.every(x => x.env2.length === 0 && Math.abs(x.depth - x.want) < 1e-9),
    '[a2t] THE SAME TRANSITIONS WITH EVERY RELAY PUSH IN ITS OWN TASK (a page timer can run between two pushes of one load): no ENV2 write, ENV2_DEPTH stays what the loaded preset stored',
    transT.map(x => `${x.lbl}: ${x.pushed} pushed (${atS(x.at)}) · ENV2 writes ${fmtW(x.env2)} · link writes ${fmtW(x.link)} · depth ${r4(x.depth)} (stored ${r4(x.want)})`).join('\n        '));

  await b.close();
  if (errors.length) console.log('  page errors: ' + errors.length + ' · ' + errors.slice(0, 3).join(' | '));
  console.log(`\n  ${pass}/${pass + fail} PASS`);
  process.exit(fail ? 1 : 0);
})();
