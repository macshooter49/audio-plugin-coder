// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb637_resize_gate.js — NOTHING IS CUT OFF, AND NOTHING MOVES INSIDE ITS BOX.
//
//      node Tests/fb637_resize_gate.js            RG_MUT=<name>  (each mutation below must go RED)
//
//  Max, after the first Windows build he could actually play: "every time I try to resize, some
//  buttons, some letters and some knobs move from the middle position... the footer shouldn't get
//  cut off anymore along with the effect card bottom line footers... Always and Scaled should not
//  be cut off either... everything is just smaller, still high-quality, nothing moves."
//
//  That is two laws, and this gate is the measurement of both, at every size the window can be:
//
//   1  NOTHING IS CUT OFF. The page is a FIXED 820×656 CSS box with overflow:hidden. Any ink whose
//      bottom passes 656 is not "slightly low", it is GONE — and the amount that is gone changes
//      with the window size, which is why it read as a resize bug. fb637 found FOUR boxes in one
//      chain that refused to shrink to the room they were given, each for the same reason: an AUTO
//      MINIMUM. A flex item's min-height defaults to auto, a bare `1fr` grid track is
//      minmax(AUTO,1fr), and an implicit grid row is max-content. All three mean "never smaller
//      than my content", so the content won height and the box overflowed instead of adapting.
//      Measured before the fix: .ti-syn-page hung 12px out of #syn-panel, and the voice column
//      another 12px out of that — the two bands that held the FX cards' bottom line and the
//      Always/Scaled pills.
//
//   2  TEXT SITS DEAD CENTRE. Measured in CSS px against the BORDER box — the pill or ring you can
//      actually see — using the text's REAL baseline (a zero-height inline-block sits on it) and the
//      font's real cap height. Centring the cap band, not the ink: ink includes descenders, so
//      centring that would push "Always" up relative to "Mono" and the row would read uneven.
//      Sub-pixel bias is not cosmetic here. A word biased half a pixel low rounds to 0 at one window
//      size and to a whole pixel at the next, so the word HOPS as the user drags — exactly the
//      "letters move inside their boxes" complaint. Drive the bias to ~0 and every zoom rounds the
//      same way. Before fb637: the voice pills sat 0.50px low and 0.50px left (half of their own 1px
//      letter-spacing — letter-spacing is added after the LAST glyph too, so a centred line carries
//      a trailing gap), "Mono" sat a further half pixel left on a stray padding-left:0, and the knob
//      numbers sat 0.75px high. All four now measure |err| < 0.05px.
//
//  THE SWEEP. The window is aspect-locked and runs 0.65×–1.90×, and the page is shown at
//  pageZoom == scale, so its CSS layout is the same at every size and only the DEVICE grid under it
//  changes. The gate therefore re-measures at a spread of scales with the viewport modelled exactly
//  as TerrainUiCore::resized() sizes it — including the capture strip — so a rounding that starves
//  the page of its 656th row shows up here as a clip, not in a bug report six weeks later.
//
//  PROOF THE BARS CAN FAIL — each mutation restores pre-fb637 lines, and these are MEASURED, not
//  assumed. Where a mutation does NOT go red that is recorded here too, because a mutation list
//  that claims more than it delivers is worse than none:
//     RG_MUT=gap       (the voice column's fixed 7px gap back) → RED   [2] and [3]
//     RG_MUT=indent    (the trailing letter-space comes back)  → RED   [4] — the pills sit left
//     RG_MUT=knob      (the knob number's 0.2em nudge removed) → RED   [4] — the digits ride high
//     RG_MUT=chain     (ALL FOUR auto-minimum fixes reverted)  → green
//     RG_MUT=autoflex / autorow / implicit (one of them each)  → green
//
//  THE CHAIN MUTATIONS COME BACK GREEN, AND THAT IS THE MOST USEFUL THING THIS GATE MEASURED.
//  The bottom of the page was being cut off by ONE over-sized box, not by four under-specified
//  ones: the voice column asked for 121px of rows plus 5 fixed 7px gaps = 156px inside a 144px
//  column. The auto-minimum chain above it (a flex item's min-height:auto, a bare `1fr` track, an
//  implicit max-content row) is what turned that 12px of excess into a GUILLOTINE — every box in
//  the chain refused to shrink, so the overflow was handed upward intact until #syn-panel's
//  overflow:hidden sliced it off, taking the FX cards' bottom line and the Always/Scaled pills with
//  it. Remove the excess and the chain never binds; that is why reverting it now changes nothing.
//  Both halves earn their place. `gap` is the fix. The chain fixes are the reason a future overflow
//  will show up as a box quietly adapting instead of a row of controls disappearing — so they stay,
//  and this note stays with them, so nobody deletes them on the strength of a green mutation and
//  re-arms the guillotine for the next person who adds a row to that column.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');

