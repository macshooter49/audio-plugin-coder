// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_header_gate.js — fb595: THE HARMONICS HEADER NEVER COLLIDES, THE ARROWS ARE CENTRED,
//                        AND NOTHING MOVES.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/harm_header_gate.js [page.html]
//
//  Max, with a screenshot reading "OSC TaHarmonicKeel ‹ PLUTO 2 - JNO … ›": "it still collides ...
//  because our table names are longer ... the arrows are way taller than the names ... make sure
//  the names don't overlap no matter what ... it gets cut off really early ... dot, dot, dot ...
//  the arrows centered and symmetrical just like everywhere ... Nothing moves. Everything has to
//  stay fixed ... run a bunch of tests on it."
//
//  WHY fb588's OWN GATE PASSED WHILE FOUR WORDS PAINTED ON TOP OF EACH OTHER: harm_wtmenu_gate.js
//  asserts `display !== 'none'` on every bar and never measures a rectangle. This gate measures
//  nothing BUT rectangles, across every state the header can be in:
//      7 families × 6 sculpt words × 3 name lengths = 126 header states, plus the wavetable
//      engine's own arrows measured in the same run as the reference.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below may be asserted on a page that never rendered
//   1  NO OVERLAP, EVER — no two visible header words share pixels in ANY of the 126 states, and
//      the label row keeps ≥ 4 px of clear air to the corner cluster (WebKit and Chromium differ
//      in sub-pixel font advance; a 0.7 px clearance is a collision waiting for a font update)
//   2  THE NAME GIVES, THE CHIPS DON'T — on Table with a long name the NAME ends in … and the
//      Table / sculpt chips are intact (a flex-shrink fix that chews "Ta…" fails this bar)
//   3  THE ARROWS ARE CENTRED — each ‹ › glyph sits centred in its box (equal insets) and on the
//      name's text centreline, and Table's arrows measure IDENTICAL to the wavetable engine's
//   4  THE ARROWS ARE SYMMETRIC — equal box gaps either side of the name, every sculpt, every name
//   5  NOTHING MOVES — for a given sculpt word, every header element except the name's text sits
//      at the SAME pixels across all three name lengths; and across all 126 states the label row
//      and the A slot never move at all
//   6  OTHER ENGINES ARE UNTOUCHED — the wavetable engine's cluster has no width clamp and its
//      arrows come from the canonical rule (the fix is scoped by selector, and this proves it)
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');
const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');

