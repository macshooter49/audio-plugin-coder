// flowmod_underline — THE LIVING UNDERLINE ON THE FOUR FLOW CARDS (fb524).
//
// Max, on a POPPED GLITCH card: "I'm trying to put an LFO 1 on the chance of the glitch and
// there's nothing there. We can't even attenuate or measure these. There's no underlines, I
// can't even see them."
//
// WHY THIS IS A SIBLING OF fxmod_underline.js AND NOT A BAR INSIDE IT
//   fxmod_underline covers the FX RACK: __fxAdd/__fxrDevs/.fxr-dev/.fxr-clip, one document, one
//   820×656 viewport, destinations resolved through __fxModIsDest/__fxModKnobNorm. A FLOW card is
//   a different surface with a different model (data-ti-key → BIND → MODDEST → __tiCardNorm) and,
//   crucially, a SECOND DOCUMENT: the popped card is index.html?card=<id> in its own native window
//   with its own, much smaller bounds. Every bar here has to say which document and which bounds
//   it is speaking about. Folding that into fxmod would have put a two-way branch in all seven of
//   its bars; a green fxmod suite is exactly what hid this bug for a whole arc, and the answer to
//   that is a gate for the surface that was never covered, not a wider branch on the one that was.
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/flowmod_underline.js [page.html]
//
// 🚨 THE VIEWPORT. The docked bars run at the SHIPPED 820×656 (fb454's law). The popped bars run at
// each card's REAL popped bounds — popOutCardWindow is handed Math.round(rect.width/height) of the
// docked card (the .pop handler), so the native window is exactly:
//        arp 379×421 · chop 379×441 · gli 379×451 · rbn 379×510
// Those are measured from the shipped page, not chosen. Running a card at 820×656 would make every
// containment bar pass vacuously — the whole point of fb454's law, one window down.
//
// THE BARS
//   1  COVERAGE — all four cards, DOCKED and POPPED: every [data-mod-dest] cell paints a mark on
//      assign and the marks come DOWN on delete. GLITCH's "Chance" is asserted BY NAME (it is
//      FLOW_GLI_VARY → dest 23; there is no FLOW_GLI_CHANCE param).
//   2  LIVE — in the POPPED card the comet tracks an injected __modViz LFO feed through
//      __mvLfoValAt: the head moves across the mark and the direction follows the value.
//   3  ANCHOR LAW — span.left = (1−d)·knobNorm·w and span.width = d·w (the ~9162 law), with
//      knobNorm taken from the CARD'S OWN READOUT (the Chance cell prints its percent), never from
//      the same __tiCardNorm the code under test uses.
//   4  DEPTH DRAG + X — a vertical drag on the mark changes the route's depth live in __tiRoutes,
//      and the hover route list's ✕ really deletes (route gone, mark gone).
//   5  CONTAINMENT — at the card's real bounds the attenuator meter, its readout HEAD and the
//      route list are non-degenerate and fully inside the window at the left edge, the right edge,
//      the bottom row and the top row, and the list never covers the mark's own 7 px drag strip
//      (fb454). The readout head hangs ABOVE the meter box, so the top row is what proves the
//      clamp's lower bound and the right edge is what proves its horizontal half.
//   6  MIRROR — the popped card's model IS the processor blackboard: a route written into
//      setSynthMod from outside appears (mark up), and one removed outside disappears (mark down).
//      A private copy that only ever grew would pass bar 1 and fail here.
//
// PROOF THE BARS CAN FAIL (fb421 — a gate that has never failed has never been tested).
// Each mutation is a SOURCE rewrite into a temp copy of the page, so what is broken is the shipped
// line and not a harness stub:
//   FLOWUL_MUTATE=1  restores `if(window.__cardOnly)return;` as loop()'s first statement (the
//                    pre-fb524 line)                                    → bars 1(popped)/2/3/4/5 red
//   FLOWUL_MUTATE=2  removes the fb455/laneB clamp from showAtt entirely — `top`/`left` written
//                    verbatim, the pre-clamp code                       → bar 5 red
//   FLOWUL_MUTATE=3  re-adds .ti-preboot to the shared meter instead of removing it (the
//                    parse-time-body-child sweep, fb145's 34914 trap)   → bar 5 red
//   FLOWUL_MUTATE=4  disables the card-only mirror prune                → bar 6 red
//   FLOWUL_MUTATE=5  drops the S argument from stampMod, so knobNorm has no card model and the
//                    territory anchors at 0                            → bar 3 red
//   FLOWUL_MUTATE=6  removes the card-cell z-index lift, so the mark is painted UNDER the card
//                    that owns its word (the shipped bug)              → bars 1/4/5 red
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const PAGE = process.argv[2] || process.env.FLOWUL_PAGE ||
  path.join(__dirname, '..') + '/Source/ui/public/index.html';