const MUT = process.env.RG_MUT || '';
const SRC = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const CHROME = process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

/* the design box and the native chrome, from PluginEditor.cpp */
const BASE_W = 820, BASE_H = 656, STRIP = 16;
const SCALES = [0.65, 0.8, 1.0, 1.1, 1.25, 1.5, 1.9];

/* the text that has to be dead centre in its own visible box */
/* `boxed` = the element draws a box the eye can measure the text against: a bordered pill, a knob
   ring. THOSE must be dead centre, and a bias there is what hops as the window is dragged. The
   others are bare words in a stack (a chip label, a dial caption) with no visible edge to be
   centred against — for them the defect that matters is CLIPPING, covered by bars [2] and [3], so
   they are measured and reported here but do not fail this bar. Asserting sub-pixel centring on a
   box that hugs its own text would be asserting the font's metrics, not the layout. */
const CENTRED = [
  ['#syn-panel .voice-toggle',         'voice pills (Mono · Legato · Poly · Macros · Always · Scaled)', true],
  ['#syn-panel .knob-ring .kv',        'knob numbers (WT Pos · Warp · Spectral · Fold · Feedback)',     true],
  ['#syn-panel .coarse-param-value',   'back-panel chip words (Coarse …)',                              false],
  ['#syn-panel .fxr-lab',              'FX card dial labels',                                           false],
];
const TOL = 0.25;          // CSS px — below this the device grid rounds the same way at every scale

/* ── the file under test, with one line optionally put back the way it was before fb637 ──────── */
function source () {
  let src = fs.readFileSync(SRC, 'utf8');
  const sub = (f, t) => {
    const n = src.split(f).length - 1;
    if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 80)); process.exit(2); }
    src = src.replace(f, t);
  };
  /* anchors stay on ONE line: this file is checked out CRLF on Windows, so an anchor spanning a
     newline silently matches zero times and the mutation proves nothing. */
  const AUTOROW  = ['grid-template-rows: 140px 140px 96px minmax(0, 1fr); gap: 12px;',
                    'grid-template-rows: 140px 140px 96px 1fr; gap: 12px;'];
  const AUTOFLEX = ['  min-height: 0;   /* 🚨 fb637 — THE FLEXBOX AUTO-MINIMUM', '  /* 🚨 fb637 — THE FLEXBOX AUTO-MINIMUM'];
  const IMPLICIT = ['#syn-panel .ribbon-row.rr-new{ padding:3px 0 3px; grid-template-rows:minmax(0, 1fr); }',
                    '#syn-panel .ribbon-row.rr-new{ padding:3px 0 3px; }'];
  if (MUT === 'chain')         { sub(...AUTOROW); sub(...AUTOFLEX); sub(...IMPLICIT);
                                 sub('#syn-panel .rr-left{ min-width:0; min-height:0;', '#syn-panel .rr-left{ min-width:0;');
                                 sub('#syn-panel .fxr-clip{ flex:1; min-width:0; min-height:0;', '#syn-panel .fxr-clip{ flex:1; min-width:0;'); }
  else if (MUT === 'autorow')  sub(...AUTOROW);
  else if (MUT === 'autoflex') sub(...AUTOFLEX);
  else if (MUT === 'implicit') sub(...IMPLICIT);
  else if (MUT === 'gap')      sub('justify-content:space-between; gap:0; }', 'justify-content:space-between; gap:7px; }');
  else if (MUT === 'indent')   sub('padding:calc(4px - 0.056em) 0 calc(4px + 0.056em); text-indent:1px; }',
                                   'padding:4px 0; }');
  else if (MUT === 'knob')     sub('  padding-top: var(--kv-pad, 0.2em); box-sizing: border-box; }', '  }');
  /* fb637b — the knob nudge is no longer a constant: the page measures the LIVE .kv at boot and
     writes --kv-pad, because 0.2em was the answer for this machine's font and the Mac's SF Pro
     needed a different one. `knob` removes the declaration outright, so it still proves bar [4].
     `kvconst` freezes it back to the hard-coded Windows value — it stays GREEN here and should go
     RED on the Mac, which is the whole point of measuring instead of guessing. */
  else if (MUT === 'kvconst')  sub('padding-top: var(--kv-pad, 0.2em);', 'padding-top: 0.2em;');
  else if (MUT)                { console.error('unknown RG_MUT ' + MUT); process.exit(2); }
  if (! MUT) return SRC;
  const tmp = path.join(os.tmpdir(), 'fb637-mut-' + MUT + '.html');
  fs.writeFileSync(tmp, src);
  return tmp;
}

