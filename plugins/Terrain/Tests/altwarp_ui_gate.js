// altwarp_ui_gate — fb636 ALT WARP · THE PAGE SIDE: THE MENU IN SERUM 2's ORDER, THE 12 O'CLOCK PICK, THE READOUTS.
//
//   NODE_PATH=Tests/node_modules node Tests/altwarp_ui_gate.js [page.html]      (from plugins/Terrain)
//   AWU_MUTATE=<control> ...                                                     (must go red on its bar)
//
// The eight Alt modes (39-46) are live in the DSP; this is everything the page owes them. Max's answers
// (altwarp_design.json maxAnswers): the menu files them as Serum does; "jump to 50% only on a menu pick of a
// centred mode (41, 44, 46) when the knob is at 0; never on preset load or automation"; Terrain's Bend (1) and
// Skew (5) keep their names; 47 stays reserved.
//
// 🚨 THE STUB IS JUCE'S OWN SEMANTICS, NOT A KINDER ONE (fb393). js/juce/index.js:178 — a slider state's
//   setNormalisedValue EMITS TO THE BACKEND AND FIRES NO LOCAL LISTENER. warp_menu's stub fires them, which
//   would hide a knob that never repaints after a pick. Here a local write only records an emit, and
//   window.__push(name, value) is the BACKEND (automation, preset load, state restore): it sets the value and
//   fires the listeners and writes nothing. Every value this gate reads is what "the backend" received.
//
// THE BARS
//   [1]  MENU     WARP_FAMILIES is None · Sync · Alt Warp · Draw · Geometry · Filter · Saturation · Clip ·
//                 Diode & Asym · Fold · Rectify · Digital; Alt Warp is Bend + · Bend - · Bend +/- · PWM · Asym + ·
//                 Asym - · Asym +/- · Flip · Mirror · P-Quantize · Odd/Even (indices FROM THE C++ NAMES, never
//                 retyped); Sync = Sync · Formant · Fractalize; Geometry = Bend · Skew; Filter and the six
//                 distortion families equal git HEAD's; every live index filed once, 47 nowhere, the guard green;
//                 the REAL browser renders those rows in that order, opens on Alt Warp for a slot on PWM, and its
//                 search counts every live mode.
//   [2a] PICK     on all 8 slots, picking 41 / 44 / 46 from the menu onto a slot at 0 % writes the amount 0.5,
//                 once, BEFORE the mode.
//   [2b]          an amount that is not 0 % (30 %, 0.6 %) is left alone.
//   [2c]          a pick of a mode that is not centred (39, 42, 45, PWM) writes no amount.
//   [2d]          re-picking the mode the slot is already on writes no amount.
//   [2e] LOAD     the three centred modes pushed through all 8 mode relays onto amounts at 0 (automation, a
//                 preset load, a state restore), one by one and as one burst: ZERO amount writes.
//   [2f]          an amount that reads 0 % (0.4 %) counts as 0.
//   [3a] READOUT  fmtSynReadout on WARP and WARP 2 amounts: 41 / 44 signed ("+100%" · "0%" · "-100%"), 46
//                 "Odd 100%" · "Orig" · "Even 100%", 45 the flipped band, every other mode the plain percent;
//                 the drag legend under the knob prints it.
//   [3s]          the page's ALT_PM_SIGN equals SynthVoice.h kAltPmSign, and "+" is printed where altSigned > 0.
//   [3f]          the Flip readout is SynthVoice.h's law (lo = max(0, 2a - 1), hi = min(1, 2a)) at 41 amounts.
//   [3r] RING     a WARP knob's arc is bipolar and its number the distance from centre exactly while its slot is
//                 on 41 / 44 / 46 — after a backend mode push AND after a pick on this page (no local listener).
//   [3p] PILL     the WARP 2 pill prints the same words (number + unit), and its value stays inside the pill.
//   [3o] OSCQ     oscqReadout prints fmtSynReadout's text at every one of 241 steps of Volume / Pan / Semitone,
//                 goes THROUGH fmtSynReadout, and its fallback copy prints the same text.
//   [4]  EXTEND   the warp card's body drag writes no VAR on 41 / 45 / 46 (slot 1) or 44 (slot 2) and shows "—";
//                 the in-run control, Bend (1), still writes VAR.
//   [5]  FLOOR    no page error git HEAD's page did not already throw (skipped under a control).
//
// CONTROLS (the page is mutated at SOURCE into a temp copy; Source is never written) — each must redden its bar:
//   order -> 1   filing -> 1   nojump -> 2a   anyamt -> 2b   repick -> 2d   onload -> 2e   readout -> 3a
//   sign -> 3s   ring -> 3r   norepaint -> 3r   pill -> 3p   oscq -> 3o   extvar -> 4
const puppeteer = require('puppeteer-core');
const fs   = require('fs');
const path = require('path');
const ROOT = path.join(__dirname, '..');
const SRC  = process.argv[2] || process.env.AWU_PAGE || path.join(ROOT, 'Source/ui/public/index.html');
const CPP  = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
const SVH  = fs.readFileSync(path.join(ROOT, 'Source/SynthVoice.h'), 'utf8');
const MUT  = process.env.AWU_MUTATE || '';
const OUT  = process.env.AWU_TMP || require('os').tmpdir();
const VW = 820, VH = 656, DSF = 2;

let pass = 0, fail = 0; const red = [];
const chk = (ok, bar, label, detail) => {
  if (ok) pass++; else { fail++; if (red.indexOf(bar) < 0) red.push(bar); }
  console.log('  ' + (ok ? 'ok  ' : 'FAIL') + '  [' + bar + '] ' + label + (detail ? '   ' + detail : '')); };