const MUT = +(process.env.FLOWUL_MUTATE || 0);

// card id → [popped width, popped height, docked class]. Bounds MEASURED from the shipped page.
const CARDS = {
  arp:  { w: 379, h: 421, cls: 'arp-ext',  mode: 'arp'    },
  chop: { w: 379, h: 441, cls: 'chop-ext', mode: 'chop'   },
  gli:  { w: 379, h: 451, cls: 'gli-ext',  mode: 'glitch' },
  rbn:  { w: 379, h: 510, cls: 'rbn-ext',  mode: 'drift'  },
};

let pass = 0, fail = 0;
function chk (ok, label, detail) {
  if (ok) { pass++; console.log('  ok    ' + label + (detail ? '   ' + detail : '')); }
  else    { fail++; console.log('  FAIL  ' + label + (detail ? '   ' + detail : '')); }
}

// ── the mutated page (source rewrite, temp copy) ────────────────────────────────────────────
function mutatedPage () {
  if (!MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (from, to) => {
    if (src.indexOf(from) < 0) { console.error('MUTATION ' + MUT + ': anchor not found -> ' + from.slice(0, 90)); process.exit(2); }
    src = src.replace(from, to);
  };
  if (MUT === 1) sub("function loop(ts){ if(window.__cardOnly==='mod')return;",
                     "function loop(ts){ if(window.__cardOnly)return;");
  if (MUT === 2) sub("    att.style.left=Math.max(M+OV, Math.min(xLayout, window.__vw()-W-M-OV))+'px';\n" +
                     "    att.style.top=Math.max(HEAD+M, Math.min(yLayout, window.__vh()-H-M))+'px'; }",
                     "    att.style.left=xLayout+'px';\n    att.style.top=yLayout+'px'; }");
  if (MUT === 3) sub("    att.classList.remove('ti-preboot');\n    att.classList.add('on');",
                     "    att.classList.add('ti-preboot');\n    att.classList.add('on');");
  if (MUT === 4) sub("      if(live&&!badgeBusy&&(Date.now()-lastLocalEdit)>1500)",
                     "      if(false&&live&&!badgeBusy&&(Date.now()-lastLocalEdit)>1500)");
  if (MUT === 5) src = src.split('TIC.stampMod(sh.card,BIND,S);').join('TIC.stampMod(sh.card,BIND);');
  if (MUT === 6) sub("        if(el.closest&&el.closest('.ti-card')) u.style.zIndex='2147483646';",
                     "        /* fb524 lift removed by the mutation */");
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'flowul-')), 'index.html');
  fs.writeFileSync(out, src);
  return out;
}