/* ── the native bridge, stubbed exactly as the other UI gates stub it ─────────────────────────── */
const STUB = () => {
  const mk = () => ({ getScaledValue: () => 0, getNormalisedValue: () => 0, setNormalisedValue () {},
    valueChangedEvent: { addListener () {}, removeListener () {} },
    propertiesChangedEvent: { addListener () {}, removeListener () {} },
    properties: { start: 0, end: 1, name: '', label: '', numSteps: 128, interval: 0, parameterIndex: 0, choices: [] } });
  const ans = (n) => {
    if (/getLayerVoiceActivity/.test(n)) return [false, false, false, false];
    if (/getScanPosition/.test(n)) return -1;
    if (/getScanWindowBounds/.test(n)) return { start: 0, end: 1 };
    if (/getSynthMod|getPoppedCards|getModDrag/.test(n)) return '';
    if (/getPresets/i.test(n)) return '[]';
    if (/Json|JSON|getOscWavetable|SamplePayload|getWaterfallView/i.test(n)) return '{}';
    return 0;
  };
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk,
                  getNativeFunction: (n) => (...a) => Promise.resolve(ans(n, a)),
                  backend: { addEventListener () {}, removeEventListener () {}, emitEvent () {} } };
  (function () { const mine = window.Juce; let held = mine;
    Object.defineProperty(window, 'Juce', { configurable: true, get () { return held; },
      set (v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction, getSliderState: mine.getSliderState,
                                                    getToggleState: mine.getToggleState, getComboBoxState: mine.getComboBoxState }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '',
    __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  Element.prototype.setPointerCapture = function () {}; Element.prototype.releasePointerCapture = function () {};
  setInterval(() => { try { window.__tiAlive && window.__tiAlive(); } catch (e) {} }, 50);
};

/* ── the measurement, in the page ─────────────────────────────────────────────────────────────── */
const MEASURE = (designH, CENTRED) => {
  const vis = (el) => { const s = getComputedStyle(el);
    if (s.display === 'none' || s.visibility === 'hidden' || parseFloat(s.opacity) === 0) return false;
    const r = el.getBoundingClientRect(); return r.width > 0.5 && r.height > 0.5; };
  const name = (el) => { let n = el.tagName.toLowerCase();
    if (el.id) n += '#' + el.id;
    if (el.className && typeof el.className === 'string') n += '.' + el.className.trim().split(/\s+/).slice(0, 2).join('.');
    return n; };
  const own = (el) => { let t = ''; for (const n of el.childNodes) if (n.nodeType === 3) t += n.nodeValue; return t.trim(); };

  const all = Array.from(document.querySelectorAll('#plugin *, #syn-panel *'));
  const clipped = [], selfclip = [];
  for (const el of all) {
    if (! vis(el)) continue;
    const r = el.getBoundingClientRect();
    if (r.bottom > designH + 0.01 && r.top < designH)
      clipped.push(name(el) + (own(el) ? '[' + own(el).slice(0, 12) + ']' : '') + ' +' + (r.bottom - designH).toFixed(1));
    const s = getComputedStyle(el);
    if (s.overflowY === 'hidden' || s.overflow === 'hidden') {
      /* ASK THE BOX TO SCROLL, don't subtract two integers. scrollHeight and clientHeight are both
         ROUNDED, so a box of fractional height (13.9px is typical here — 9px text at line-height
         1.1 plus padding) reports client 13 and scroll 14 and looks like it clips 1px when nothing
         is wrong at all. Every chip label in the back panel is such a box, so the subtraction was
         all false positives. A box that can actually be scrolled is a box whose content really does
         not fit; one that refuses to move has everything inside it. */
      const was = el.scrollTop;
      el.scrollTop = 9999;
      const can = el.scrollTop;
      el.scrollTop = was;
      if (can > 0) selfclip.push(name(el) + (own(el) ? '[' + own(el).slice(0, 12) + ']' : '') + ' +' + can);
    }
  }

  const cv = document.createElement('canvas'), ctx = cv.getContext('2d'), centre = [];
  for (const [sel, label, boxed] of CENTRED) {
    let worstY = 0, worstX = 0, worstEl = '', n = 0;
    for (const el of Array.from(document.querySelectorAll(sel))) {
      if (! vis(el)) continue;
      const t = own(el); if (! t) continue;
      const s = getComputedStyle(el);
      ctx.font = `${s.fontStyle} ${s.fontWeight} ${s.fontSize} ${s.fontFamily}`;
      const m = ctx.measureText(t);
      if (! (m.actualBoundingBoxAscent >= 0)) continue;
      n++;
      const r = el.getBoundingClientRect();
      /* THE BASELINE, MEASURED — a zero-height inline-block sits on it. No font model, so this is
         right for a button, a flex item and a block alike. In a flex/grid box the bare text is an
         anonymous item, so wrap it first or the probe becomes a sibling item and reads the box
         centre instead of the baseline. */
      const probe = document.createElement('i');
      probe.style.cssText = 'display:inline-block;width:0;height:0;overflow:hidden;padding:0;margin:0;border:0;vertical-align:baseline';
      const needWrap = /flex|grid/.test(s.display);
      let wrap = null, moved = null;
      if (needWrap) {
        moved = Array.from(el.childNodes).filter(x => x.nodeType === 3);
        wrap = document.createElement('span'); wrap.style.cssText = 'display:inline;font:inherit;letter-spacing:inherit';
        el.insertBefore(wrap, moved[0]); moved.forEach(x => wrap.appendChild(x)); wrap.appendChild(probe);
      } else el.appendChild(probe);
      const baseline = probe.getBoundingClientRect().bottom;
      probe.remove();
      if (wrap) { moved.forEach(x => el.insertBefore(x, wrap)); wrap.remove(); }

      /* vertical: the CAP BAND against the border box */
      const errY = (baseline - ctx.measureText('H').actualBoundingBoxAscent / 2) - (r.top + r.height / 2);
      /* horizontal: the line's advance carries one trailing letter-space; the ink is that minus it */
      const ls = (s.letterSpacing === 'normal') ? 0 : (parseFloat(s.letterSpacing) || 0);
      const rng = document.createRange(); rng.selectNodeContents(el);
      const rr = rng.getBoundingClientRect();
      const errX = rr.width > 0 ? (rr.left + (rr.width - ls) / 2) - (r.left + r.width / 2) : 0;

      if (Math.abs(errY) > Math.abs(worstY)) { worstY = errY; worstEl = name(el) + '[' + t.slice(0, 10) + ']'; }
      if (Math.abs(errX) > Math.abs(worstX)) worstX = errX;
    }
    centre.push({ sel, label, boxed, n, errY: +worstY.toFixed(3), errX: +worstX.toFixed(3), worstEl });
  }
  const syn = document.querySelector('#syn-panel');
  return { clipped, selfclip, centre,
           synVisible: !! syn && getComputedStyle(syn).display !== 'none' && getComputedStyle(syn).visibility !== 'hidden' };
};

(async () => {
  const file = source();
  const b = await puppeteer.launch({ executablePath: CHROME, headless: 'new',
                                     args: ['--no-sandbox', '--allow-file-access-from-files', '--font-render-hinting=none'] });
  console.log('\n══ fb637 — NOTHING IS CUT OFF · NOTHING MOVES IN ITS BOX ══' + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  const clipRows = [], centreRows = [], errs = [];
  let sawSyn = false;
  for (const sc of SCALES) {
    /* the viewport the shipping editor hands the page at this scale, rounding and all */
    const winH = Math.round(sc * (BASE_H + STRIP));
    const webH = Math.ceil(sc * BASE_H);                 // fb637: the web area is sized FIRST
    const strip = Math.max(1, winH - webH);
    const cssH = (winH - strip) / sc;
    const pg = await b.newPage();
    await pg.setViewport({ width: BASE_W, height: Math.floor(cssH), deviceScaleFactor: sc });
    /* keep the top stack frame, not just the message: the one throw this page makes under stubbed
       natives is identifiable only by WHERE it comes from, and a bare "Cannot read properties of
       undefined" is exactly the kind of message a real regression would share with it. */
    pg.on('pageerror', e => errs.push((String(e.message || e) + ' @ ' +
      (String(e.stack || '').split('\n')[1] || '').trim()).slice(0, 200)));
    await pg.evaluateOnNewDocument(STUB);
    await pg.goto('file://' + file + '?page=2', { waitUntil: 'load', timeout: 60000 });
    await sleep(2200);
    /* A BLANK RACK PROVES NOTHING. The empty-state placeholder stretches to whatever height it is
       given, so it can never clip — the boxes that CAN clip are real device cards, whose head +
       core + footer have a natural height. Same for the OSC back panel: .back-only is display:none
       until the device is .swapped, so the chip words that Max saw cut off ("Coarse") are not even
       laid out until we flip it. Build the state the user is actually looking at, then measure. */
    await pg.evaluate(() => {
      document.documentElement.classList.remove('card-only-late');
      document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
      const sp = document.querySelector('#syn-panel'); if (sp) sp.style.display = 'block';
      try { ['reverb', 'flt', 'cho'].forEach(k => window.__fxAdd && window.__fxAdd(k));
            if (window.__fx4Tick) window.__fx4Tick(); } catch (e) {}
      const osc = document.querySelector('#syn-panel .device.osc');
      if (osc) osc.classList.add('swapped');                    // .back-only is display:none without it
      window.dispatchEvent(new Event('resize'));
    });
    await sleep(1400);
    const r = await pg.evaluate(MEASURE, BASE_H, CENTRED);
    sawSyn = sawSyn || r.synVisible;
    clipRows.push({ sc, cssH, strip, clipped: r.clipped, selfclip: r.selfclip });
    centreRows.push({ sc, centre: r.centre });
    await pg.close();
  }
  await b.close();

  const bars = [];
  const bar = (ok, title, detail) => { bars.push(ok); console.log('  ' + (ok ? 'PASS' : 'FAIL') + '  ' + title + '\n        ' + detail); };

  /* [0] the rig actually looked at the page we care about.
     formatOutput is excluded by name and only by name: it throws at boot in EVERY headless run of
     this page, before and after fb637, because the stubbed natives hand updateOutputDisplay no
     output level to format. That is stub data, not a page fault, and it is unrelated to layout —
     but anything ELSE that throws is a real regression and fails this bar. */
  const stubArtefact = (e) => /formatOutput|updateOutputDisplay/.test(e);
  const realErrs = errs.filter(e => ! stubArtefact(e));
  bar(sawSyn && realErrs.length === 0, '[0] THE RIG — the synth page is up and the page does not throw',
      'syn panel seen ' + sawSyn + ' · unexpected page errors ' + realErrs.length +
      (realErrs.length ? ' [' + realErrs.slice(0, 2).join(' | ') + ']' : '') +
      ' · known stub-data throws ignored: ' + (errs.length - realErrs.length) + ' (formatOutput, no output level stubbed)');

  /* [1] the page is never handed less than its design box */
  const short = clipRows.filter(r => r.cssH < BASE_H - 0.001);
  bar(short.length === 0, '[1] 🚨 THE PAGE ALWAYS GETS ITS 656 — the strip takes the remainder, not the page',
      SCALES.map((s, i) => s + '→' + clipRows[i].cssH.toFixed(2)).join(' · ') +
      (short.length ? '  SHORT AT ' + short.map(r => r.sc).join(',') : '  (never short)'));

  /* [2] nothing hangs out of the bottom of the design box, at any size */
  const anyClip = clipRows.filter(r => r.clipped.length);
  bar(anyClip.length === 0, '[2] 🚨 NOTHING IS CUT OFF — no ink past the 656 line, at any window size',
      anyClip.length ? anyClip.map(r => 'scale ' + r.sc + ': ' + r.clipped.slice(0, 4).join(', ')).join('  |  ')
                     : 'clean at ' + SCALES.length + ' scales, ' + SCALES[0] + '×–' + SCALES[SCALES.length - 1] + '×');

  /* [3] and no box quietly clips its own content either */
  /* Two boxes clip themselves by 1px and did so long before fb637 — they are one-line text boxes
     whose fixed height is a hair under what the font paints. They are listed by name so this bar can
     stay green and still catch a NEW one; RG_FULL=1 prints everything, and the baseline is
     reproducible with `git stash` on index.html. They are not fb637's to fix: changing them means
     re-tuning the FX card's preset chip and the SUB pill, which is a design call, not a bug fix. */
  const KNOWN = [/\.sub-pill\b/, /\bfxr-pname\b/];
  const FULL = !! process.env.RG_FULL;
  const fresh = (list) => list.filter(x => ! KNOWN.some(k => k.test(x)));
  const anySelf = clipRows.filter(r => fresh(r.selfclip).length);
  const knownSeen = new Set(clipRows.flatMap(r => r.selfclip.filter(x => KNOWN.some(k => k.test(x)))));
  bar(anySelf.length === 0, '[3] NO BOX CLIPS ITS OWN CONTENT — overflow:hidden has nothing to eat',
      (anySelf.length
        ? anySelf.map(r => 'scale ' + r.sc + ': ' + fresh(r.selfclip).slice(0, FULL ? 99 : 4).join(', ')).join('  |  ')
        : 'no new self-clipping at any scale')
      + (knownSeen.size ? '\n        known pre-fb637 residuals, allowed: ' + [...knownSeen].join(', ') : ''));

  /* [4] text is centred in the box you can see — and therefore stops hopping as the window moves */
  let worst = null;
  for (const row of centreRows) for (const c of row.centre)
    if (! worst || Math.max(Math.abs(c.errY), Math.abs(c.errX)) > Math.max(Math.abs(worst.errY), Math.abs(worst.errX)))
      worst = Object.assign({ sc: row.sc }, c);
  const offenders = [], noted = [];
  for (const row of centreRows) for (const c of row.centre)
    if (c.n && (Math.abs(c.errY) > TOL || Math.abs(c.errX) > TOL))
      (c.boxed ? offenders : noted).push('scale ' + row.sc + ' ' + c.label + ' dY ' + c.errY + ' dX ' + c.errX + ' ' + c.worstEl);
  const counted = centreRows[0].centre.filter(c => c.n).map(c => c.label + '×' + c.n).join(' · ');
  const boxedWorst = centreRows.flatMap(r => r.centre).filter(c => c.boxed && c.n)
    .reduce((a, c) => Math.max(Math.abs(c.errY), Math.abs(c.errX)) > a ? Math.max(Math.abs(c.errY), Math.abs(c.errX)) : a, 0);
  bar(offenders.length === 0, '[4] 🚨 TEXT SITS DEAD CENTRE IN ITS BOX — cap band vs the border box, within ' + TOL + 'px',
      (offenders.length ? offenders.slice(0, 4).join('  |  ')
                        : 'boxed text (pills, knob rings) worst over the whole sweep: ' + boxedWorst.toFixed(3) + ' CSS px')
      + '\n        measured: ' + (counted || 'nothing matched — the selectors went stale')
      + (noted.length ? '\n        (unboxed text, reported not asserted — bare words with no edge to centre against: '
                        + noted.slice(0, 2).join(' | ') + ')' : ''));

  /* [5] the selectors still match something: a gate measuring zero elements passes vacuously */
  const empty = centreRows[0].centre.filter(c => ! c.n).map(c => c.sel);
  bar(empty.length === 0, '[5] THE GATE IS STILL LOOKING AT SOMETHING — every centred selector matches',
      empty.length ? 'MATCHED NOTHING: ' + empty.join(', ') + ' (renamed? then re-point this list)'
                   : centreRows[0].centre.map(c => c.sel.replace('#syn-panel ', '') + '×' + c.n).join(' · '));

  const nPass = bars.filter(Boolean).length;
  console.log('\n  ' + (nPass === bars.length ? '✅' : '❌') + ' ' + nPass + ' passed, ' + (bars.length - nPass) + ' failed\n');
  process.exit(nPass === bars.length ? 0 : 1);
})();