// ── the C++ side, parsed (never retyped) ───────────────────────────────────────────────────────
function cppWarpNames() {
  const i = CPP.indexOf('static juce::StringArray terrainWarpModeNames()');
  if (i < 0) throw new Error('terrainWarpModeNames() not found');
  const body = CPP.slice(i, CPP.indexOf('\n}', i));
  const arr  = body.slice(body.indexOf('juce::StringArray w {'), body.indexOf('};'));
  const names = [];
  arr.split('\n').forEach((line) => { const c = line.indexOf('//'); (c >= 0 ? line.slice(0, c) : line).replace(/"([^"]*)"/g, (_, n) => { names.push(n); return _; }); });
  const cap = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved " \+ juce::String \(i\)\)/.exec(body);
  const total = cap ? +cap[1] : names.length;
  for (let k = names.length; k < total; k++) names.push('Reserved ' + k);
  return names;
}
const cppUi = cppWarpNames().map((n) => (n === 'NONE' ? 'None' : n));      // warp_menu's one sanctioned alias
const N = cppUi.length;
const LIVE = []; for (let i = 0; i < N; i++) if (!/^Reserved /.test(cppUi[i])) LIVE.push(i);
const IX = (name) => { const i = cppUi.indexOf(name); if (i < 0) throw new Error('no C++ warp mode named ' + name); return i; };
const pmSignM = /static constexpr double kAltPmSign\s*=\s*(-?[0-9.]+)\s*;/.exec(SVH);
const CPP_PM_SIGN = pmSignM ? +pmSignM[1] : NaN;
const FLIP_LAW = SVH.indexOf('const double lo = juce::jmax (0.0, 2.0 * a - 1.0);') >= 0
              && SVH.indexOf('const double hi = juce::jmin (1.0, 2.0 * a);') >= 0;

const WANT_LABELS = ['None', 'Sync', 'Alt Warp', 'Draw', 'Geometry', 'Filter', 'Saturation', 'Clip', 'Diode & Asym', 'Fold', 'Rectify', 'Digital'];
const WANT_NAMES = {
  'None': ['None'], 'Sync': ['Sync', 'Formant', 'Fractalize'],
  'Alt Warp': ['Bend +', 'Bend -', 'Bend +/-', 'PWM', 'Asym +', 'Asym -', 'Asym +/-', 'Flip', 'Mirror', 'P-Quantize', 'Odd/Even'],
  'Draw': ['Draw', 'Draw Amp'], 'Geometry': ['Bend', 'Skew'] };
const UNCHANGED = ['Filter', 'Saturation', 'Clip', 'Diode & Asym', 'Fold', 'Rectify', 'Digital'];
const CENTRED = [IX('Bend +/-'), IX('Asym +/-'), IX('Odd/Even')];

// ── the controls ───────────────────────────────────────────────────────────────────────────────
const MUTS = {
  order:     [["idx: [39, 40, 41, 4, 42, 43, 44, 45, 6, 8, 46]", "idx: [40, 39, 41, 4, 42, 43, 44, 45, 6, 8, 46]"]],
  filing:    [["idx: [39, 40, 41, 4, 42,", "idx: [39, 40, 41, 42,"], ["{ label: 'Geometry',    idx: [1, 5] }", "{ label: 'Geometry',    idx: [1, 4, 5] }"]],
  nojump:    [["warpCentreOnPick (modeParamId, i); pick (i);", "pick (i);"]],
  anyamt:    [["        if (Math.round (as.getNormalisedValue () * 100) !== 0) return;         // the knob does not read 0 %\n", ""]],
  repick:    [["        if (Math.round (ms.getNormalisedValue () * (card - 1)) === i) return;   // not a change of mode\n", ""]],
  // the naive design: the 12 o'clock rule rides the mode RELAY, so a preset load or automation fires it too
  onload:    [["if (ms && ms.valueChangedEvent && ms.valueChangedEvent.addListener) ms.valueChangedEvent.addListener (repaint);",
               "if (ms && ms.valueChangedEvent && ms.valueChangedEvent.addListener) ms.valueChangedEvent.addListener (() => { if (WARP_CENTRED[warpSlotMode (paramId)] === true && Math.round (state.getNormalisedValue () * 100) === 0) state.setNormalisedValue (0.5); repaint (); });"]],
  readout:   [["if (mode === 41 || mode === 44) {", "if (false) {"]],
  sign:      [["const ALT_PM_SIGN = -1;", "const ALT_PM_SIGN = 1;"]],
  ring:      [["const bipNow = () => isBipolar || (isWarpAmt && WARP_CENTRED[warpSlotMode (paramId)] === true);", "const bipNow = () => isBipolar;"]],
  norepaint: [["pick (i); if (window.__warpAmtRepaint) window.__warpAmtRepaint ();", "pick (i);"]],
  pill:      [["(c.isWarp && window.__fmtWarpAmt)", "(false)"]],
  oscq:      [["if (window.__fmtSynReadout) return window.__fmtSynReadout ('SYN_OSC_' + (L || 'A') + '_' + suf, n);", "if (false) return null;"]],
  extvar:    [["var varLive = ! (", "var varLive = true || ! ("]]
};
let PAGE = SRC, HTML = fs.readFileSync(SRC, 'utf8');
if (MUT) {
  if (!MUTS[MUT]) { console.log('  !! unknown control ' + MUT); process.exit(2); }
  for (const [a, b] of MUTS[MUT]) {
    const n = HTML.split(a).length - 1;
    if (n !== 1) { console.log('  !! control ' + MUT + ' cannot fire: anchor found ' + n + ' times: ' + a.slice(0, 70)); process.exit(2); }
    HTML = HTML.replace(a, b);
  }
  PAGE = path.join(OUT, 'altwarp_ui_mut_' + MUT + '.html'); fs.writeFileSync(PAGE, HTML);
}
const pageSign = (() => { const m = /const ALT_PM_SIGN = (-?\d+);/.exec(HTML); return m ? +m[1] : NaN; })();

// ── the stub ───────────────────────────────────────────────────────────────────────────────────
const WARP_PARAMS = [];
['A', 'B', 'C', 'D'].forEach((o) => WARP_PARAMS.push('SYN_OSC_' + o + '_WARP_MODE', 'SYN_OSC_' + o + '_WARP2_MODE'));
const AMT_OF = (mp) => mp.replace(/_WARP_MODE$/, '_WARP_AMOUNT').replace(/_WARP2_MODE$/, '_WARP2_AMT');
const SEL_OF = (mp) => /_WARP2_MODE$/.test(mp) ? '#osc-' + mp.charAt(8).toLowerCase() + '-warp2-val'
                                               : '#syn-panel [data-syn="' + AMT_OF(mp) + '"]';
const STUB = (cfg) => {
  const CH = cfg.choiceParams;
  window.__emits = []; window.__natives = [];
  const states = new Map();
  const mk = (name, n) => {
    const props = n ? { start: 0, end: n - 1, skew: 1, name: name, label: '', numSteps: n, interval: 1, parameterIndex: states.size }
                    : { start: 0, end: 1, skew: 1, name: name, label: '', numSteps: 100, interval: 0, parameterIndex: -1 };
    const ls = [];
    const st = {
      name: name, scaledValue: 0, properties: props, __ls: ls,
      getScaledValue() { return st.scaledValue; }, setScaledValue(v) { st.scaledValue = v; },
      getNormalisedValue() { return (st.scaledValue - props.start) / (props.end - props.start); },
      // js/juce/index.js:178 — emit to the backend, fire NO local listener
      setNormalisedValue(v) {
        const raw = v * (props.end - props.start) + props.start;
        st.scaledValue = props.interval > 0
          ? Math.max(props.start, Math.min(props.end, props.start + props.interval * Math.floor((raw - props.start) / props.interval + 0.5))) : raw;
        window.__emits.push({ name: name, norm: v, value: st.scaledValue }); },
      valueChangedEvent: { addListener(f) { ls.push(f); return { remove() {} }; }, removeListener() {} },
      propertiesChangedEvent: { addListener() { return { remove() {} }; }, removeListener() {} },
      getChoiceIndex: () => st.scaledValue, setChoiceIndex(i) { st.scaledValue = i; },
      getValue: () => false, setValue() {}, sliderDragStarted() {}, sliderDragEnded() {}
    };
    return st;
  };
  const get = (name) => { if (!states.has(name)) states.set(name, mk(name, CH[name] || 0)); return states.get(name); };
  window.__stubState = get;
  window.__push = (name, v) => { const st = get(name); st.scaledValue = v; st.__ls.slice().forEach((f) => { try { f(); } catch (e) {} }); };
  window.__wcJson = '{}';
  window.Juce = { getSliderState: get, getToggleState: get, getComboBoxState: get,
    getNativeFunction: (n) => (...a) => new Promise((r) => { window.__natives.push({ n: n, a: a });
      if (n === 'getWarpCurve') return r(window.__wcJson);
      if (/getPresets/i.test(n)) return r('[]'); if (/Json|JSON/.test(n)) return r('{}'); r(0); }),
    backend: { addEventListener() {}, removeEventListener() {}, emitEvent() {} } };
  (function () { const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable: true,
    get() { return held; }, set(v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '',
    __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function () {};
  Element.prototype.releasePointerCapture = function () {};
};
const HELPERS = () => {
  window.__wmPanes = function () { return [...document.querySelectorAll('.tpb-pane')]; };
  window.__wmRows  = function (pane) { return [...pane.children].map((d) => ({ el: d, name: (d.querySelector('span[style*="flex:1"]') || d).textContent.trim() })); };
  window.__wmWavetable = function () { [...document.querySelectorAll('#syn-panel .device.osc')].forEach((d) => {
      ['engine-sample', 'engine-granular', 'engine-geode', 'engine-harm'].forEach((c) => d.classList.remove(c)); }); };
  window.__wmClose = function () { try { if (window.__tpbClose) window.__tpbClose(); } catch (e) {} try { if (window.__synHideMenu) window.__synHideMenu(); } catch (e) {} };
  window.__wmOpen = function (sel) {
    window.__wmClose(); window.__wmWavetable();
    const el = document.querySelector(sel); if (!el) return 'no element ' + sel;
    el.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 300, clientY: 220 }));
    const cm = document.getElementById('syn-ctx-menu');
    if (cm && cm.classList.contains('act')) { const row = [...cm.querySelectorAll('.syn-ctx-item')].find((d) => /^(Warp mode|Mode)/.test(d.textContent.trim())); if (row) row.click(); }
    return window.__wmPanes().length >= 2 ? 'browser' : 'none';
  };
  window.__wmPick = function (family, name) {
    const panes = window.__wmPanes(); if (panes.length < 2) return 'no browser';
    const c = window.__wmRows(panes[0]).find((r) => r.name === family); if (!c) return 'no family ' + family;
    c.el.click();
    const it = window.__wmRows(window.__wmPanes()[1]).find((r) => r.name === name); if (!it) return 'no row ' + name;
    it.el.click(); return 'ok';
  };
  // pick <name> of <family> on the control <sel>, starting from mode <m0> and amount <a0> set by the BACKEND
  window.__awPick = function (mp, ap, sel, m0, a0, family, name) {
    window.__push(mp, m0); window.__push(ap, a0);
    window.__emits.length = 0;
    const o = window.__wmOpen(sel); if (o !== 'browser') return { err: 'picker did not open (' + o + ')' };
    const p = window.__wmPick(family, name); window.__wmClose();
    if (p !== 'ok') return { err: p };
    const e = window.__emits.slice();
    return { amt: e.filter((x) => x.name === ap), mode: e.filter((x) => x.name === mp),
             amtFirst: e.findIndex((x) => x.name === ap) < e.findIndex((x) => x.name === mp),
             amtNow: window.__stubState(ap).getNormalisedValue(), modeNow: window.__stubState(mp).getScaledValue() };
  };
};

(async () => {
  const cardinalities = {}; WARP_PARAMS.forEach((p) => cardinalities[p] = N);
  const b = await puppeteer.launch({ executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const boot = async (file, errs) => {
    const pg = await b.newPage(); await pg.setViewport({ width: VW, height: VH, deviceScaleFactor: DSF });
    pg.on('pageerror', (e) => errs.push(String(e).slice(0, 200)));
    await pg.evaluateOnNewDocument(STUB, { choiceParams: cardinalities });
    await pg.evaluateOnNewDocument(HELPERS);
    await pg.goto('file://' + file, { waitUntil: 'load', timeout: 60000 });
    await new Promise((r) => setTimeout(r, 1400));
    await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block'; window.dispatchEvent(new Event('resize')); });
    await new Promise((r) => setTimeout(r, 700));
    return pg;
  };
  const PRE = require('./floor_page')('AWU_PREPAGE');
  const PRE_HTML = (PRE && fs.existsSync(PRE)) ? fs.readFileSync(PRE, 'utf8') : '';
  const errs = []; const pg = await boot(PAGE, errs);
  console.log('\n══ ALT WARP · THE PAGE — ' + (MUT ? 'CONTROL ' + MUT : 'the page as it stands') + ' ══');
  console.log('   page ' + PAGE + '\n   C++: ' + N + ' warp values, ' + LIVE.length + ' live · kAltPmSign ' + CPP_PM_SIGN + '\n');

  // ── [1] THE MENU ─────────────────────────────────────────────────────────────────────────────
  {
    const fams = await pg.evaluate(() => (window.__warpFamilies || []).map((f) => ({ label: f.label, idx: f.idx.slice() })));
    const labels = fams.map((f) => f.label);
    chk(JSON.stringify(labels) === JSON.stringify(WANT_LABELS), '1', 'the families, in Serum\'s order', labels.join(' · '));
    for (const lab of Object.keys(WANT_NAMES)) {
      const f = fams.find((x) => x.label === lab), want = WANT_NAMES[lab].map(IX);
      chk(!!f && JSON.stringify(f.idx) === JSON.stringify(want), '1', lab + ' = ' + WANT_NAMES[lab].join(' · '),
          f ? '[' + f.idx.join(',') + ']' + (JSON.stringify(f.idx) === JSON.stringify(want) ? '' : ' wanted [' + want.join(',') + ']') : 'missing');
    }
    const head = {}; const k = PRE_HTML.indexOf('const WARP_FAMILIES = [');
    if (k >= 0) { const blk = PRE_HTML.slice(k, PRE_HTML.indexOf('];', k));
      blk.replace(/label:\s*'([^']*)',\s*idx:\s*\[([^\]]*)\]/g, (_, l, ix) => { head[l] = (ix.match(/\d+/g) || []).map(Number); return _; }); }
    const moved = UNCHANGED.filter((l) => { const f = fams.find((x) => x.label === l); return !f || !head[l] || JSON.stringify(f.idx) !== JSON.stringify(head[l]); });
    chk(Object.keys(head).length > 0 && moved.length === 0, '1', 'Filter and the six distortion families equal git HEAD\'s',
        Object.keys(head).length ? (moved.length ? 'differ: ' + moved.join(', ') : UNCHANGED.length + '/' + UNCHANGED.length + ' identical') : 'no floor page');
    const filed = {}; let dupes = 0; fams.forEach((f) => f.idx.forEach((i) => { if (filed[i] != null) dupes++; filed[i] = f.label; }));
    const missing = LIVE.filter((i) => filed[i] == null), reserved = Object.keys(filed).filter((i) => /^Reserved /.test(cppUi[+i] || 'Reserved'));
    const g = await pg.evaluate(() => window.__warpGuard({ quiet: true }));
    chk(missing.length === 0 && dupes === 0 && reserved.length === 0 && g.ok, '1', 'every live mode filed once, 47 nowhere, the guard green',
        'filed ' + Object.keys(filed).length + '/' + LIVE.length + ' · missing [' + missing + '] · dupes ' + dupes + ' · reserved [' + reserved + '] · guard ' + g.ok);
    const r = await pg.evaluate((sel) => {
      window.__push('SYN_OSC_A_WARP_MODE', 4);                      // a slot on PWM — the browser must open on Alt Warp
      if (window.__wmOpen(sel) !== 'browser') return null;
      const opened = window.__wmRows(window.__wmPanes()[1]).map((x) => x.name);
      const srch = document.querySelector('input.tpb-srch'); const ph = srch ? srch.placeholder : '';
      const cats = window.__wmRows(window.__wmPanes()[0]).filter((c) => c.name !== 'All'); const rows = {};
      cats.forEach((c) => { c.el.click(); rows[c.name] = window.__wmRows(window.__wmPanes()[1]).map((x) => x.name); });
      window.__wmClose(); window.__push('SYN_OSC_A_WARP_MODE', 0);
      return { cats: cats.map((c) => c.name), rows: rows, opened: opened, ph: ph };
    }, SEL_OF('SYN_OSC_A_WARP_MODE'));
    chk(!!r && JSON.stringify(r.cats) === JSON.stringify(WANT_LABELS), '1', 'the browser renders the families in that order', r ? r.cats.join(' · ') : 'no browser');
    const rowsOk = !!r && Object.keys(WANT_NAMES).every((l) => JSON.stringify(r.rows[l]) === JSON.stringify(WANT_NAMES[l]));
    chk(rowsOk, '1', 'and each family\'s rows in Serum\'s order', r ? 'Alt Warp: ' + (r.rows['Alt Warp'] || []).join(' · ') : '');
    chk(!!r && JSON.stringify(r.opened) === JSON.stringify(WANT_NAMES['Alt Warp']), '1', 'a slot on PWM opens the browser on Alt Warp', r ? r.opened.join(' · ') : '');
    chk(!!r && r.ph === 'Search ' + LIVE.length + ' warp modes…', '1', 'the search counts every live mode', r ? '"' + r.ph + '"' : '');
  }

  // ── [2] THE 12 O'CLOCK PICK ──────────────────────────────────────────────────────────────────
  const pick = (mp, m0, a0, name, family) => pg.evaluate((mp, ap, sel, m0, a0, fam, nm) => window.__awPick(mp, ap, sel, m0, a0, fam, nm),
                                                         mp, AMT_OF(mp), SEL_OF(mp), m0, a0, family || 'Alt Warp', name);
  {
    const bad = []; let n = 0;
    for (const mp of WARP_PARAMS) for (const m of CENTRED) {
      const r = await pick(mp, 0, 0, cppUi[m]); n++;
      if (r.err || r.amt.length !== 1 || r.amt[0].value !== 0.5 || !r.amtFirst || r.modeNow !== m)
        bad.push(mp.slice(8) + '/' + cppUi[m] + ': ' + (r.err || ('amount writes ' + JSON.stringify(r.amt.map((x) => x.value)) + ' first=' + r.amtFirst + ' mode=' + r.modeNow)));
    }
    chk(bad.length === 0, '2a', 'a pick of 41 / 44 / 46 onto a slot at 0 % writes 0.5, once, before the mode — all 8 slots',
        bad.length ? bad.length + '/' + n + ' wrong · ' + bad.slice(0, 3).join(' · ') : n + '/' + n);
  }
  {
    const bad = [];
    for (const [mp, a0, nm] of [['SYN_OSC_A_WARP_MODE', 0.3, 'Asym +/-'], ['SYN_OSC_B_WARP2_MODE', 0.3, 'Odd/Even'], ['SYN_OSC_C_WARP_MODE', 0.006, 'Bend +/-']]) {
      const r = await pick(mp, 0, a0, nm);
      if (r.err || r.amt.length !== 0 || Math.abs(r.amtNow - a0) > 1e-12 || r.modeNow !== IX(nm)) bad.push(mp.slice(8) + ' at ' + a0 + ': ' + (r.err || JSON.stringify(r.amt.map((x) => x.value))));
    }
    chk(bad.length === 0, '2b', 'an amount that is not 0 % (30 %, 0.6 %) is left alone', bad.join(' · ') || '3/3 kept');
  }
  {
    const bad = [];
    for (const [mp, nm] of [['SYN_OSC_A_WARP_MODE', 'Bend +'], ['SYN_OSC_D_WARP_MODE', 'Asym +'], ['SYN_OSC_A_WARP2_MODE', 'Flip'], ['SYN_OSC_B_WARP_MODE', 'PWM']]) {
      const r = await pick(mp, 0, 0, nm);
      if (r.err || r.amt.length !== 0 || r.modeNow !== IX(nm)) bad.push(mp.slice(8) + '/' + nm + ': ' + (r.err || JSON.stringify(r.amt.map((x) => x.value))));
    }
    chk(bad.length === 0, '2c', 'a pick of a mode that is not centred writes no amount', bad.join(' · ') || '4/4 silent');
  }
  {
    const bad = [];
    for (const mp of ['SYN_OSC_A_WARP_MODE', 'SYN_OSC_C_WARP2_MODE']) for (const m of CENTRED) {
      const r = await pick(mp, m, 0, cppUi[m]);
      if (r.err || r.amt.length !== 0) bad.push(mp.slice(8) + '/' + cppUi[m] + ': ' + (r.err || JSON.stringify(r.amt.map((x) => x.value))));
    }
    chk(bad.length === 0, '2d', 're-picking the mode the slot is already on writes no amount', bad.join(' · ') || '6/6 silent');
  }
  {
    const r = await pg.evaluate((params, centred) => {
      const amt = (mp) => mp.replace(/_WARP_MODE$/, '_WARP_AMOUNT').replace(/_WARP2_MODE$/, '_WARP2_AMT');
      window.__wmClose(); let one = [], burst = [];
      for (const mp of params) for (const m of centred) {
        window.__push(mp, 0); window.__push(amt(mp), 0); window.__emits.length = 0;
        window.__push(mp, m);                                          // automation / a state restore: the relay, nothing else
        one = one.concat(window.__emits.filter((e) => e.name === amt(mp)).map((e) => mp.slice(8) + '=' + e.value));
      }
      params.forEach((mp) => { window.__push(mp, 0); window.__push(amt(mp), 0); }); window.__emits.length = 0;
      params.forEach((mp, k) => window.__push(mp, centred[k % 3]));      // a preset load: every slot at once
      burst = window.__emits.filter((e) => /_WARP(_AMOUNT|2_AMT)$/.test(e.name)).map((e) => e.name.slice(8) + '=' + e.value);
      const stay = params.every((mp) => window.__stubState(amt(mp)).getNormalisedValue() === 0);
      params.forEach((mp) => window.__push(mp, 0));
      return { one: one, burst: burst, stay: stay };
    }, WARP_PARAMS, CENTRED);
    chk(r.one.length === 0 && r.burst.length === 0 && r.stay, '2e', 'a relay push of 41 / 44 / 46 (automation, load, restore) onto 0 % writes NO amount — 24 pushes + a burst',
        'amount writes: ' + r.one.length + ' single, ' + r.burst.length + ' in the burst' + (r.one.length ? ' · ' + r.one.slice(0, 3).join(' ') : '') + ' · amounts still 0: ' + r.stay);
  }
  {
    const r = await pick('SYN_OSC_D_WARP2_MODE', 0, 0.004, 'Odd/Even');
    chk(!r.err && r.amt.length === 1 && r.amt[0].value === 0.5, '2f', 'an amount that reads 0 % (0.4 %) counts as 0', r.err || JSON.stringify(r.amt.map((x) => x.value)));
  }

  // ── [3] THE READOUTS ─────────────────────────────────────────────────────────────────────────
  const plain = (a) => Math.round(a * 100) + '%';
  const signed = (a) => { const v = Math.round(CPP_PM_SIGN * (2 * a - 1) * 100) || 0; return (v > 0 ? '+' : '') + v + '%'; };
  const flipLaw = (a) => { const lo = Math.round(Math.max(0, 2 * a - 1) * 100), hi = Math.round(Math.min(1, 2 * a) * 100); return lo === hi ? 'Dry' : 'Flip ' + lo + '–' + hi + '%'; };
  const TABLE = [
    [IX('Bend +/-'), [[0, '+100%'], [0.25, '+50%'], [0.5, '0%'], [0.75, '-50%'], [1, '-100%']]],
    [IX('Asym +/-'), [[0, '+100%'], [0.37, '+26%'], [0.5, '0%'], [1, '-100%']]],
    [IX('Odd/Even'), [[0, 'Odd 100%'], [0.3, 'Odd 40%'], [0.5, 'Orig'], [0.8, 'Even 60%'], [1, 'Even 100%']]],
    [IX('Flip'),     [[0, 'Dry'], [0.37, 'Flip 0–74%'], [0.5, 'Flip 0–100%'], [0.8, 'Flip 60–100%'], [1, 'Dry']]],
    ...['None', 'Bend', 'PWM', 'Bend +', 'Bend -', 'Asym +', 'Asym -', 'Mirror', 'Tube', 'Draw', 'LP Filter'].map((nm) => [IX(nm), [[0, '0%'], [0.37, '37%'], [1, '100%']]])
  ];
  {
    const got = await pg.evaluate((T) => { const out = [];
      for (const [pid, mp] of [['SYN_OSC_A_WARP_AMOUNT', 'SYN_OSC_A_WARP_MODE'], ['SYN_OSC_B_WARP2_AMT', 'SYN_OSC_B_WARP2_MODE']])
        for (const [m, rows] of T) { window.__push(mp, m); for (const [a] of rows) out.push(window.__fmtSynReadout(pid, a)); window.__push(mp, 0); }
      return out; }, TABLE);
    const want = []; for (let s = 0; s < 2; s++) for (const [, rows] of TABLE) for (const [, w] of rows) want.push(w);
    const bad = []; want.forEach((w, k) => { if (got[k] !== w) bad.push('"' + got[k] + '" wanted "' + w + '"'); });
    chk(bad.length === 0, '3a', 'fmtSynReadout reads WARP and WARP 2 amounts in their mode\'s terms', bad.length ? bad.length + '/' + want.length + ' wrong · ' + bad.slice(0, 4).join(' · ') : want.length + '/' + want.length);
    const leg = await pg.evaluate(() => {
      window.__push('SYN_OSC_A_WARP_MODE', 41); window.__push('SYN_OSC_A_WARP_AMOUNT', 0.25);
      const k = document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]'), ring = k.querySelector('.knob-ring');
      ring.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientX: 10, clientY: 10, pointerId: 1 }));
      const t = k.querySelector('.knob-label').textContent;
      document.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 1 }));
      window.__push('SYN_OSC_A_WARP_MODE', 0); window.__push('SYN_OSC_A_WARP_AMOUNT', 0);
      return t; });
    chk(leg === signed(0.25), '3a', 'the drag legend under the knob prints it (Bend +/- at 25 %)', '"' + leg + '"');
  }
  {
    const zero = await pg.evaluate(() => { window.__push('SYN_OSC_A_WARP_MODE', 41); const t = window.__fmtSynReadout('SYN_OSC_A_WARP_AMOUNT', 0); window.__push('SYN_OSC_A_WARP_MODE', 0); return t; });
    const plusAtZero = CPP_PM_SIGN * (2 * 0 - 1) > 0;
    chk(pageSign === CPP_PM_SIGN && (zero.charAt(0) === '+') === plusAtZero, '3s', 'the page\'s ALT_PM_SIGN is SynthVoice.h\'s kAltPmSign, and "+" is where altSigned > 0',
        'page ' + pageSign + ' · C++ ' + CPP_PM_SIGN + ' · readout at 0 % "' + zero + '"');
  }
  {
    const A = []; for (let k = 0; k <= 40; k++) A.push(k / 40);
    const got = await pg.evaluate((A) => A.map((a) => window.__fmtWarpAmt(45, a)), A);
    const bad = A.filter((a, k) => got[k] !== flipLaw(a));
    chk(FLIP_LAW && bad.length === 0, '3f', 'the Flip readout is applyPhaseWarp\'s band law', 'law in SynthVoice.h: ' + FLIP_LAW + ' · ' + (A.length - bad.length) + '/' + A.length + ' amounts');
  }
  {
    const ringOf = (o) => pg.evaluate((o) => { const r = document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_' + o + '_WARP_AMOUNT"] .knob-ring');
      return { bip: r.classList.contains('kr-bip'), num: r.__kv ? r.__kv.textContent : null }; }, o);
    const steps = [];
    await pg.evaluate(() => { window.__push('SYN_OSC_B_WARP_MODE', 0); window.__push('SYN_OSC_B_WARP_AMOUNT', 0.2); });
    steps.push(['None, 20 %', await ringOf('B'), false, '20']);
    await pg.evaluate(() => window.__push('SYN_OSC_B_WARP_MODE', 41));
    steps.push(['pushed Bend +/-', await ringOf('B'), true, '60']);
    await pg.evaluate(() => window.__push('SYN_OSC_B_WARP_MODE', 39));
    steps.push(['pushed Bend +', await ringOf('B'), false, '20']);
    let r = await pick('SYN_OSC_B_WARP_MODE', 39, 0.2, 'Asym +/-');
    steps.push(['picked Asym +/- (20 % kept)', await ringOf('B'), true, '60']);
    await pick('SYN_OSC_B_WARP_MODE', IX('Asym +/-'), 0.2, 'Mirror');
    steps.push(['picked Mirror', await ringOf('B'), false, '20']);
    r = await pick('SYN_OSC_B_WARP_MODE', 0, 0, 'Odd/Even');
    steps.push(['picked Odd/Even from 0 % (12 o\'clock)', await ringOf('B'), true, '0']);
    await pg.evaluate(() => { window.__push('SYN_OSC_B_WARP_MODE', 0); window.__push('SYN_OSC_B_WARP_AMOUNT', 0); });
    const bad = steps.filter((s) => s[1].bip !== s[2] || s[1].num !== s[3]);
    chk(bad.length === 0, '3r', 'the WARP ring is bipolar, reading from centre, exactly while its slot is centred — after a push AND after a pick',
        bad.length ? bad.map((s) => s[0] + ': bip ' + s[1].bip + ' "' + s[1].num + '" wanted ' + s[2] + ' "' + s[3] + '"').join(' · ') : steps.length + '/' + steps.length + ' states');
  }
  {
    const r = await pg.evaluate((rows) => { window.__wmWavetable(); const out = [];
      const v = document.getElementById('osc-a-warp2-val'), pill = v.closest('.warp2-pill');
      const dev = v.closest('.device'), had = !!(dev && dev.classList.contains('swapped'));
      if (dev) dev.classList.add('swapped');                     // the pill is .back-only: display:none unless .swapped
      for (const [m, a] of rows) { window.__push('SYN_OSC_A_WARP2_MODE', m); window.__push('SYN_OSC_A_WARP2_AMT', a);
        const pr = pill.getBoundingClientRect(), vr = v.getBoundingClientRect();
        out.push({ t: v.textContent, pw: pr.width, inside: vr.width <= pr.width + 0.01 && vr.left >= pr.left - 0.01 && vr.right <= pr.right + 0.01 }); }
      if (dev && !had) dev.classList.remove('swapped');
      window.__push('SYN_OSC_A_WARP2_MODE', 0); window.__push('SYN_OSC_A_WARP2_AMT', 0); return out; },
      [[IX('Bend +/-'), 0.25], [IX('Asym +/-'), 1], [IX('Odd/Even'), 0.3], [IX('Odd/Even'), 0.5], [IX('Odd/Even'), 1], [IX('Flip'), 0.8], [IX('Bend +'), 0.37], [0, 0.37]]);
    const want = [signed(0.25), signed(1), 'Odd 40%', 'Orig', 'Even 100%', 'Flip 60–100%', '37%', '37%'];
    const bad = want.filter((w, k) => r[k].t !== w).map((w) => { const k = want.indexOf(w); return '"' + r[k].t + '" wanted "' + w + '"'; });
    chk(bad.length === 0, '3p', 'the WARP 2 pill prints the same words', bad.join(' · ') || want.map((w) => '"' + w + '"').join(' '));
    const measurable = r.every((x) => x.pw > 0), out = r.filter((x) => !x.inside).map((x) => '"' + x.t + '"');
    chk(measurable && out.length === 0, '3p', 'and every one of them stays inside the pill', measurable ? (out.length ? 'overflow: ' + out.join(' ') : 'pill ' + r[0].pw.toFixed(1) + 'px, all inside') : 'pill not laid out (width 0)');
  }
  {
    const r = await pg.evaluate(() => {
      const out = { diff: [], viaFmt: 0, legacy: [] }, S = ['LEVEL', 'PAN', 'SEMI'];
      if (!window.__oscqReadout) return { missing: true };
      const now = [];
      for (const suf of S) for (let k = 0; k <= 240; k++) { const n = k / 240, a = window.__oscqReadout(suf, n, 'A'), b = window.__fmtSynReadout('SYN_OSC_A_' + suf, n);
        now.push(a); if (a !== b) out.diff.push(suf + '@' + k + ' "' + a + '" vs "' + b + '"'); }
      const orig = window.__fmtSynReadout; window.__fmtSynReadout = function () { out.viaFmt++; return orig.apply(this, arguments); };
      window.__oscqReadout('PAN', 0.3, 'B'); window.__fmtSynReadout = null;
      let i = 0; for (const suf of S) for (let k = 0; k <= 240; k++) { const t = window.__oscqReadout(suf, k / 240, 'A'); if (t !== now[i++]) out.legacy.push(suf + '@' + k + ' "' + t + '"'); }
      window.__fmtSynReadout = orig; return out; });
    chk(!r.missing && r.diff.length === 0, '3o', 'oscqReadout prints fmtSynReadout\'s text at 3 x 241 steps', r.missing ? 'no __oscqReadout' : (r.diff.length ? r.diff.slice(0, 3).join(' · ') : '723/723'));
    chk(!r.missing && r.viaFmt > 0, '3o', 'it goes THROUGH fmtSynReadout (one formatter, so the two cannot drift)', 'calls ' + r.viaFmt);
    chk(!r.missing && r.legacy.length === 0, '3o', 'and its fallback copy prints the same text (the delegation changed no word)', r.missing ? '' : (r.legacy.length ? r.legacy.slice(0, 3).join(' · ') : '723/723'));
  }

  // ── [4] THE EXTEND CARD ──────────────────────────────────────────────────────────────────────
  {
    const card = (osc, slot, mode, kind) => pg.evaluate(async (osc, slot, mode, kind) => {
      const pts = []; for (let i = 0; i < 129; i++) pts.push(kind === 'filter' ? -6 : i / 128);
      const js = (m, k) => JSON.stringify({ mode: m, kind: k, amt: 0.4, 'var': 0.3, pure: true, rate: 1, pts: pts });
      const wait = (ms) => new Promise((r) => setTimeout(r, ms));
      if (window.__closeWarpExt) window.__closeWarpExt();
      window.__wcJson = js(slot ? 36 : 35, 'filter');                  // the card only opens on a filter mode ...
      window.__openWarpFilterExt(osc, slot, { clientX: 140, clientY: 120 }); await wait(300);
      window.__wcJson = js(mode, kind); await wait(400);               // ... and the slot then changes mode under it (the poll follows)
      const body = document.querySelector('.warp-ext.open .we-b'); if (!body) return { err: 'no card' };
      const R = body.getBoundingClientRect(); window.__natives.length = 0;
      body.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0, clientX: R.left + R.width * 0.7, clientY: R.top + R.height * 0.25 }));
      document.dispatchEvent(new MouseEvent('mouseup', { bubbles: true }));
      const w = window.__natives.filter((c) => c.n === 'setSynParam').map((c) => c.a[0]);
      const yv = (document.querySelector('.warp-ext .yv') || {}).textContent;
      window.__closeWarpExt(); return { w: w, yv: yv };
    }, osc, slot, mode, kind);
    const rows = [];
    for (const [osc, slot, nm] of [['a', 0, 'Bend +/-'], ['a', 0, 'Flip'], ['c', 0, 'Odd/Even'], ['b', 1, 'Asym +/-']]) {
      const O = osc.toUpperCase(), r = await card(osc, slot, IX(nm), 'phase');
      const amtP = 'SYN_OSC_' + O + (slot ? '_WARP2_AMT' : '_WARP_AMOUNT'), varP = 'SYN_OSC_' + O + (slot ? '_W2VAR' : '_WVAR');
      rows.push({ nm: nm + ' (' + O + ' slot ' + (slot + 1) + ')', ok: !r.err && r.w.indexOf(amtP) >= 0 && r.w.indexOf(varP) < 0 && r.yv === '—', r: r });
    }
    const bad = rows.filter((x) => !x.ok);
    chk(bad.length === 0, '4', 'the card\'s drag writes the amount and NO VAR on 39-46, and shows "—"',
        bad.length ? bad.map((x) => x.nm + ': ' + (x.r.err || JSON.stringify(x.r.w) + ' yv "' + x.r.yv + '"')).join(' · ') : rows.map((x) => x.nm).join(' · '));
    const c = await card('a', 0, IX('Bend'), 'phase');
    chk(!c.err && c.w.indexOf('SYN_OSC_A_WVAR') >= 0 && c.yv !== '—', '4', 'the in-run control: on Bend (1) the same drag still writes VAR',
        c.err || JSON.stringify(c.w) + ' yv "' + c.yv + '"');
  }

  // ── [5] THE FLOOR ────────────────────────────────────────────────────────────────────────────
  if (MUT) console.log('  --    [5] floor skipped under a control');
  else {
    let base = null;
    if (PRE && fs.existsSync(PRE)) { const e0 = []; const p0 = await boot(PRE, e0); base = [...new Set(e0)]; await p0.close(); }
    const uniq = [...new Set(errs)], novel = base ? uniq.filter((e) => base.indexOf(e) < 0) : uniq;
    chk(base !== null && novel.length === 0, '5', 'no page error git HEAD\'s page did not already throw',
        base === null ? 'no floor page' : uniq.length + ' error(s), ' + novel.length + ' novel' + (novel.length ? ': ' + novel[0] : ''));
  }

  console.log('\n  PASS ' + pass + '   FAIL ' + fail + (red.length ? '   RED BARS: ' + red.join(' ') : '') + '\n');
  await b.close();
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