// ── the page stub. THE BLACKBOARD IS REAL (fb393: a harness kinder than reality is worse than no
//    harness — and one HARSHER is a false failure). getSynthMod/setSynthMod are backed by one
//    in-memory array, exactly as the processor backs them, so the card's mirror is exercised for
//    real instead of reading a frozen '[]' that would prune every route it just made. ────────────
const STUB = () => {
  window.__BB = [];                                   // the "processor" mod matrix
  const mk = () => ({ getScaledValue: () => 0.5, setScaledValue () {}, getNormalisedValue: () => 0.5, setNormalisedValue () {},
    getChoiceIndex: () => 0, setChoiceIndex () {}, getValue: () => false, setValue () {},
    valueChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} },
    propertiesChangedEvent: { addListener () { return { remove () {} }; }, removeListener () {} },
    properties: { start: 0, end: 1, interval: 0, name: '', label: '', numSteps: 100, choices: [], parameterIndex: 0 } });
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk,
    getNativeFunction: (n) => (...a) => new Promise(r => {
      if (n === 'getSynthMod') return r(JSON.stringify(window.__BB || []));
      if (n === 'setSynthMod') { try { window.__BB = JSON.parse(a[0] || '[]'); } catch (e) {} return r(0); }
      if (n === 'getModDrag')  return r('null');
      /* the anchor law is (1−d)·knob — with every param reading 0 the bar would confirm
         "0 == 0" forever. FLOW_GLI_VARY (GLITCH's Chance) is given a real, off-centre
         position so the territory has somewhere to be anchored TO; everything else keeps
         the plain 0 the rest of the suite was built on. */
      if (n === 'getSynParam') return r(a[0] === 'FLOW_GLI_VARY' ? 0.62 : 0);
      if (/getPresets/i.test(n)) return r('[]');
      if (/Json|JSON/.test(n)) return r('{}');
      r(0); }),
    backend: { addEventListener () {}, removeEventListener () {}, emitEvent () {} } };
  (function () { const mine = window.Juce; let held = mine;
    Object.defineProperty(window, 'Juce', { configurable: true, get () { return held; },
      set (v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction }); } }); })();
  window.__JUCE__ = { backend: window.Juce.backend, initialisationData: { vendor: '', pluginName: '', pluginVersion: '',
    __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  /* synthetic PointerEvents carry no active pointer; the ul drag calls setPointerCapture */
  Element.prototype.setPointerCapture = function () {};
  Element.prototype.releasePointerCapture = function () {};
};

const HELPERS = () => {
  /* the visible mark sitting on a cell's WORD. A card cell's word is .lb (`.ti-card .cell .lb`);
     the MIX header and other wordless surfaces fall back to the element itself — the same
     resolution order the painter uses, so this measures what the user sees. */
  window.__fmWord = function (cell) { return (cell && cell.querySelector && cell.querySelector('.lb')) || cell; };
  window.__fmInk = function (cell) {
    const lb = window.__fmWord(cell); if (!lb) return null;
    const rg = document.createRange(); rg.selectNodeContents(lb);
    const r = rg.getBoundingClientRect();
    return r.width ? r : (lb.getBoundingClientRect ? lb.getBoundingClientRect() : null);
  };
  window.__fmFind = function (cell) {
    const r = window.__fmInk(cell); if (!r || !r.width) return null;
    return [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')
      .find(x => { const q = x.getBoundingClientRect();
        return Math.abs(q.left - r.left) < 5 && Math.abs(q.top - (r.bottom - 1)) < 5; }) || null;
  };
  /* the mark must WIN THE HIT TEST at its own centre. A mark that is in the DOM, correctly
     positioned and painted underneath the opaque card that owns its word is invisible to the
     user and unclickable — which is exactly what z-index 2147483644 under a 2147483645 card
     produced. Geometry alone cannot see that; elementFromPoint can. */
  window.__fmTop = function (u) { if (!u) return false;
    const r = u.getBoundingClientRect();
    const e = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
    return !!(e && (e === u || u.contains(e))); };
  window.__fmCells = function () { return [...document.querySelectorAll('.ti-card [data-mod-dest]')]; };
  /* only the cells the user can actually SEE. A card keeps every pane in the DOM and shows one at
     a time, so a cell in a hidden tab has a zero-width word and MUST NOT paint a mark (the loop's
     own `r2.width===0 -> down` rule). Counting those would make the bar demand a bug. */
  window.__fmVisCells = function (root) {
    const W = window.innerWidth, H = window.innerHeight;
    return window.__fmCells().filter(c => { if (root && !root.contains(c)) return false;
      const r = window.__fmInk(c);
      /* and ON SCREEN. A docked card can hang past the 820 px window edge; a word nobody can see
         must not carry a mark, so demanding one there would make the bar demand a bug. */
      return !!(r && r.width > 0.5 && r.height > 0.5 &&
                r.left >= 0 && r.top >= 0 && r.right <= W && r.bottom <= H); }); };
  window.__fmTabs = function (root) {
    return [...(root || document).querySelectorAll('.tabs .tab')].map(t => t.getAttribute('data-p')); };
  window.__fmTab = function (root, p) {
    const t = [...(root || document).querySelectorAll('.tabs .tab')].find(x => x.getAttribute('data-p') === p);
    if (t) t.click(); return !!t; };
  window.__fmChance = function () { return document.querySelector('.ti-card .chcell[data-mod-dest]'); };
  window.__fmMarksUp = function () {
    return [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none').length; };
  window.__fmFeed = function (v) {
    const a = new Array(10).fill(0); a[0] = v;
    window.__modViz(null, a, null); window.__mvLfoValTPrev = Date.now() - 33;
  };
  window.__fmRect = function (sel) { const e = document.querySelector(sel); if (!e) return null;
    const r = e.getBoundingClientRect();
    return { l: r.left, t: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height }; };
};

const wait = ms => new Promise(r => setTimeout(r, ms));

async function openCard (browser, id, opts) {
  const c = CARDS[id];
  const pg = await browser.newPage();
  await pg.setViewport({ width: c.w, height: c.h, deviceScaleFactor: 2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + (opts && opts.page) + '?card=' + id, { waitUntil: 'load', timeout: 60000 });
  await wait(2400);   // card boot (setTimeout 60 + the openFlowCard retry ladder) + the 700 ms first restore
  pg.__errs = errs;
  return pg;
}

/* a real pointer gesture on the mark: down, optional vertical move, up. */
async function markDrag (pg, box, dy) {
  const cx = box.l + box.w / 2, cy = box.t + box.h / 2;
  await pg.mouse.move(cx, cy); await pg.mouse.down();
  if (dy) { await pg.mouse.move(cx, cy + dy, { steps: 4 }); }
  await wait(60);
  return { cx, cy };
}

(async () => {
  const P = mutatedPage();
  const browser = await puppeteer.launch({
    executablePath: (process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });

  console.log('\n══ THE LIVING UNDERLINE ON THE FLOW CARDS — ' + (MUT ? ('MUTATION ' + MUT) : 'the shipped page') + ' ══');
  console.log('   page ' + PAGE + (MUT ? ('  (mutated copy: ' + P + ')') : ''));
  console.log('   popped bounds  arp 379×421 · chop 379×441 · gli 379×451 · rbn 379×510   docked 820×656\n');

  // ── 1a  COVERAGE, DOCKED (820×656) — the surface that already worked must keep working ─────
  {
    const pg = await browser.newPage();
    await pg.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
    const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
    await pg.evaluateOnNewDocument(STUB);
    await pg.evaluateOnNewDocument(HELPERS);
    await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 });
    await wait(2200);
    await pg.evaluate(() => { const sp = document.getElementById('syn-panel');
      if (sp) sp.style.display = 'block'; window.dispatchEvent(new Event('resize')); });
    await wait(700);
    let tot = 0, got = 0, miss = [];
    for (const id of Object.keys(CARDS)) {
      const opened = await pg.evaluate(async (mode, cls) => {
        window.__tiPruneFxRoutes(0, 1e9);
        /* ONE CARD AT A TIME. The docked cards all open at the same screen position, so leaving
           the previous one open stacks four cards — and four sets of marks — on the same pixels,
           and the topmost mark wins every hit test. Nothing to do with the code under test. */
        document.querySelectorAll('.ti-card.open').forEach(c => c.classList.remove('open'));
        try { window.__openFlowCard(mode); } catch (e) {}
        await new Promise(r => setTimeout(r, 800));
        const card = document.querySelector('.' + cls);
        if (!card || !card.classList.contains('open')) return null;
        /* park it fully inside the shipped window — the default placement hangs ~145 px past the
           right edge of 820, and a word off the edge is not a surface. */
        card.style.left = '4px'; card.style.top = '4px';
        await new Promise(r => setTimeout(r, 250));
        return window.__fmTabs(card).length ? window.__fmTabs(card) : [null];
      }, CARDS[id].mode, CARDS[id].cls);
      if (!opened) { miss.push(id + ': card did not open'); continue; }
      for (const tab of opened) {
        const r = await pg.evaluate(async (cls, tab) => {
          const card = document.querySelector('.' + cls);
          if (tab) { window.__fmTab(card, tab); await new Promise(r => setTimeout(r, 250)); }
          window.__tiPruneFxRoutes(0, 1e9);
          const cells = window.__fmVisCells(card);
          window.__selMod = { lfo: 1 };
          for (const c of cells) window.__tiAddRoute(0, 1, +c.getAttribute('data-mod-dest'));
          return cells.length;
        }, CARDS[id].cls, tab);
        await wait(400);
        const q = await pg.evaluate((cls) => {
          const card = document.querySelector('.' + cls);
          const cells = window.__fmVisCells(card);
          let n = 0; const bad = [];
          const seen = {};
          for (const c of cells) { const d = c.getAttribute('data-mod-dest');
            /* dests 23/24/25 are MACRO-SHARED across the flow cards by design (30231), and the
               docked page can have all four cards open at once — one dest, one surface, first
               found. Count such a dest once: a mark on any of its visible cells is the mark. */
            if (seen[d]) continue;
            const u = window.__fmFind(c);
            if (u && window.__fmTop(u)) { seen[d] = 1; n++; }
            else if (!bad.includes(d)) bad.push(d); }
          for (const b of bad) if (seen[b]) bad.splice(bad.indexOf(b), 1);
          window.__tiPruneFxRoutes(0, 1e9);
          return { n, bad, tot: Object.keys(cells.reduce((o, c) => (o[c.getAttribute('data-mod-dest')] = 1, o), {})).length };
        }, CARDS[id].cls);
        tot += q.tot; got += q.n;
        if (q.bad.length) miss.push(id + '/' + (tab || 'main') + ' missing ' + JSON.stringify(q.bad));
      }
    }
    chk(tot > 0 && got === tot && !miss.length,
      'COVERAGE/docked — every routable cell on all four FLOW cards paints a mark (820×656)',
      'marks ' + got + '/' + tot + (miss.length ? '  ' + miss.join(' | ') : ''));
    chk(errs.length === 0, 'COVERAGE/docked — no page errors', errs.join(' | ') || 'clean');
    await pg.close();
  }

  // ── 1b  COVERAGE, POPPED — one document per card, at that card's real window bounds ────────
  //    🚨 ONE PAGE AT A TIME. A backgrounded tab's requestAnimationFrame is throttled to nothing,
  //    and the underline rides the fb511b rAF fallback lane in a card window (there is no C++ push
  //    here) — holding four card pages open would have every bar after this one reading a stalled
  //    painter and calling it a bug.
  {
    let tot = 0, got = 0, miss = [], down = 0, chanceOk = false, chanceDest = null, chanceWord = null;
    const errs = [];
    for (const id of Object.keys(CARDS)) {
      const pg = await openCard(browser, id, { page: P });
      const tabs = await pg.evaluate(() => { const t = window.__fmTabs(document); return t.length ? t : [null]; });
      for (const tab of tabs) {
        const n = await pg.evaluate(async (tab) => {
          if (tab) { window.__fmTab(document, tab); await new Promise(r => setTimeout(r, 250)); }
          window.__tiPruneFxRoutes(0, 1e9);
          const cells = window.__fmVisCells(null);
          window.__selMod = { lfo: 1 };
          for (const c of cells) window.__tiAddRoute(0, 1, +c.getAttribute('data-mod-dest'));
          return cells.length; }, tab);
        await wait(450);
        const q = await pg.evaluate(() => {
          const cells = window.__fmVisCells(null);
          let n = 0; const bad = [];
          for (const c of cells) { const u = window.__fmFind(c);
            if (u && window.__fmTop(u)) n++;
            else bad.push(c.getAttribute('data-mod-dest') + '/' + ((c.querySelector('.lb') || {}).textContent || '·') + (u ? ' (covered)' : ' (no mark)')); }
          const ch = window.__fmChance();
          return { n, bad, chance: !!(ch && window.__fmFind(ch) && window.__fmTop(window.__fmFind(ch))),
                   chanceDest: ch ? ch.getAttribute('data-mod-dest') : null,
                   chanceWord: ch ? (ch.querySelector('.lb') || {}).textContent : null }; });
        tot += n; got += q.n;
        if (q.bad.length) miss.push(id + '/' + (tab || 'main') + ' missing ' + JSON.stringify(q.bad));
        if (id === 'gli' && q.chanceDest) { chanceOk = chanceOk || q.chance; chanceDest = q.chanceDest; chanceWord = q.chanceWord; }
        await pg.evaluate(() => window.__tiPruneFxRoutes(0, 1e9));
        await wait(350);
        down += await pg.evaluate(() => window.__fmMarksUp());
      }
      (pg.__errs || []).forEach(e => errs.push(id + ': ' + e));
      await pg.close();
    }
    chk(tot > 0 && got === tot && !miss.length,
      'COVERAGE/popped — every VISIBLE routable cell on all four POPPED cards, every tab, paints a mark',
      'marks ' + got + '/' + tot + (miss.length ? '  ' + miss.join(' | ') : ''));
    chk(chanceOk && chanceWord === 'Chance',
      'COVERAGE/popped — GLITCH\u2019s "Chance" carries a mark (Max\u2019s exact control)',
      'dest ' + chanceDest + ' = FLOW_GLI_VARY, word "' + chanceWord + '"');
    chk(down === 0, 'COVERAGE/popped — every mark comes DOWN on delete', 'marks still up: ' + down);
    chk(errs.length === 0, 'COVERAGE/popped — no page errors in any card document', errs.slice(0, 3).join(' | ') || 'clean');
  }

  // the remaining bars all live in ONE foreground GLITCH card at its real 379×451 bounds
  const gli = await openCard(browser, 'gli', { page: P });
  await gli.bringToFront();

  // ── 2  LIVE — the comet rides the injected feed inside the CARD document ───────────────────
  {
    const o = await gli.evaluate(async () => {
      window.__tiPruneFxRoutes(0, 1e9);
      const ch = window.__fmChance(); const d = +ch.getAttribute('data-mod-dest');
      window.__tiAddRoute(0, 1, d); window.__selMod = { lfo: 1 };
      await new Promise(r => setTimeout(r, 400));
      const read = () => { const u = window.__fmFind(ch); if (!u) return null;
        const cm = u.children[2];
        return { gw: u.offsetWidth, on: cm.style.display !== 'none',
                 l: parseFloat(cm.style.left) || 0, w: parseFloat(cm.style.width) || 0 }; };
      window.__fmFeed(-1); await new Promise(r => setTimeout(r, 250)); const lo = read();
      window.__fmFeed(1);  await new Promise(r => setTimeout(r, 250)); const hi = read();
      const dep = (window.__tiRoutes().find(r => r.d === d) || {}).v || 0.5;
      return { lo, hi, dep };
    });
    const ok = o.lo && o.hi && o.lo.on && o.hi.on &&
      ((o.hi.l + o.hi.w) - (o.lo.l + o.lo.w)) > 0.5 * o.dep * o.lo.gw;
    chk(ok, 'LIVE — the comet follows an injected LFO feed across the mark (popped GLITCH)',
      o.lo && o.hi ? ('head moved ' + ((o.hi.l + o.hi.w) - (o.lo.l + o.lo.w)).toFixed(1) + 'px of ' +
        (o.dep * o.lo.gw).toFixed(1) + 'px territory') : 'no mark/comet');
  }

  // ── 3  ANCHOR LAW — (1−d)·knob, knob read from the card's OWN printed value ────────────────
  {
    const o = await gli.evaluate(() => {
      const ch = window.__fmChance(); const u = window.__fmFind(ch);
      if (!u) return { err: 'no mark' };
      const d = (window.__tiRoutes().find(r => r.d === +ch.getAttribute('data-mod-dest')) || {}).v;
      const bn = ch.querySelector('.bn');
      const shown = bn ? (parseInt(bn.textContent, 10) / 100) : null;   // the card PRINTS its own %
      const sp = u.children[1];
      return { d: Math.abs(d), shown, w: u.offsetWidth,
               left: parseFloat(sp.style.left) || 0, span: parseFloat(sp.style.width) || 0 };
    });
    if (o.err || o.shown == null) chk(false, 'ANCHOR LAW — span.left = (1−d)·knob·w', o.err || 'no printed value to test against');
    else {
      const expL = (1 - o.d) * o.shown * o.w, expW = Math.max(0.02, o.d) * o.w;
      const ok = Math.abs(o.left - expL) < 1.0 && Math.abs(o.span - expW) < 1.0 &&
                 o.shown > 0.05 && expL > 2;   /* a knob at 0 anchors at 0 and proves nothing */
      chk(ok, 'ANCHOR LAW — the depth territory anchors at (1−d)·knob on a CARD dial',
        'knob ' + o.shown.toFixed(2) + ' d ' + o.d.toFixed(2) + ' w ' + o.w.toFixed(1) +
        '  left ' + o.left.toFixed(2) + ' (want ' + expL.toFixed(2) + ')  span ' + o.span.toFixed(2) + ' (want ' + expW.toFixed(2) + ')');
    }
  }

  // ── 4  DEPTH DRAG + THE ROUTE LIST'S ✕ ─────────────────────────────────────────────────────
  {
    const before = await gli.evaluate(() => {
      const ch = window.__fmChance(); const u = window.__fmFind(ch);
      const r = u ? u.getBoundingClientRect() : null;
      return { v: (window.__tiRoutes()[0] || {}).v,
               box: r ? { l: r.left, t: r.top, w: r.width, h: r.height } : null };
    });
    if (!before.box) chk(false, 'DEPTH DRAG — a vertical drag on the mark sets depth', 'no mark');
    else {
      await markDrag(gli, before.box, -25);   // up = deeper (0.008/px → +0.20)
      const mid = await gli.evaluate(() => (window.__tiRoutes()[0] || {}).v);
      await gli.mouse.up(); await wait(150);
      const after = await gli.evaluate(() => (window.__tiRoutes()[0] || {}).v);
      chk(Math.abs(mid - before.v) > 0.05 && Math.abs(after - before.v) > 0.05,
        'DEPTH DRAG — a vertical drag on the mark changes the route depth in __tiRoutes',
        before.v.toFixed(3) + ' → ' + Number(after).toFixed(3) + ' (25 px up)');
    }
    // clean click → the route list; its ✕ must really delete
    const box2 = await gli.evaluate(() => { const u = window.__fmFind(window.__fmChance());
      const r = u ? u.getBoundingClientRect() : null; return r ? { l: r.left, t: r.top, w: r.width, h: r.height } : null; });
    if (!box2) chk(false, 'ROUTE LIST ✕ — the X really deletes', 'no mark');
    else {
      await markDrag(gli, box2, 0); await gli.mouse.up(); await wait(250);
      const listed = await gli.evaluate(() => {
        const m = document.querySelector('.sm-routes');
        return m ? { rows: m.querySelectorAll('.rr').length, label: (m.querySelector('.rr span') || {}).textContent } : null; });
      const clicked = await gli.evaluate(() => {
        const x = document.querySelector('.sm-routes .rx'); if (!x) return false;
        x.dispatchEvent(new MouseEvent('click', { bubbles: true })); return true; });
      await wait(400);
      const gone = await gli.evaluate(() => ({ routes: window.__tiRoutes().length, marks: window.__fmMarksUp() }));
      chk(!!listed && listed.rows > 0 && clicked && gone.routes === 0 && gone.marks === 0,
        'ROUTE LIST ✕ — hover list shows the route and its ✕ really deletes it',
        (listed ? ('"' + listed.label + '"  ') : 'no list  ') + 'after: routes ' + gone.routes + ', marks ' + gone.marks);
    }
  }

  // ── 5  CONTAINMENT at the card's REAL bounds: left edge, right edge, bottom row ────────────
  {
    const VW = CARDS.gli.w, VH = CARDS.gli.h;
    const picks = await gli.evaluate(() => {
      const cs = window.__fmVisCells(null).map(c => { const r = window.__fmInk(c);
        return r ? { d: c.getAttribute('data-mod-dest'), l: r.left, t: r.top, r: r.right, b: r.bottom } : null; })
        .filter(Boolean);
      const byL = cs.slice().sort((a, b) => a.l - b.l)[0];
      const byR = cs.slice().sort((a, b) => b.r - a.r)[0];
      const byB = cs.slice().sort((a, b) => b.b - a.b)[0];
      const byT = cs.slice().sort((a, b) => a.t - b.t)[0];
      return { left: byL.d, right: byR.d, bottom: byB.d, top: byT.d };
    });
    const bad = [];
    for (const which of ['left', 'right', 'bottom', 'top']) {
      const dest = picks[which];
      const box = await gli.evaluate((d) => {
        window.__tiPruneFxRoutes(0, 1e9);
        window.__selMod = { lfo: 1 }; window.__tiAddRoute(0, 1, +d);
        return null; }, dest);
      await wait(450);
      const mb = await gli.evaluate((d) => {
        const c = document.querySelector('.ti-card [data-mod-dest="' + d + '"]');
        const u = window.__fmFind(c); if (!u) return null;
        const r = u.getBoundingClientRect(); return { l: r.left, t: r.top, w: r.width, h: r.height }; }, dest);
      if (!mb) { bad.push(which + ': no mark'); continue; }
      await markDrag(gli, mb, 0);                       // hold → the meter pops
      const met = await gli.evaluate(() => ({
        att: window.__fmRect('.sm-att'), head: window.__fmRect('.sm-att .vv') }));
      await gli.mouse.up(); await wait(300);            // release → the clean click pins the list
      const lst = await gli.evaluate(() => ({ list: window.__fmRect('.sm-routes'),
        mark: (function () { const u = document.querySelector('.sm-ul[style*="display: block"]') ||
          [...document.querySelectorAll('.sm-ul')].filter(x => x.style.display !== 'none')[0];
          if (!u) return null; const r = u.getBoundingClientRect();
          return { l: r.left, t: r.top, r: r.right, b: r.bottom }; })() }));
      const inside = (r) => r && r.w > 0.5 && r.h > 0.5 &&
        r.l >= -0.5 && r.t >= -0.5 && r.r <= VW + 0.5 && r.b <= VH + 0.5;
      if (!inside(met.att))  bad.push(which + ': meter ' + JSON.stringify(met.att));
      if (!inside(met.head)) bad.push(which + ': readout head ' + JSON.stringify(met.head));
      if (!inside(lst.list)) bad.push(which + ': route list ' + JSON.stringify(lst.list));
      if (lst.list && lst.mark) {
        const L = lst.list, M = lst.mark;
        const overlap = !(L.r <= M.l || L.l >= M.r || L.b <= M.t || L.t >= M.b);
        if (overlap) bad.push(which + ': the list COVERS the mark’s drag strip (fb454)');
      }
      await gli.evaluate(() => { window.__tiPruneFxRoutes(0, 1e9);
        const m = document.querySelector('.sm-routes'); if (m) m.remove(); });
      await wait(200);
    }
    chk(bad.length === 0,
      'CONTAINMENT — meter, readout head and route list stay inside ' + VW + '×' + VH + ' at every edge',
      bad.length ? bad.join(' | ') : 'left/right/bottom/top all contained, list clear of the strip');
  }

  // ── 6  MIRROR — the card's model IS the blackboard, in BOTH directions ────────────────────
  {
    await gli.evaluate(() => { window.__tiPruneFxRoutes(0, 1e9); });
    await wait(400);
    // written from OUTSIDE (as the main window's push would): the card must grow the mark
    const dest = await gli.evaluate(() => +window.__fmChance().getAttribute('data-mod-dest'));
    await gli.evaluate((d) => { window.__BB = [{ s: 0, d: d, v: 0.5 }]; }, dest);
    await wait(3200);                                   // the 2500 ms merge poll
    const up = await gli.evaluate(() => ({ routes: window.__tiRoutes().length, marks: window.__fmMarksUp() }));
    // removed from OUTSIDE: the card must let it go, not keep a private copy
    await gli.evaluate(() => { window.__BB = []; });
    await wait(3400);
    const dn = await gli.evaluate(() => ({ routes: window.__tiRoutes().length, marks: window.__fmMarksUp() }));
    chk(up.routes === 1 && up.marks === 1 && dn.routes === 0 && dn.marks === 0,
      'MIRROR — a route written/removed on the blackboard appears/disappears in the card',
      'outside-add → routes ' + up.routes + ' marks ' + up.marks +
      ' · outside-remove → routes ' + dn.routes + ' marks ' + dn.marks);
  }

  {
    chk((gli.__errs || []).length === 0, 'no page errors in the GLITCH card document',
      (gli.__errs || []).slice(0, 3).join(' | ') || 'clean');
  }

  console.log('\n   ' + pass + ' passed, ' + fail + ' failed\n');
  await browser.close();
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
