// ═══ fb608 — THE TWO-PANE BROWSER HEADER GATE ════════════════════════════════════════════════
// Max: "the 'wavetables' folder directory needs to go next to the arrows … at the very top of the
// menu header … idk about the 3 lines and separators, just looks weird … but no cramming and make
// sure the '...' gets there if there are any LONG directorys", then "actually, i want the SEARCH to
// be at the header, same rules apply."
//
// fb394 wrote this file to prove the opposite arrangement — "search never shares a row with the
// Import label" — and fb608 repeals that clause. What SURVIVES fb394 is the part that actually
// mattered: the field is CHROME, not a widget (no fill, no border, no radius, no shadow), and the
// magnifier shares ONE 14px left rail with the category letters. fb608 makes that rail hold in all
// six browsers for the first time, which is why the import label became a ＋ emblem.
//
// ⚠️ WHY THE BARS BELOW NEVER `.filter()` A MISSING MEASUREMENT AWAY. The audit of the previous
// version of this file found the rail bar computing `[railMag, railImp, railCat].filter(v => v != null)`
// — so deleting the search row made `railMag` null, the bar compared what was left, and it went
// GREEN having stopped measuring the law it exists for. Every bar here fails on a MISSING number.
//
// Mutation controls:  HDR_MUT=band | clip | noellip | ltr | nobidi | nocap | notitle | pad | tight
//                      | nofloor                                               (each must go RED)
//                     HDR_MUT=push                                 (must stay GREEN — see below)
const puppeteer = require('puppeteer-core');
const P   = require('path').join(__dirname, '..') + '/Source/ui/public/index.html';
const OUT = process.argv[2] || null;
const MUT = process.env.HDR_MUT || '';
const fs  = require('fs'); if (OUT) { try { fs.mkdirSync(OUT, { recursive: true }); } catch (e) {} }

let PASS = 0, FAIL = 0;
const gate = (ok, name, detail) => {
  if (ok) { PASS++; console.log('  ✓ ' + name + (detail ? '   ' + detail : '')); }
  else    { FAIL++; console.log('  ✗ ' + name + (detail ? '   ' + detail : '')); }
};

// every browser this component actually serves, in the shape its real caller passes
const CASES = [
  { key: 'wavetable', nav: true, rootCrumb: 'Wavetables', importLabel: '＋ Import Wavetable',
    onImport: 1, onAudition: 1, onDelete: 1 },
  { key: 'sample',  importLabel: '＋ Import Sample', onImport: 1, onAudition: 1, onDelete: 1 },
  { key: 'noise',   importLabel: '＋ Import Noise',  onImport: 1, onAudition: 1, onDelete: 1 },
  { key: 'filter',  searchPlaceholder: 'Search 118 filters…' },
  { key: 'warp',    importIcon: true, importTitle: 'Extend', onImport: 1,
    searchPlaceholder: 'Search 40 warp modes…' },
  { key: 'mods',    multi: true, searchPlaceholder: 'Search modulators…' },
];

