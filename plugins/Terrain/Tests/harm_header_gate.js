// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_header_gate.js — fb599: THE HARMONICS HEADER **IS** THE WAVETABLE HEADER.
//                        TABLES ONLY — NO FAMILY CHIP, NO SCULPT CHIP, NO CLAMP.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/harm_header_gate.js [page.html]
//
//  Max: "I only want to use tables for this. I don't want the families — Keel, the peel, whatever
//  the fuck those things are. I don't want those anymore. Only tables, so I need the header to be
//  cleared and only have tables on there. Make it look like wavetable if anything, the header."
//  (and, on the sculpt: "if we have to use one then we have to use keel" — Keel IS sculptMode 0.)
//
//  WHAT THIS GATE REPLACED. fb595 measured the same 126 header states but asserted the OPPOSITE
//  grammar: a FIXED-WIDTH corner cluster (`width: calc(100% - 124px)`) that never moved, because
//  the Table header carried THREE name-chips — Table + a sculpt word + the table name — where
//  every other engine carries one, and the clamp was the only thing keeping the cluster from
//  growing LEFT through "OSC · Harmonic". The cost was measured and stated at the time: the name
//  slot fell to 20.4 px on Terrace, so even "Sine" ellipsised. fb599 removes the CAUSE — both
//  chips come off — so the clamp is retired and the name is back on the shared 108 px cap. The
//  bars therefore flipped: bar 5 no longer says "nothing moves", it says "it moves EXACTLY like
//  the wavetable engine", measured against that engine in the same run.
//
//  ⚠️  THE PARAMS ARE NOT RETIRED. SYN_OSC_*_HARM_MODE still registers all seven choices
//      (Blade Neon Console Chant Bronze Hornet Table) and HARM_SCULPT all six (Keel … Clang) —
//      never renumber a choice. The UI stops OFFERING them; PluginProcessor.cpp's HARM gather
//      forces mainMode = 6 / sculptMode = 0. A preset saved on Blade loads with its index intact
//      and plays its table — which is exactly what bar 8 measures, at boot, before this harness
//      has touched a single <select>.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below may be asserted on a page that never rendered
//   1  NO OVERLAP, EVER — no two visible header words share pixels in ANY of the 126 states, and
//      the label row keeps ≥ 4 px of clear air to the corner cluster
//   2  THE CHIPS ARE GONE — neither the family chip nor the sculpt chip is visible in ANY state,
//      and the corner cluster's visible children are the wavetable engine's, in its order
//   3  THE ARROWS ARE CENTRED — each ‹ › glyph sits centred in its box and on the name's text
//      centreline, and measures IDENTICAL to the wavetable engine's
//   4  THE ARROWS ARE SYMMETRIC — equal box gaps either side of the name slot, every state
//   5  THE HEADER **IS** THE WAVETABLE ENGINE'S — for each name length every corner child sits at
//      the SAME pixels as the wavetable reference; the header is INVARIANT to the two retired
//      choices (all 7 × 6 combinations give one signature per name); label row and A never move
//   6  OTHER ENGINES ARE UNTOUCHED — the wavetable engine's cluster has no width clamp and its
//      arrows come from the canonical rule (the change is scoped by selector, and this proves it)
//   7  NO CLAMP, AND THE NAME HAS ITS 108 px BACK — HARM's corner computes to the SAME width as
//      the wavetable engine's for the same name, no corner child is flex:1, and a long name fills
//      the full 108 px cap (fb595 measured 20.4–39.9 px here)
//   8  A PRESET SAVED ON A RETIRED FAMILY STILL HAS A HEADER — measured at BOOT, before this
//      harness sets any select: on HARM the bare name wrap and both arrows are visible and no
//      chip is, whatever family index the stored preset carries
//
//  PROOF THE BARS CAN FAIL (source-level mutation — a mutation the runtime could undo is no proof):
//    HARM_HDR_MUTATE=1  the two chips come back (fb588's `display: inline-flex`)   → 1, 2, 5, 7, 8 red
//    HARM_HDR_MUTATE=2  the fb595 fixed-width clamp comes back                     → 5, 7 red
//    HARM_HDR_MUTATE=3  the name wrap is re-scoped to .harm-table and the class
//                       goes back to a toggle on i === 6 (a preset stored on any
//                       family but Table then has no name and no arrows)           → 2, 3, 4, 5, 7, 8 red
//  AND ON THE SHIPPED PAGE (a4887be, before fb599): 2, 3, 4, 5, 7, 8 red — 6 of the 9 bars.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs   = require ('fs');
const os   = require ('os');
const path = require ('path');
const puppeteer = require ('puppeteer-core');
const SRC = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');
const MUT = +(process.env.HARM_HDR_MUTATE || 0);