const STUB = () => {
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/Json|JSON/.test(n))return r('{}'); r(0);}),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const m=window.Juce;let h=m;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return h;},set(v){h=Object.assign({},v||{},{getNativeFunction:m.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

(async () => {
  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox','--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument (STUB);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1600));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2400));

  const r = await p.evaluate (() => {
    const out = { err: null, states: [], ref: null };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }
      const famSel = dev.querySelector ('.hm-mode-select');
      const scSel  = dev.querySelector ('.hm-sculpt-select');
      if (! famSel || ! scSel) { out.err = 'no family/sculpt select'; return out; }
      const devR = () => dev.getBoundingClientRect();
      out.devW = devR().width;
      out.fams = [...famSel.options].map (o => o.textContent);
      out.sculpts = [...scSel.options].map (o => o.textContent);

      const NAMES = { short: 'Sine', long: 'PLUTO 2 - JNO CORE', xlong: 'PLUTO 2 - JNO CORE EXTENDED MIX' };
      const R = (el) => { const q = el.getBoundingClientRect(), d = devR();
        return { x: +(q.left - d.left).toFixed (2), y: +(q.top - d.top).toFixed (2), w: +q.width.toFixed (2), h: +q.height.toFixed (2) }; };
      const ink = (el) => {   // the text node's own box — where the glyph actually is inside its box
        const tn = [...el.childNodes].find (n => n.nodeType === 3 && n.textContent.trim());
        if (! tn) return null; const rg = document.createRange(); rg.selectNodeContents (tn);
        const q = rg.getBoundingClientRect(), d = devR();
        return { x: +(q.left - d.left).toFixed (2), y: +(q.top - d.top).toFixed (2), w: +q.width.toFixed (2), h: +q.height.toFixed (2) }; };
      const vis = (el) => { const cs = getComputedStyle (el);
        return cs.display !== 'none' && cs.visibility !== 'hidden' && parseFloat (cs.opacity) !== 0
            && el.getBoundingClientRect().width > 0; };
      const put = (id, t) => { const e = document.getElementById (id); if (! e) return;
        if (e.tagName === 'SELECT') { if (! e.options.length) e.add (new Option (t, '0'));
          e.options[Math.max (0, e.selectedIndex)].textContent = t; } else e.textContent = t; };

      const label = dev.querySelector ('.device-label-row');
      const corner = dev.querySelector ('.device-corner-preset');
      const eng = document.getElementById ('osc-a-engine-display');
      const nameDisp = document.getElementById ('osc-a-preset-display');
      const nameSel  = document.getElementById ('osc-a-preset-select');
      const famDisp  = document.getElementById ('osc-a-harmmode-display');
      const scDisp   = document.getElementById ('osc-a-harmsculpt-display');

      // ── the REFERENCE: the wavetable engine's own arrows, long name ───────────────────────────
      const measureArrows = () => {
        const navs = [...dev.querySelectorAll ('.wt-nav')].filter (vis);
        if (navs.length !== 2) return { n: navs.length };
        const [L, Rr] = navs.map (n => ({ box: R (n), ink: ink (n), disp: getComputedStyle (n).display }));
        const nb = R (nameDisp), ni = ink (nameDisp), wb = R (nameDisp.parentElement);   // wb = the SLOT (the flex item); nb = the label inside it
        const insetL = (a) => +(a.ink.x - a.box.x).toFixed (2), insetR = (a) => +((a.box.x + a.box.w) - (a.ink.x + a.ink.w)).toFixed (2);
        const cy = (q) => q.y + q.h / 2;
        return { n: 2,
          L: { insetL: insetL (L), insetR: insetR (L), dy: +(cy (L.ink) - cy (ni)).toFixed (2), disp: L.disp, box: L.box },
          R: { insetL: insetL (Rr), insetR: insetR (Rr), dy: +(cy (Rr.ink) - cy (ni)).toFixed (2), disp: Rr.disp, box: Rr.box },
          gapL: +(nb.x - (L.box.x + L.box.w)).toFixed (2), gapR: +(Rr.box.x - (nb.x + nb.w)).toFixed (2),
          gapLw: +(wb.x - (L.box.x + L.box.w)).toFixed (2), gapRw: +(Rr.box.x - (wb.x + wb.w)).toFixed (2),
          dispOff: +(nb.x - wb.x).toFixed (2) };   // the label's offset inside its slot — Max's deliberate 1 px nudge (.preset-display left:1px)
      };
      const ENG = ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'];
      ENG.forEach (c => dev.classList.remove (c)); dev.classList.remove ('harm-table');
      put ('osc-a-engine-display', 'Wavetable'); put ('osc-a-preset-display', NAMES.long); put ('osc-a-preset-select', NAMES.long);
      out.ref = measureArrows();
      out.refNameGrow = getComputedStyle (nameDisp.parentElement).flexGrow;
      out.refCornerMaxW = getComputedStyle (corner).maxWidth; out.refCornerW = getComputedStyle (corner).width;

      // ── the 126 harmonics states ──────────────────────────────────────────────────────────────
      dev.classList.add ('engine-harm');
      put ('osc-a-engine-display', 'Harmonic');
      for (let f = 0; f < out.fams.length; ++f) {
        famSel.value = String (f); famSel.dispatchEvent (new Event ('change', { bubbles: true }));
        put ('osc-a-harmmode-display', out.fams[f]);
        for (let sc = 0; sc < out.sculpts.length; ++sc) {
          scSel.value = String (sc); scSel.dispatchEvent (new Event ('change', { bubbles: true }));
          put ('osc-a-harmsculpt-display', out.sculpts[sc]);
          for (const nk of Object.keys (NAMES)) {
            put ('osc-a-preset-display', NAMES[nk]); put ('osc-a-preset-select', NAMES[nk]);
            const st = { f, fam: out.fams[f], sc, sculpt: out.sculpts[sc], nk, isTable: dev.classList.contains ('harm-table') };
            // every visible text-bearing element in the header band, with its box
            const items = [];
            dev.querySelectorAll ('*').forEach (el => {
              if (! vis (el)) return; const q = R (el);
              if (q.y < -4 || q.y > 42) return;
              let t = ''; for (const n of el.childNodes) if (n.nodeType === 3) t += n.textContent;
              if (! t.trim() && el.tagName === 'SELECT') return;   // the transparent overlay twin — same slot by design
              t = t.replace (/\s+/g, ' ').trim(); if (! t) return;
              items.push ({ t, id: el.id || '', cls: (el.className || '').toString().split (/\s+/)[0], ...q });
            });
            st.items = items;
            st.overlaps = [];
            for (let i = 0; i < items.length; ++i) for (let j = i + 1; j < items.length; ++j) {
              const a = items[i], c = items[j];
              const ox = Math.min (a.x + a.w, c.x + c.w) - Math.max (a.x, c.x), oy = Math.min (a.y + a.h, c.y + c.h) - Math.max (a.y, c.y);
              if (ox > 0.5 && oy > 0.5) st.overlaps.push (`"${a.t}"×"${c.t}" ${ox.toFixed (1)}px`);
            }
            const lr = R (label), cr = R (corner);
            st.clear = +(cr.x - (lr.x + lr.w)).toFixed (2);
            st.labelRect = lr; st.cornerRect = cr;
            st.aRect = R (dev.querySelector ('.osc-letter'));
            // the "nothing moves" signature: every corner child except the name display
            st.fixedSig = [...corner.children].filter (vis).map (ch => {
              const q = R (ch); const isName = ch.contains (nameDisp);
              return isName ? `name@${q.x + q.w}|${q.y}` : `${(ch.className||'').toString().split(/\s+/)[0]}@${q.x},${q.y},${q.w},${q.h}`;   // the name pins its RIGHT edge
            }).join (' ');
            if (st.isTable) {
              st.nameEllipsis = nameDisp.scrollWidth > nameDisp.clientWidth + 0.5;
              st.chipIntact = famDisp.scrollWidth <= famDisp.clientWidth + 0.5 && scDisp.scrollWidth <= scDisp.clientWidth + 0.5;
              st.nameBoxW = R (nameDisp).w;
              st.arrows = measureArrows();
            }
            out.states.push (st);
          }
        }
      }
    } catch (e) { out.err = String (e && e.stack || e); }
    return out;
  });

  console.log (`\n══ harm_header_gate — fb595 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.devW > 100 && r.states.length === 7 * 6 * 3 && r.ref && r.ref.n === 2;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — 126 header states + the wavetable reference',
        `osc width ${(r.devW||0).toFixed (1)} px, ${r.states.length} states, reference arrows ${r.ref ? r.ref.n : 0}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n'); await b.close(); process.exit (1); }

  const S = r.states, T = S.filter (s => s.isTable);
  const bad1 = S.filter (s => s.overlaps.length || s.clear < 4);
  const worstClear = Math.min (...S.map (s => s.clear));
  gate (bad1.length === 0,
        '[1] NO OVERLAP, EVER — and ≥ 4 px of clear air between the two clusters, all 126 states',
        bad1.length ? `${bad1.length} bad: e.g. ${bad1[0].fam}/${bad1[0].sculpt}/${bad1[0].nk} clear ${bad1[0].clear} ${bad1[0].overlaps.slice (0, 2).join (' ')}`
                    : `0 overlaps; worst clearance ${worstClear.toFixed (2)} px (Table states: ${T.length})`);

  const longT = T.filter (s => s.nk !== 'short');
  const bad2 = longT.filter (s => ! s.nameEllipsis || ! s.chipIntact);
  gate (bad2.length === 0 && T.filter (s => s.nk === 'short').every (s => s.chipIntact),
        '[2] THE NAME GIVES, THE CHIPS DON\'T — long names end in …, Table and the sculpt word stay whole',
        bad2.length ? `${bad2.length} bad: e.g. ${bad2[0].sculpt}/${bad2[0].nk} ellipsis=${bad2[0].nameEllipsis} chips=${bad2[0].chipIntact}`
                    : `name slot ${Math.min (...T.map (s => s.nameBoxW)).toFixed (1)}–${Math.max (...T.map (s => s.nameBoxW)).toFixed (1)} px across sculpts; every long name ellipsised`);

  const cen = (a) => Math.abs (a.insetL - a.insetR) <= 1.0 && Math.abs (a.dy) <= 1.5;
  const same = (a, b) => Math.abs (a.insetL - b.insetL) <= 0.5 && Math.abs (a.insetR - b.insetR) <= 0.5 && Math.abs (a.dy - b.dy) <= 0.5;
  const bad3 = T.filter (s => ! (s.arrows.n === 2 && cen (s.arrows.L) && cen (s.arrows.R) && same (s.arrows.L, r.ref.L) && same (s.arrows.R, r.ref.R)));
  const a0 = T[0].arrows;
  gate (bad3.length === 0,
        '[3] THE ARROWS ARE CENTRED — glyph centred in its box and on the name\'s line, identical to the wavetable engine',
        bad3.length ? `${bad3.length} bad: e.g. ${bad3[0].sculpt}/${bad3[0].nk} ‹ inset ${bad3[0].arrows.L.insetL}/${bad3[0].arrows.L.insetR} dy ${bad3[0].arrows.L.dy} (ref ${r.ref.L.insetL}/${r.ref.L.insetR} dy ${r.ref.L.dy})`
                    : `Table ‹ inset ${a0.L.insetL}/${a0.L.insetR} dy ${a0.L.dy}  › ${a0.R.insetL}/${a0.R.insetR} dy ${a0.R.dy}  ·  wavetable ref ‹ ${r.ref.L.insetL}/${r.ref.L.insetR} dy ${r.ref.L.dy}  (display ${a0.L.disp})`);

  // The SLOT (the flex item) must be symmetric between the arrows. The label INSIDE it carries
  // `.preset-display { position:relative; left:1px }` — "Max: nudge the name text RIGHT 1px (between
  // the ‹ › selectors)" — a deliberate optical nudge shared with every engine, so the bar requires
  // Table's label offset to EQUAL the wavetable engine's, not to be zero.
  const bad4 = T.filter (s => Math.abs (s.arrows.gapLw - s.arrows.gapRw) > 0.5 || Math.abs (s.arrows.dispOff - r.ref.dispOff) > 0.25);
  gate (bad4.length === 0,
        '[4] THE ARROWS ARE SYMMETRIC — equal gaps either side of the name slot, and the name sits in it exactly as on the wavetable engine',
        bad4.length ? `${bad4.length} bad: e.g. ${bad4[0].sculpt}/${bad4[0].nk} slot gaps ${bad4[0].arrows.gapLw} vs ${bad4[0].arrows.gapRw}, label offset ${bad4[0].arrows.dispOff} (ref ${r.ref.dispOff})`
                    : `slot gaps ${a0.gapLw} / ${a0.gapRw} px on every Table state; label offset in slot ${a0.dispOff} px = wavetable's ${r.ref.dispOff} (Max's 1 px nudge, shared)`);

  // nothing moves: per sculpt, the fixed signature is identical across the three names
  const moved = [];
  for (let sc = 0; sc < 6; ++sc) { const g = T.filter (s => s.sc === sc);
    if (new Set (g.map (s => s.fixedSig)).size !== 1) moved.push (g[0].sculpt); }
  const lab = new Set (S.map (s => JSON.stringify (s.labelRect))), aSlot = new Set (S.map (s => JSON.stringify (s.aRect)));
  gate (moved.length === 0 && lab.size === 1 && aSlot.size === 1,
        '[5] NOTHING MOVES — same pixels for every element but the name\'s text, across all names; the label row and A never move',
        moved.length ? `moved on sculpt: ${moved.join (', ')}` : `fixed across 3 names on all 6 sculpts; label row ${lab.size} distinct rect(s), A slot ${aSlot.size}, over 126 states`);

  gate (r.refCornerMaxW === 'none' && r.refNameGrow === '0' && r.ref.L.disp === 'flex',
        '[6] OTHER ENGINES ARE UNTOUCHED — no clamp, no fill, canonical arrows on the wavetable engine',
        `wavetable corner max-width=${r.refCornerMaxW} · name slot flex-grow=${r.refNameGrow} (Table's is 1) · arrows display=${r.ref.L.disp} (blockified inline-flex)`);

  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail ? 1 : 0);
})();