(async () => {
  const b = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width: 1560, height: 1200, deviceScaleFactor: 2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(() => {
    const mk = () => ({ getScaledValue: () => .5, setScaledValue(){}, getNormalisedValue: () => .5, setNormalisedValue(){},
      getChoiceIndex: () => 0, setChoiceIndex(){}, getValue: () => false, setValue(){},
      valueChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      propertiesChangedEvent: { addListener(){ return { remove(){} }; }, removeListener(){} },
      properties: { start:0, end:1, interval:0, name:'', label:'', numSteps:100, choices:[], parameterIndex:0 } });
    const mine = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk,
      getNativeFunction: (n) => (...a) => new Promise(r => { if (/getPresets/i.test(n)) return r('[]');
        if (/Json|JSON/.test(n)) return r('{}'); r(0); }),
      backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
    let held = mine; Object.defineProperty(window, 'Juce', { configurable: true, get(){ return held; },
      set(v){ held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction }); } });
    window.__JUCE__ = { backend: mine.backend, initialisationData: { vendor:'', pluginName:'', pluginVersion:'',
      __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
  });
  await pg.goto('file://' + P, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1600));

  // ── the one page-side helper: open a case, apply the mutation, and hand back pure numbers ──
  await pg.evaluate((MUT) => {
    window.__hdr = function (c, opts) {
      opts = opts || {};
      if (window.__tpbClose) { try { window.__tpbClose(); } catch (e) {} }
      window.__impFired = 0;
      const it = (n, p) => ({ name: p + ' ' + n, pick(){} });
      const many = (n, p) => Array.from({ length: n }, (_, i) => it(i + 1, p));
      const cats = [
        { label: 'Basic Shapes', items: many(6, 'B') },
        { label: 'Analog',       items: many(9, 'A') },
        /* ⚠️ 3 LEVELS DEEP ON PURPOSE. A sub with `subs: []` is a LEAF — clicking it selects and
           does not descend — so a two-level fixture silently tests a one-level path and the
           ellipsis bar passes on a path that never got long. */
        { label: 'TERRAIN-WAVETABLES', items: many(6, 'T'),
          subs: [ { label: 'CHAOS', items: many(4, 'C'), subs: [] },
                  { label: 'SPECTRAL FORMANT SHIFT', items: many(4, 'S'),
                    subs: [ { label: 'INHARMONIC PARTIALS', items: many(3, 'P'), subs: [] } ] },
                  /* a non-Latin folder name, because the RTL box the ellipsis needs will reorder the
                     whole path around one if the inner span loses its LTR embedding */
                  { label: 'צלילים', items: many(2, 'H'),
                    subs: [ { label: 'DEEPER', items: many(2, 'D'), subs: [] } ] } ] },
      ];
      if (c.onDelete) cats.push({ label: 'My Import Folder', delKind: 'folder', delPath: '/x', items: many(3, 'I') });
      const cfg = { cats: cats, openCat: 0 };
      if (c.nav) { cfg.nav = true; cfg.rootCrumb = c.rootCrumb; }
      if (c.importLabel)       cfg.importLabel = c.importLabel;
      if (c.importIcon)        { cfg.importIcon = true; cfg.importTitle = c.importTitle; }
      if (c.onImport)          cfg.onImport = function () { window.__impFired++; };
      if (c.onAudition)        cfg.onAudition = function () {};
      if (c.onDelete)          cfg.onDelete = function () {};
      if (c.multi)             cfg.multi = true;
      if (c.searchPlaceholder) cfg.searchPlaceholder = c.searchPlaceholder;
      if (opts.noSearch)       cfg.search = false;
      const panel = window.openTwoPaneBrowser({ clientX: 300, clientY: 160 }, cfg);
      const head  = panel.children[0];
      const crumbBox = panel.querySelector('.tpb-crumb');
      const srowEarly = panel.querySelector('.tpb-srow');
      const right = head.children[head.children.length - 1];

      /* ── MUTATIONS: each puts the header back into a state fb608 removed, so a green bar here
            would mean the bar cannot see the thing it claims to measure. ── */
      if (MUT === 'band' && panel.querySelector('.tpb-srow')) {          // search gets its own band again (pre-fb608)
        const row = panel.querySelector('.tpb-srow');
        row.style.cssText = 'display:flex;align-items:center;gap:6px;height:26px;padding:0 9px 0 14px;flex:none;border-bottom:1px solid rgba(255,255,255,0.05);';
        panel.insertBefore(row, panel.children[1]);
        panel.style.height = (parseFloat(panel.style.height) + 26) + 'px';
      }
      /* ⚠️ `push` IS THE CONTROL THAT IS *SUPPOSED* TO STAY GREEN, and it is here on purpose.
         The folklore says `min-width:0` is what lets a nowrap flex child shrink and ellipse. It is
         not: Flexbox §4.5 already clamps the automatic minimum size to zero on any box whose
         overflow is not visible, and this one is overflow:hidden. Strip min-width and NOTHING
         changes — which is exactly what this mutation demonstrates, and why the comment in
         index.html no longer credits it. `clip` below is the one that finds the real mechanism. */
      if (MUT === 'push' && crumbBox) crumbBox.children[1].style.minWidth = 'auto';
      /* the REAL mechanism: without overflow:hidden on the TEXT there is no ellipsis at all — the
         text grows to its full width and the box around it simply cuts it off mid-word. */
      if (MUT === 'clip'   && crumbBox) crumbBox.children[1].style.overflow = 'visible';
      /* the "…" back on the WRONG END — a deep path would then show where you started, not where
         you are. This is a RENDER claim, so bar [8] proves it from glyph positions, not from
         getComputedStyle: a style assertion would pass on any value someone typed. */
      if (MUT === 'ltr'    && crumbBox) crumbBox.children[1].style.direction = 'ltr';
      /* strip the inner span's LTR embedding and the bidi algorithm reorders the "›" separators —
         the path renders backwards inside the RTL box. */
      if (MUT === 'nobidi' && crumbBox && crumbBox.children[1].firstChild)
        crumbBox.children[1].firstChild.style.cssText = '';
      if (MUT === 'noellip'&& crumbBox) crumbBox.children[1].style.textOverflow = 'clip';
      if (MUT === 'nocap' && crumbBox) crumbBox.style.maxWidth = 'none'; // no cap → a long path widens the box
      if (MUT === 'notitle') { const p = head.querySelector('[title]');   // the words vanish with the label
        [].slice.call(head.querySelectorAll('[title]')).forEach(e => { if (/import/i.test(e.title)) e.title = ''; }); }
      if (MUT === 'pad') { const r = panel.querySelector('.tpb-srow');    // the magnifier off the 14px rail
        if (r) r.style.paddingLeft = '0px'; }
      /* fb609 — the path back HARD against the ‹, which is what fb608 shipped and Max rejected on
         sight: "the directory is WAY too close to the ARROWS." Bar [6] used to ASSERT this state. */
      if (MUT === 'tight' && crumbBox) crumbBox.style.marginRight = '0px';

      /* descend, so the crumb carries a real path */
      if (opts.descend || opts.descendRTL) {
        const hit = re => { for (const r of [].slice.call(panel.querySelectorAll('.tpb-pane > div')))
                              if (re.test(r.textContent.trim()) && r.onclick) { r.onclick({}); return true; }
                            return false; };
        hit(/^TERRAIN-WAVETABLES/);
        if (opts.descendRTL)      hit(/^\u05e6/);              // צלילים
        else if (opts.descend > 1) hit(/^SPECTRAL/);
      }
      /* ⚠️ CROWD THE CLUSTER — the only way to reach SRCH_MIN. In every shipping configuration the
         seat's natural flex basis already keeps it above its floor, so the floor is a guard for a
         cluster that grows later. These stand-in glyphs ARE that later cluster. */
      if (opts.crowd) for (let i = 0; i < opts.crowd; i++) {
        const d = document.createElement('div');
        d.style.cssText = 'flex:none;width:15px;height:13px;';
        right.insertBefore(d, right.lastChild);
      }
      /* select the deletable folder, so "Delete Folder" claims part of the row */
      if (opts.pickDeletable) {
        const rows = [].slice.call(panel.querySelectorAll('.tpb-pane > div'));
        for (const r of rows) if (/^My Import Folder/.test(r.textContent.trim()) && r.onclick) { r.onclick({}); break; }
      }

      const rp = panel.getBoundingClientRect(), K = rp.width / 384;      // the UI renders under a transform — divide it out
      const box = e => { if (!e) return null; const r = e.getBoundingClientRect();
        return { l: +((r.left - rp.left) / K).toFixed(1), r: +((r.right - rp.left) / K).toFixed(1),
                 t: +((r.top - rp.top) / K).toFixed(1),  w: +(r.width / K).toFixed(1), h: +(r.height / K).toFixed(1) }; };
      const rail = e => { if (!e) return null;
        return +(box(e).l + parseFloat(getComputedStyle(e).paddingLeft || 0)).toFixed(2); };

      const kids  = [].slice.call(panel.children);
      if (MUT === 'nofloor' && srowEarly) srowEarly.style.minWidth = '0px';
      const inp   = panel.querySelector('input.tpb-srch');
      const srow  = panel.querySelector('.tpb-srow');
      const mag   = srow ? srow.querySelector('svg') : null;
      const cat0  = panel.querySelector('.tpb-pane > div');
      const navBox = (c.nav && right.children.length) ? right.children[0] : null;
      const plus  = [].slice.call(right.querySelectorAll('div')).filter(e => /import/i.test(e.title || ''))[0] || null;
      const crumbT = crumbBox ? crumbBox.children[1] : null;

      /* every VISIBLE leaf box on the header row, for the overlap test */
      const leaves = [];
      const walk = e => { [].slice.call(e.children).forEach(ch => {
        if (!ch.offsetWidth || getComputedStyle(ch).display === 'none') return;
        if (ch.children.length && !ch.querySelector('input') && ch.tagName !== 'SVG' && !ch.querySelector('svg'))
          walk(ch);
        else leaves.push({ tag: (ch.title || ch.className || ch.textContent || ch.tagName).toString().slice(0, 18).trim(), b: box(ch) }); }); };
      walk(head);
      let overlap = [];
      for (let i = 0; i < leaves.length; i++) for (let j = i + 1; j < leaves.length; j++)
        if (leaves[i].b.r > leaves[j].b.l + 0.5 && leaves[j].b.r > leaves[i].b.l + 0.5)
          overlap.push(leaves[i].tag + '×' + leaves[j].tag);

      const R = {
        panel: { w: +(rp.width / K).toFixed(1), h: +(rp.height / K).toFixed(1) },
        bands: kids.length - 1,
        bandH: kids.slice(0, kids.length - 1).map(e => +(e.getBoundingClientRect().height / K).toFixed(0)).join('+'),
        paneH: +(kids[kids.length - 1].children[0].getBoundingClientRect().height / K).toFixed(1),
        headKids: [].slice.call(head.children).map(e => (e.className || 'slot') + ':' + Math.round(e.getBoundingClientRect().width / K)).join(' '),
        hasInput: !!inp, inputInHead: !!(inp && head.contains(inp)),
        srowIsFirstVisible: !!(srow && [].slice.call(head.children).filter(e => e.offsetWidth > 0)[0] === srow),
        railMag: rail(mag), railCat: rail(cat0),
        inpW: inp ? +(inp.getBoundingClientRect().width / K).toFixed(1) : null,
        overlap: overlap,
        leaves: leaves.map(o => o.tag + '[' + o.b.l + '..' + o.b.r + ']').join(' '),
        plusExists: !!plus, plusInRight: !!(plus && right.contains(plus)),
        plusTitle: plus ? plus.title : null,
        crumbExists: !!crumbBox, crumbInHead: !!(crumbBox && head.contains(crumbBox)),
        crumbBox: box(crumbBox), navBox: box(navBox), rightBox: box(right),
        crumbText: crumbT ? crumbT.textContent : null,
        crumbTitle: crumbT ? crumbT.title : null,
        /* ⚠️ scrollWidth > clientWidth ALONE IS NOT AN ELLIPSIS — it is only "the text is wider
           than its box", which is equally true of a hard clip that cuts a word in half. The two
           computed properties below are the difference between "…" and a severed word, and the
           `clip` / `noellip` mutations exist because the first version of this bar measured the
           overflow only and stayed GREEN through both of them. */
        crumbEllipsed: crumbT ? (crumbT.scrollWidth > crumbT.clientWidth + 0.5) : null,
        crumbOv:  crumbT ? getComputedStyle(crumbT).overflowX : null,
        crumbTov: crumbT ? getComputedStyle(crumbT).textOverflow : null,
        /* ⚠️ WHICH END IS CLIPPED, MEASURED OFF THE GLYPHS THEMSELVES. A Range around the first and
           the last character says where each one actually landed, so this catches a reordered path
           (bidi) AND an ellipsis on the wrong end — neither of which any style read can see. */
        crumbChars: (function () {
          if (!crumbT) return null;
          const span = crumbT.firstChild, tn = span && span.firstChild;
          if (!tn || tn.nodeType !== 3 || !tn.data.length) return null;
          const at = i => { const r = document.createRange(); r.setStart(tn, i); r.setEnd(tn, i + 1);
                            const b = r.getBoundingClientRect();
                            return { l: +((b.left - rp.left) / K).toFixed(1), r: +((b.right - rp.left) / K).toFixed(1) }; };
          const b = box(crumbT);
          return { first: at(0), last: at(tn.data.length - 1), boxL: b.l, boxR: b.r };
        })(),
        delShown: (function () { const d = [].slice.call(right.children).filter(e => /Delete/i.test(e.title || ''))[0];
                                 return !!(d && getComputedStyle(d).display !== 'none'); })(),
      };
      if (inp) { const cs = getComputedStyle(inp);
        R.box = { bg: cs.backgroundColor, bd: [cs.borderTopWidth, cs.borderRightWidth, cs.borderBottomWidth, cs.borderLeftWidth].join('/'),
                  rad: cs.borderRadius, sh: cs.boxShadow }; }
      if (plus && !opts.noClick) { plus.onclick(); R.plusFired = window.__impFired; }   // ⚠️ this CLOSES the panel (that is the shipping behaviour)
      R.close = () => {};
      return JSON.parse(JSON.stringify(R));
    };
  }, MUT);

  console.log('══ fb608 TWO-PANE BROWSER HEADER ══   mutation: ' + (MUT || '(none)'));

  const M = {};
  for (const c of CASES) M[c.key] = await pg.evaluate(c => window.__hdr(c, {}), c);

  // ── [0] ONE BAND OF CHROME, and the list keeps every pixel it had ────────────────────────────
  const shapes = CASES.map(c => c.key + ' ' + M[c.key].panel.w + '×' + M[c.key].panel.h + '/' + M[c.key].bands + 'band');
  gate(CASES.every(c => M[c.key].panel.w === 384 && M[c.key].panel.h === 294 && M[c.key].bands === 1),
       '[0] ONE 31px BAND, PANEL 384×294 IN ALL SIX', shapes.join('  '));
  const panes = CASES.map(c => M[c.key].paneH);
  gate(panes.every(v => Math.abs(v - panes[0]) < 0.6) && panes[0] > 250,
       '[1] THE LIST IS THE SAME HEIGHT IN ALL SIX (chrome added to the panel, never stolen from the list)',
       'panes ' + panes.join(' / '));

  // ── [2] SEARCH LEADS THE ROW, ON THE 14px RAIL ──────────────────────────────────────────────
  const railBad = CASES.filter(c => { const m = M[c.key];
    return !(m.hasInput && m.inputInHead && m.srowIsFirstVisible
             && m.railMag != null && m.railCat != null && Math.abs(m.railMag - m.railCat) <= 0.6); });
  gate(railBad.length === 0, '[2] SEARCH IS FIRST IN THE HEADER AND ON THE CATEGORIES’ 14px RAIL',
       CASES.map(c => c.key + ' mag=' + M[c.key].railMag + '/cat=' + M[c.key].railCat).join('  ')
       + (railBad.length ? '   ← BROKEN: ' + railBad.map(c => c.key).join(',') : ''));

  // ── [3] NO CRAMMING — a pixel test, not an opinion ──────────────────────────────────────────
  const over = CASES.filter(c => M[c.key].overlap.length);
  gate(over.length === 0, '[3] NO TWO THINGS ON THE HEADER ROW OVERLAP',
       over.length ? over.map(c => c.key + ': ' + M[c.key].overlap.join(' ')).join(' | ')
                   : 'wavetable row → ' + M.wavetable.leaves);
  const minSrch = Math.min(...CASES.map(c => M[c.key].inpW));
  /* fb609 — the floor came down from 96 with Max's blessing ("i don't mind the search getting a
     little smaller") to buy the path its air. 74 is still ~12 characters against a 42px
     placeholder — room to TYPE, not merely to read the prompt. */
  gate(minSrch >= 74, '[4] THE FIELD IS STILL A FIELD IN EVERY BROWSER (≥ 74px)',
       CASES.map(c => c.key + ' ' + M[c.key].inpW).join(' / '));

  // ── [5] THE ＋ CARRIES THE WORDS, AND IT STILL IMPORTS ──────────────────────────────────────
  const impCases = CASES.filter(c => c.onImport && !c.importIcon);
  gate(impCases.every(c => M[c.key].plusExists && M[c.key].plusInRight
                        && M[c.key].plusTitle && M[c.key].plusTitle.length > 3
                        && c.importLabel.indexOf(M[c.key].plusTitle) >= 0
                        && M[c.key].plusFired === 1),
       '[5] THE ＋ EMBLEM IS IN THE CLUSTER, KEEPS THE LABEL’S WORDS, AND FIRES onImport',
       impCases.map(c => c.key + ' "' + M[c.key].plusTitle + '" fired=' + M[c.key].plusFired).join('  '));

  // ── [6] THE PATH HAS REAL AIR ON BOTH SIDES ─────────────────────────────────────────────────
  /* ⚠️ THIS BAR USED TO ASSERT THE BUG. fb608 read "put it next to the arrows" literally and gated
     `|crumb.right − nav.left| <= 1` — a GREEN BAR CERTIFYING A COLLISION. Max, on the screenshot:
     "the directory is WAY too close to the ARROWS." The gap is now the claim. */
  const NAV_AIR = 42;
  const wv = M.wavetable, air = wv.navBox ? +(wv.navBox.l - wv.crumbBox.r).toFixed(1) : null;
  gate(wv.crumbExists && wv.crumbInHead && air != null && Math.abs(air - NAV_AIR) <= 1.5,
       '[6] THE PATH IS IN THE HEADER WITH ' + NAV_AIR + 'px OF AIR BEFORE THE ‹ ›',
       'path ends ' + wv.crumbBox.r + ', arrows start ' + wv.navBox.l + '  →  ' + air + 'px'
       + (air < 8 ? '   *** TOUCHING ***' : ''));
  gate(CASES.filter(c => !c.nav).every(c => !M[c.key].crumbExists),
       '[7] AND IT DOES NOT EXIST WHERE THERE IS NO NAVIGATION',
       CASES.filter(c => !c.nav).map(c => c.key + '=' + M[c.key].crumbExists).join(' '));

  // ── [8] LONG PATHS ELLIPSE — AND MOVE NOTHING ───────────────────────────────────────────────
  const deep = await pg.evaluate(c => window.__hdr(c, { descend: 2 }), CASES[0]);
  const stillPut = JSON.stringify(deep.rightBox) === JSON.stringify(wv.rightBox);
  const ch = deep.crumbChars;
  /* the head is clipped (first char sits LEFT of the box) and the tail is not (last char is inside)
     — that IS "the … is on the left", read off the rendered glyphs. And first-left-of-last proves
     the path did not come out backwards. */
  const headClipped = !!ch && ch.first.l < ch.boxL - 1 && ch.last.r <= ch.boxR + 1 && ch.first.l < ch.last.l;
  gate(deep.crumbEllipsed === true && deep.crumbOv === 'hidden' && deep.crumbTov === 'ellipsis' && headClipped
       && deep.crumbTitle && deep.crumbTitle.indexOf('SPECTRAL') >= 0
       && deep.crumbTitle === deep.crumbText && stillPut && deep.overlap.length === 0
       && Math.abs(deep.crumbBox.w - 102) <= 1,   /* the CAP binds here — see [9] for the FLOOR */
       '[8] A LONG PATH ELLIPSES INSIDE A CAPPED BOX — THE ICONS DO NOT MOVE A PIXEL',
       'path "' + deep.crumbText + '"  box ' + deep.crumbBox.w + 'px (cap 102)  field ' + deep.inpW
       + 'px  overflows=' + deep.crumbEllipsed + '  first char at ' + (ch && ch.first.l) + ', last at '
       + (ch && ch.last.r) + ', box ' + (ch && ch.boxL) + '..' + (ch && ch.boxR)
       + '  → ' + (headClipped ? 'HEAD clipped, tail visible (… on the LEFT)' : '*** WRONG END ***')
       + '  cluster ' + (stillPut ? 'IDENTICAL' : 'MOVED ' + JSON.stringify(wv.rightBox) + '→' + JSON.stringify(deep.rightBox)));

  // ── [8b] A NON-LATIN FOLDER NAME MUST NOT TURN THE PATH ROUND ───────────────────────────────
  const heb = await pg.evaluate(c => window.__hdr(c, { descendRTL: 1, noClick: 1 }), CASES[0]);
  const hc = heb.crumbChars;
  gate(!!hc && hc.first.l < hc.last.l && /\u05e6\u05dc\u05d9\u05dc\u05d9\u05dd/.test(heb.crumbTitle || ''),
       '[8b] A HEBREW-NAMED FOLDER DOES NOT REVERSE THE PATH (the inner span’s LTR embedding)',
       'path "' + heb.crumbTitle + '"   first char at ' + (hc && hc.first.l) + ', last at ' + (hc && hc.last.l)
       + '  → ' + (hc && hc.first.l < hc.last.l ? 'reads left-to-right' : '*** RENDERED BACKWARDS ***'));

  // ── [9] "DELETE FOLDER" JOINS THE ROW AND STILL NOTHING BREAKS ──────────────────────────────
  const del = await pg.evaluate(c => window.__hdr(c, { pickDeletable: true }), CASES[0]);
  /* the FLOOR's own regime: the trash glyph widens the cluster, the path drops BELOW its cap, and
     the seat holds at SRCH_MIN. If this and [8] ever report the same path width, one of the two
     constants has stopped doing anything. */
  gate(del.delShown && del.overlap.length === 0 && del.inpW >= 74 && del.crumbBox.w < 100,
       '[9] THE TRASH GLYPH JOINS THE ROW — THE PATH GIVES WAY, THE FIELD HOLDS ITS FLOOR',
       'delete shown=' + del.delShown + '  field ' + del.inpW + 'px  overlap=' + (del.overlap.join(',') || 'none')
       + '  path ' + (del.crumbBox && del.crumbBox.w) + 'px');

  // ── [9b] TYPING A LONG QUERY MUST NOT REACH THE PATH ────────────────────────────────────────
  /* the input scrolls its own text, so a long query must never widen the seat and shove the path.
     Measured with the field full, not empty — the state the resting screenshot cannot show. */
  const typed = await pg.evaluate(async c => {
    const r = window.__hdr(c, { descend: 2, noClick: 1 });
    const inp = document.querySelector('input.tpb-srch');
    inp.value = 'TERRA SIERPINSKI GASKET LADDER'; inp.oninput();
    await new Promise(z => setTimeout(z, 30));
    const panel = document.querySelector('.tpb-panel'), head = panel.children[0];
    const rp = panel.getBoundingClientRect(), K = rp.width / 384;
    const X = e => { const b = e.getBoundingClientRect();
      return { l: +((b.left - rp.left) / K).toFixed(1), r: +((b.right - rp.left) / K).toFixed(1) }; };
    const cr = X(panel.querySelector('.tpb-crumb')), nb = X(head.children[head.children.length - 1].children[0]);
    return { seat: X(panel.querySelector('.tpb-srow')), crumb: cr, air: +(nb.l - cr.r).toFixed(1),
             overlaps: X(panel.querySelector('.tpb-srow')).r > cr.l + 0.5 };
  }, CASES[0]);
  gate(!typed.overlaps && Math.abs(typed.air - NAV_AIR) <= 1.5,
       '[9b] A LONG QUERY SCROLLS INSIDE THE FIELD — IT DOES NOT PUSH THE PATH',
       'seat ends ' + typed.seat.r + ', path starts ' + typed.crumb.l + ', air before ‹ › ' + typed.air + 'px');

  // ── [9c] AND THE SEAT'S FLOOR ENGAGES WHEN THE CLUSTER GROWS ────────────────────────────────
  const crowd = await pg.evaluate(c => window.__hdr(c, { descend: 2, crowd: 3, noClick: 1 }), CASES[0]);
  gate(crowd.inpW >= 74 && crowd.crumbBox.w < 45 && crowd.overlap.length === 0,
       '[9c] THREE MORE GLYPHS IN THE CLUSTER — THE PATH COLLAPSES, THE FIELD STOPS AT ITS FLOOR',
       'field ' + crowd.inpW + 'px  path ' + crowd.crumbBox.w + 'px  overlap=' + (crowd.overlap.join(',') || 'none'));

  // ── [10] fb394's SURVIVING LAW: the field is chrome, not a widget ───────────────────────────
  const boxy = CASES.filter(c => { const x = M[c.key].box; return !x
    || !/rgba\(0, 0, 0, 0\)|transparent/.test(x.bg) || !/^0px\/0px\/0px\/0px$/.test(x.bd)
    || !/^0px$/.test(x.rad) || !/none/.test(x.sh); });
  gate(boxy.length === 0, '[10] THE FIELD DRAWS NO BOX — no fill, no border, no radius, no shadow (fb394)',
       JSON.stringify(M.wavetable.box));

  // ── [11] search:false must not throw the icons to the left ─────────────────────────────────
  const nos = await pg.evaluate(c => window.__hdr(c, { noSearch: true }), CASES[1]);
  gate(!nos.hasInput && nos.rightBox.r > 360 && nos.panel.h === 294,
       '[11] WITH search:false THE ICON CLUSTER STAYS RIGHT (the empty left slot is load-bearing)',
       'cluster ' + nos.rightBox.l + '..' + nos.rightBox.r + ' of 384, panel h=' + nos.panel.h);

  // ── [12] the field still searches, clears and closes ────────────────────────────────────────
  const fn = await pg.evaluate(async () => {
    if (window.__tpbClose) { try { window.__tpbClose(); } catch (e) {} }
    const cats = [ { label: 'Ladder',  items: [{ name: 'Ladder LP 24', pick(){} }, { name: 'Acid 303', pick(){} }] },
                   { label: 'Vintage', items: [{ name: 'Acid Scream', pick(){} }, { name: 'SEM Notch', pick(){} }] } ];
    const p = window.openTwoPaneBrowser({ clientX: 300, clientY: 200 }, { cats, openCat: 0, searchPlaceholder: 'Search 118 filters…' });
    const inp = p.querySelector('input');
    const rows = () => [].slice.call(p.querySelectorAll('div'))
      .filter(d => /^(Ladder LP 24|Acid 303|Acid Scream|SEM Notch)$/.test(d.textContent.trim())).map(d => d.textContent.trim());
    inp.value = 'acid'; inp.oninput();
    const hits = rows().join(' | ');
    inp.onkeydown({ key: 'Escape', stopPropagation(){} });
    const cleared = inp.value === '';
    inp.onkeydown({ key: 'Escape', stopPropagation(){} });
    const closed = !document.body.contains(p);
    await new Promise(r => setTimeout(r, 20));
    if (window.__tpbClose) { try { window.__tpbClose(); } catch (e) {} }
    const p2 = window.openTwoPaneBrowser({ clientX: 300, clientY: 200 }, { cats, openCat: 0 });
    await new Promise(r => setTimeout(r, 80));
    const focused = document.activeElement === p2.querySelector('input');
    if (window.__tpbClose) { try { window.__tpbClose(); } catch (e) {} }
    return { hits, cleared, closed, focused };
  });
  gate(fn.hits === 'Acid 303 | Acid Scream' && fn.cleared && fn.closed && fn.focused,
       '[12] SEARCH STILL SEARCHES ACROSS CATEGORIES, Esc CLEARS THEN CLOSES, OPENS FOCUSED',
       '"acid" → ' + fn.hits + '   cleared=' + fn.cleared + ' closed=' + fn.closed + ' focused=' + fn.focused);

  if (OUT) { for (const c of CASES) {
    await pg.evaluate(c => window.__hdr(c, c.key === 'wavetable' ? { descend: 2, noClick: 1 } : { noClick: 1 }), c);
    const el = await pg.$('.tpb-panel'); if (el) await el.screenshot({ path: OUT + '/tpb_' + c.key + '.png' }); } }

  gate(errs.length === 0, '[13] NO PAGE ERRORS', errs.length ? errs.slice(0, 2).join(' | ') : 'clean');
  console.log('\n  ' + PASS + ' pass, ' + FAIL + ' fail' + (MUT ? '   (mutation: ' + MUT + ')' : ''));
  await b.close();
  process.exit(FAIL ? 1 : 0);
})();