// ── the page, mutated at SOURCE ───────────────────────────────────────────────────────────────
const A_CHIPS = `#syn-panel .device.osc.engine-harm .device-corner-preset .hm-mode-wrap { display: none; }`;
const A_CLAMP = `#syn-panel .device.osc.engine-harm .device-corner-preset
  .preset-wrap:not(.hm-mode-wrap):not(.md-fam-wrap):not(.samp-name-wrap) { display: inline-block; }`;
const A_PIN   = `          try { var dv = sel.closest ('.device'); if (dv) dv.classList.add ('harm-table'); } catch (e2) {}`;
let PAGE = SRC;
if (MUT) {
  let h = fs.readFileSync (SRC, 'utf8');
  const swap = (a, b, tag) => { if (h.indexOf (a) < 0) throw new Error ('MUT ' + MUT + ': anchor not found — ' + tag); h = h.replace (a, b); };
  if (MUT === 1) swap (A_CHIPS, `#syn-panel .device.osc.engine-harm .device-corner-preset .hm-mode-wrap { display: inline-flex; }`, 'chips');
  if (MUT === 2) swap (A_CLAMP, A_CLAMP + `
#syn-panel .device.osc.engine-harm.harm-table .device-corner-preset { width: calc(100% - 124px); }
#syn-panel .device.osc.engine-harm.harm-table .device-corner-preset > * { flex: 0 0 auto; }
#syn-panel .device.osc.engine-harm.harm-table .device-corner-preset
  > .preset-wrap:not(.hm-mode-wrap):not(.md-fam-wrap):not(.samp-name-wrap) { flex: 1 1 auto; min-width: 0; }`, 'clamp');
  if (MUT === 3) { swap (A_CLAMP, A_CLAMP.replace ('.engine-harm ', '.engine-harm.harm-table '), 'rescope');
                   swap (A_PIN, `          try { var dv = sel.closest ('.device'); if (dv) dv.classList.toggle ('harm-table', i === 6); } catch (e2) {}`, 'pin'); }
  PAGE = path.join (process.env.HARM_HDR_TMP || os.tmpdir(), 'harm_header_mut' + MUT + '.html');
  fs.writeFileSync (PAGE, h);
}

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
    const out = { err: null, states: [], ref: {}, boot: null };
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
      const NK = Object.keys (NAMES);
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

      const label  = dev.querySelector ('.device-label-row');
      const corner = dev.querySelector ('.device-corner-preset');
      const nameDisp = document.getElementById ('osc-a-preset-display');
      const famDisp  = document.getElementById ('osc-a-harmmode-display');
      const scDisp   = document.getElementById ('osc-a-harmsculpt-display');
      const ENG = ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'];

      // the cluster signature: every visible corner child, its first class token and its rect
      const clusterSig = () => [...corner.children].filter (vis).map (ch => {
        const q = R (ch); return `${(ch.className||'').toString().split(/\s+/)[0]}@${q.x},${q.y},${q.w},${q.h}`; }).join (' ');
      const clusterCls = () => [...corner.children].filter (vis)
        .map (ch => (ch.className||'').toString().split(/\s+/)[0]).join (' ');

      // ── BAR 8's measurement, FIRST: the page as it boots, with only the engine class flipped.
      //    Nothing here sets a <select>: this is what a preset saved on a retired family shows. ──
      dev.classList.add ('engine-harm');
      out.boot = { fam: famDisp ? famDisp.textContent.trim() : '', harmTable: dev.classList.contains ('harm-table'),
                   nameVisible: vis (nameDisp.parentElement),
                   nameSlotW: vis (nameDisp.parentElement) ? R (nameDisp.parentElement).w : 0,
                   navs: [...dev.querySelectorAll ('.wt-nav')].filter (vis).length,
                   chips: [famDisp, scDisp].filter (e => e && vis (e)).length,
                   cls: clusterCls() };

      // ── the REFERENCE: the wavetable engine's own header, all three name lengths ─────────────
      const measureArrows = () => {
        const navs = [...dev.querySelectorAll ('.wt-nav')].filter (vis);
        if (navs.length !== 2) return { n: navs.length };
        const [L, Rr] = navs.map (n => ({ box: R (n), ink: ink (n), disp: getComputedStyle (n).display }));
        const nb = R (nameDisp), ni = ink (nameDisp), wb = R (nameDisp.parentElement);
        const insetL = (a) => +(a.ink.x - a.box.x).toFixed (2), insetR = (a) => +((a.box.x + a.box.w) - (a.ink.x + a.ink.w)).toFixed (2);
        const cy = (q) => q.y + q.h / 2;
        return { n: 2,
          L: { insetL: insetL (L), insetR: insetR (L), dy: +(cy (L.ink) - cy (ni)).toFixed (2), disp: L.disp },
          R: { insetL: insetL (Rr), insetR: insetR (Rr), dy: +(cy (Rr.ink) - cy (ni)).toFixed (2), disp: Rr.disp },
          gapLw: +(wb.x - (L.box.x + L.box.w)).toFixed (2), gapRw: +(Rr.box.x - (wb.x + wb.w)).toFixed (2),
          dispOff: +(nb.x - wb.x).toFixed (2) };   // .preset-display left:1px — Max's optical nudge, shared
      };
      ENG.forEach (c => dev.classList.remove (c)); dev.classList.remove ('harm-table');
      put ('osc-a-engine-display', 'Wavetable');
      for (const nk of NK) {
        put ('osc-a-preset-display', NAMES[nk]); put ('osc-a-preset-select', NAMES[nk]);
        out.ref[nk] = { sig: clusterSig(), cls: clusterCls(), cornerW: getComputedStyle (corner).width,
                        slotW: R (nameDisp.parentElement).w, arrows: measureArrows() };
      }
      out.refNameGrow  = getComputedStyle (nameDisp.parentElement).flexGrow;
      out.refCornerMaxW = getComputedStyle (corner).maxWidth;
      out.nameMaxW = getComputedStyle (nameDisp).maxWidth;

      // ── the 126 harmonics states: 7 stored families × 6 stored sculpts × 3 names ─────────────
      //    The two chips are gone, so the header MUST be blind to the first two dimensions.
      dev.classList.add ('engine-harm');
      put ('osc-a-engine-display', 'Harmonic');
      for (let f = 0; f < out.fams.length; ++f) {
        famSel.value = String (f); famSel.dispatchEvent (new Event ('change', { bubbles: true }));
        for (let sc = 0; sc < out.sculpts.length; ++sc) {
          scSel.value = String (sc); scSel.dispatchEvent (new Event ('change', { bubbles: true }));
          for (const nk of NK) {
            put ('osc-a-preset-display', NAMES[nk]); put ('osc-a-preset-select', NAMES[nk]);
            const st = { f, fam: out.fams[f], sc, sculpt: out.sculpts[sc], nk };
            const items = [];
            dev.querySelectorAll ('*').forEach (el => {
              if (! vis (el)) return; const q = R (el);
              if (q.y < -4 || q.y > 42) return;
              let t = ''; for (const n of el.childNodes) if (n.nodeType === 3) t += n.textContent;
              if (! t.trim() && el.tagName === 'SELECT') return;   // the transparent overlay twin — same slot by design
              t = t.replace (/\s+/g, ' ').trim(); if (! t) return;
              items.push ({ t, id: el.id || '', cls: (el.className || '').toString().split (/\s+/)[0], ...q });
            });
            st.overlaps = [];
            for (let i = 0; i < items.length; ++i) for (let j = i + 1; j < items.length; ++j) {
              const a = items[i], c = items[j];
              const ox = Math.min (a.x + a.w, c.x + c.w) - Math.max (a.x, c.x), oy = Math.min (a.y + a.h, c.y + c.h) - Math.max (a.y, c.y);
              if (ox > 0.5 && oy > 0.5) st.overlaps.push (`"${a.t}"×"${c.t}" ${ox.toFixed (1)}px`);
            }
            const lr = R (label), cr = R (corner);
            st.clear = +(cr.x - (lr.x + lr.w)).toFixed (2);
            st.labelRect = lr; st.aRect = R (dev.querySelector ('.osc-letter'));
            st.chipsVisible = [famDisp, scDisp].filter (e => e && vis (e))
                                .map (e => e.id + '="' + e.textContent.trim() + '"');
            st.sig = clusterSig(); st.cls = clusterCls();
            st.cornerW = getComputedStyle (corner).width;
            st.slotW = R (nameDisp.parentElement).w;
            st.slotGrow = getComputedStyle (nameDisp.parentElement).flexGrow;
            st.flexKids = [...corner.children].filter (vis)
              .map (ch => getComputedStyle (ch).flexGrow).filter (g => g !== '0').length;
            st.nameEllipsis = nameDisp.scrollWidth > nameDisp.clientWidth + 0.5;
            st.arrows = measureArrows();
            out.states.push (st);
          }
        }
      }
    } catch (e) { out.err = String (e && e.stack || e); }
    return out;
  });

  console.log (`\n══ harm_header_gate — fb599 ══  ${PAGE}${MUT ? '   (HARM_HDR_MUTATE=' + MUT + ')' : ''}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }

  const laidOut = r.devW > 100 && r.states.length === 7 * 6 * 3 && r.ref.long && r.ref.long.arrows.n === 2;
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — 126 header states + the wavetable reference at 3 name lengths',
        `osc width ${(r.devW||0).toFixed (1)} px, ${r.states.length} states, reference arrows ${r.ref.long ? r.ref.long.arrows.n : 0}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n'); await b.close(); process.exit (1); }

  const S = r.states;
  const bad1 = S.filter (s => s.overlaps.length || s.clear < 4);
  gate (bad1.length === 0,
        '[1] NO OVERLAP, EVER — and ≥ 4 px of clear air between the two clusters, all 126 states',
        bad1.length ? `${bad1.length} bad: e.g. ${bad1[0].fam}/${bad1[0].sculpt}/${bad1[0].nk} clear ${bad1[0].clear} ${bad1[0].overlaps.slice (0, 2).join (' ')}`
                    : `0 overlaps; worst clearance ${Math.min (...S.map (s => s.clear)).toFixed (2)} px`);

  const bad2 = S.filter (s => s.chipsVisible.length || s.cls !== r.ref[s.nk].cls);
  gate (bad2.length === 0,
        '[2] THE CHIPS ARE GONE — no family word, no sculpt word, and the cluster is the wavetable engine\'s',
        bad2.length ? `${bad2.length} bad: e.g. ${bad2[0].fam}/${bad2[0].sculpt}/${bad2[0].nk} chips ${JSON.stringify (bad2[0].chipsVisible)} cluster [${bad2[0].cls}] vs wavetable [${r.ref[bad2[0].nk].cls}]`
                    : `0 chips in 126 states; cluster children [${S[0].cls}] — identical to the wavetable engine's`);

  const cen  = (a) => Math.abs (a.insetL - a.insetR) <= 1.0 && Math.abs (a.dy) <= 1.5;
  const same = (a, b) => Math.abs (a.insetL - b.insetL) <= 0.5 && Math.abs (a.insetR - b.insetR) <= 0.5 && Math.abs (a.dy - b.dy) <= 0.5;
  const bad3 = S.filter (s => ! (s.arrows.n === 2 && cen (s.arrows.L) && cen (s.arrows.R)
                                 && same (s.arrows.L, r.ref[s.nk].arrows.L) && same (s.arrows.R, r.ref[s.nk].arrows.R)));
  const a0 = (S.find (s => s.arrows.n === 2) || S[0]).arrows;
  gate (bad3.length === 0,
        '[3] THE ARROWS ARE CENTRED — glyph centred in its box and on the name\'s line, identical to the wavetable engine',
        bad3.length ? `${bad3.length} bad: e.g. ${bad3[0].fam}/${bad3[0].sculpt}/${bad3[0].nk} → ` + (bad3[0].arrows.n !== 2
                        ? `${bad3[0].arrows.n} visible ‹ › (the header has no stepper at all)`
                        : `‹ inset ${bad3[0].arrows.L.insetL}/${bad3[0].arrows.L.insetR} dy ${bad3[0].arrows.L.dy} (ref ${r.ref[bad3[0].nk].arrows.L.insetL}/${r.ref[bad3[0].nk].arrows.L.insetR})`)
                    : `HARM ‹ inset ${a0.L.insetL}/${a0.L.insetR} dy ${a0.L.dy}  › ${a0.R.insetL}/${a0.R.insetR} dy ${a0.R.dy}  ·  wavetable ref ‹ ${r.ref.short.arrows.L.insetL}/${r.ref.short.arrows.L.insetR} (display ${a0.L.disp})`);

  const bad4 = S.filter (s => s.arrows.n !== 2 || Math.abs (s.arrows.gapLw - s.arrows.gapRw) > 0.5
                           || Math.abs (s.arrows.dispOff - r.ref[s.nk].arrows.dispOff) > 0.25);
  gate (bad4.length === 0,
        '[4] THE ARROWS ARE SYMMETRIC — equal gaps either side of the name slot, and the name sits in it exactly as on the wavetable engine',
        bad4.length ? `${bad4.length} bad: e.g. ${bad4[0].fam}/${bad4[0].sculpt}/${bad4[0].nk} → ` + (bad4[0].arrows.n !== 2
                        ? `${bad4[0].arrows.n} visible ‹ ›`
                        : `slot gaps ${bad4[0].arrows.gapLw} vs ${bad4[0].arrows.gapRw}, label offset ${bad4[0].arrows.dispOff} (ref ${r.ref[bad4[0].nk].arrows.dispOff})`)
                    : `slot gaps ${a0.gapLw} / ${a0.gapRw} px in every state; label offset in slot ${a0.dispOff} px = wavetable's ${r.ref.short.arrows.dispOff} (Max's 1 px nudge, shared)`);

  const bad5 = S.filter (s => s.sig !== r.ref[s.nk].sig);
  const perName = {}; for (const s of S) (perName[s.nk] = perName[s.nk] || new Set()).add (s.sig);
  const blind = Object.keys (perName).every (k => perName[k].size === 1);
  const lab = new Set (S.map (s => JSON.stringify (s.labelRect))), aSlot = new Set (S.map (s => JSON.stringify (s.aRect)));
  gate (bad5.length === 0 && blind && lab.size === 1 && aSlot.size === 1,
        '[5] THE HEADER **IS** THE WAVETABLE ENGINE\'S — same pixels per name, blind to family and sculpt, label row and A fixed',
        bad5.length ? `${bad5.length} bad: e.g. ${bad5[0].fam}/${bad5[0].sculpt}/${bad5[0].nk}\n          HARM ${bad5[0].sig}\n          WT   ${r.ref[bad5[0].nk].sig}`
                    : ! blind ? `header still varies with family/sculpt: ${Object.keys (perName).map (k => k + '=' + perName[k].size).join (' ')} distinct signatures`
                    : `every corner child on the wavetable engine's pixels, all 3 names; ${Object.keys (perName).length} signatures over 7×6 stored choices; label row ${lab.size} rect, A slot ${aSlot.size}`);

  gate (r.refCornerMaxW === 'none' && r.refNameGrow === '0' && r.ref.long.arrows.L.disp === 'flex',
        '[6] OTHER ENGINES ARE UNTOUCHED — no clamp, no fill, canonical arrows on the wavetable engine',
        `wavetable corner max-width=${r.refCornerMaxW} · name slot flex-grow=${r.refNameGrow} · arrows display=${r.ref.long.arrows.L.disp} (blockified inline-flex)`);

  const bad7 = S.filter (s => s.cornerW !== r.ref[s.nk].cornerW || s.flexKids !== 0 || s.slotGrow !== '0');
  const longSlot = Math.min (...S.filter (s => s.nk !== 'short').map (s => s.slotW));
  const cap = parseFloat (r.nameMaxW);
  gate (bad7.length === 0 && Math.abs (longSlot - cap) <= 0.5,
        '[7] NO CLAMP, AND THE NAME HAS ITS 108 px BACK — the corner sizes to its content exactly as the wavetable engine\'s',
        bad7.length ? `${bad7.length} bad: e.g. ${bad7[0].sculpt}/${bad7[0].nk} corner width ${bad7[0].cornerW} (wavetable ${r.ref[bad7[0].nk].cornerW}), ${bad7[0].flexKids} flexing child(ren), name slot flex-grow ${bad7[0].slotGrow}`
                    : `corner width ${S[0].cornerW}/${r.ref.short.cornerW} (short) · ${r.ref.long.cornerW} (long); long-name slot ${longSlot.toFixed (2)} px = the shared .preset-display cap ${r.nameMaxW}`);

  const bt = r.boot;
  gate (!!bt && bt.nameVisible && bt.navs === 2 && bt.chips === 0 && bt.cls === r.ref.short.cls,
        '[8] A PRESET SAVED ON A RETIRED FAMILY STILL HAS A HEADER — measured at boot, before this harness sets any select',
        bt ? `stored family "${bt.fam}" · .harm-table ${bt.harmTable} · name wrap visible ${bt.nameVisible} (slot ${bt.nameSlotW} px) · ‹ › ${bt.navs} · chips ${bt.chips} · cluster [${bt.cls}]`
           : 'no boot snapshot');

  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail ? 1 : 0);
})();
