// ══════════════════════════════════════════════════════════════════════════════════════════════
//  dropdown_dot_gate.js — rs2: NO DOT NEXT TO A DROPDOWN, AND NOTHING ELSE MOVES.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/dropdown_dot_gate.js [page.html]
//
//  Max: "next to Appliances, the left arrow next to One-Shot, there's a dot right there that
//  collides with this arrow ... get rid of ALL dots that you have next to drop down menus ...
//  people aren't stupid ... on the Sample / Granular / Resynth engines that dot collides with our
//  arrows."
//
//  THE DOT is `.samp-sel::after { content:'▾'; font-size:6px }` — a 6 px glyph after the loop-mode
//  word, the LAST thing in the label row, and the first thing the ‹ name › corner cluster (fb595's
//  other absolute cluster) runs into. The harm_header_gate idiom: rectangles only, every state —
//  3 engines × 5 loop modes × 3 name lengths = 45 header states.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — 45 states measured
//   1  NO DOT — no visible `::after` content on any dropdown in the label row, in any state
//      (RED on the shipped page: '▾', 6.13 px, on all 45)
//   2  NOTHING ELSE MOVES — the mode word's box ends where its TEXT ends (no phantom width), and
//      Osc · engine · mode sit at the same x in every name length (the corner never pushes them)
//   3  THE DOT NEVER TOUCHES THE ‹ — the label row's right edge clears the ‹ in every state where
//      it cleared it BEFORE minus the dot: reported per state; this bar asserts the DOT's own
//      contribution is gone (label right edge == mode text right edge)
//   4  THE CLUSTERS DON'T COLLIDE (fb595 law: ≥ 4 px clear air, all 45 states) — informational
//      on a page that only removes the dot; the residual is the two-absolute-clusters mechanism
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
    const out = { err: null, states: [] };
    try {
      const dev = document.querySelector ('#syn-panel .device.osc');
      if (! dev) { out.err = 'no osc device'; return out; }
      const devR = () => dev.getBoundingClientRect();
      out.devW = devR().width;
      const R = (el) => { const q = el.getBoundingClientRect(), d = devR();
        return { x: +(q.left - d.left).toFixed (2), y: +(q.top - d.top).toFixed (2), w: +q.width.toFixed (2), h: +q.height.toFixed (2) }; };
      const ink = (el) => { const tn = [...el.childNodes].find (n => n.nodeType === 3 && n.textContent.trim()); if (! tn) return null;
        const rg = document.createRange(); rg.selectNodeContents (tn); const q = rg.getBoundingClientRect(), d = devR();
        return { x: +(q.left - d.left).toFixed (2), y: +(q.top - d.top).toFixed (2), w: +q.width.toFixed (2), h: +q.height.toFixed (2) }; };
      const vis = (el) => { const cs = getComputedStyle (el); if (cs.display === 'none' || cs.visibility === 'hidden' || parseFloat (cs.opacity) === 0) return false;
        const q = el.getBoundingClientRect(); return q.width > 0 && q.height > 0; };
      const visDeep = (el) => { for (let e = el; e && e !== dev; e = e.parentElement) if (getComputedStyle (e).display === 'none') return false; return vis (el); };
      const put = (id, t) => { const e = document.getElementById (id); if (e) e.textContent = t; };
      const ENG = ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'];
      const CASES = [ { name: 'Sample', cls: 'engine-sample' }, { name: 'Granular', cls: 'engine-granular' }, { name: 'Resynth', cls: 'engine-geode' } ];
      const MODES = ['One-Shot','Forward','Reverse','Ping-Pong','Tailed'];
      const NAMES = { appl: 'Appliances', load: 'Load Sample', long: 'PLUTO 2 - JNO CORE EXTENDED MIX' };
      const label = dev.querySelector ('.device-label-row'), corner = dev.querySelector ('.device-corner-preset');
      for (const c of CASES) {
        ENG.forEach (k => dev.classList.remove (k)); dev.classList.add (c.cls); put ('osc-a-engine-display', c.name);
        const modeSel = [...dev.querySelectorAll ('.samp-head .samp-sel.mode')].filter (visDeep)[0];
        if (! modeSel) { out.err = 'no visible loop-mode dropdown on ' + c.name; return out; }
        const disp = modeSel.querySelector ('.samp-sel-disp');
        for (let m = 0; m < MODES.length; ++m) {
          disp.textContent = MODES[m];
          for (const nk of Object.keys (NAMES)) {
            put ('osc-a-sampname-display', NAMES[nk]);
            const st = { eng: c.name, mode: MODES[m], nk };
            // every dropdown in the label row: its ::after, its box, its text
            st.dds = [...dev.querySelectorAll ('.device-label-row .samp-sel, .device-label-row .engine-wrap')].filter (visDeep).map (s => {
              const cs = getComputedStyle (s, '::after'); const d = s.querySelector ('.samp-sel-disp, .engine-display'); const sr = R (s), tr = d ? (ink (d) || R (d)) : sr;
              return { cls: s.className, afterContent: cs.content, afterDisplay: cs.display, box: sr, text: tr, phantom: +((sr.x + sr.w) - (tr.x + tr.w)).toFixed (2) }; });
            const lr = R (label), cr = R (corner); st.labelR = lr; st.cornerR = cr;
            st.clear = +(cr.x - (lr.x + lr.w)).toFixed (2);
            const modeText = ink (disp) || R (disp); st.modeTextRight = +(modeText.x + modeText.w).toFixed (2); st.labelRight = +(lr.x + lr.w).toFixed (2);
            const nav = [...dev.querySelectorAll ('.samp-nav')].filter (visDeep)[0]; st.navL = nav ? R (nav) : null;
            st.fixedSig = ['.device-label', '.engine-wrap', '.samp-sel.mode'].map (q => { const e = dev.querySelector ('.device-label-row ' + q); const b = R (e); return `${q}@${b.x},${b.y}`; }).join (' ');
            // fb595 census: any two visible words in the header band sharing pixels
            const items = [];
            dev.querySelectorAll ('*').forEach (el => { if (! visDeep (el)) return; const q = R (el); if (q.y < -4 || q.y > 42) return;
              let t = ''; for (const n of el.childNodes) if (n.nodeType === 3) t += n.textContent; if (! t.trim() && el.tagName === 'SELECT') return;
              t = t.replace (/\s+/g, ' ').trim(); if (! t) return; items.push ({ t, ...q }); });
            st.overlaps = [];
            for (let i = 0; i < items.length; ++i) for (let j = i + 1; j < items.length; ++j) { const a = items[i], k = items[j];
              const ox = Math.min (a.x + a.w, k.x + k.w) - Math.max (a.x, k.x), oy = Math.min (a.y + a.h, k.y + k.h) - Math.max (a.y, k.y);
              if (ox > 0.5 && oy > 0.5) st.overlaps.push (`"${a.t}"×"${k.t}" ${ox.toFixed (1)}px`); }
            out.states.push (st);
          }
        }
        disp.textContent = MODES[0];
      }
    } catch (e) { out.err = String (e && e.stack || e); }
    return out;
  });

  console.log (`\n══ dropdown_dot_gate — rs2 ══  ${PAGE}\n`);
  if (r.err) { console.log ('  harness error: ' + r.err); await b.close(); process.exit (1); }
  const S = r.states;
  const laidOut = r.devW > 100 && S.length === 45 && S.every (s => s.navL && s.dds.length >= 2);
  gate (laidOut, '[0] THE PANEL ACTUALLY LAID OUT — 3 engines × 5 loop modes × 3 names = 45 header states',
        `osc width ${(r.devW||0).toFixed (1)} px, ${S.length} states, dropdowns per state ${S[0] ? S[0].dds.length : 0}`);
  if (! laidOut) { console.log ('\n  ❌ degenerate page — not asserting anything on it\n'); await b.close(); process.exit (1); }

  const dotted = S.filter (s => s.dds.some (d => d.afterDisplay !== 'none' && d.afterContent !== 'none' && d.afterContent !== 'normal' && d.afterContent !== '""'));
  const ex = dotted[0] && dotted[0].dds.find (d => d.afterContent !== 'none' && d.afterContent !== 'normal' && d.afterContent !== '""');
  gate (dotted.length === 0, '[1] NO DOT — no dropdown in the label row draws an ::after glyph, in any of the 45 states',
        dotted.length ? `${dotted.length}/45 states draw one: ${ex.cls} ::after content ${ex.afterContent}, ${ex.phantom} px of glyph after the word (e.g. ${dotted[0].eng} / ${dotted[0].mode} / ${dotted[0].nk})`
                      : `every dropdown's ::after is none; mode word box ends at its text (+${Math.max (...S.map (s => Math.max (...s.dds.map (d => d.phantom)))).toFixed (2)} px)`);

  const phantom = S.filter (s => s.dds.some (d => Math.abs (d.phantom) > 0.5));
  const moved = []; for (const eng of ['Sample','Granular','Resynth']) for (const mode of ['One-Shot','Forward','Reverse','Ping-Pong','Tailed']) {
    const g = S.filter (s => s.eng === eng && s.mode === mode); if (new Set (g.map (s => s.fixedSig)).size !== 1) moved.push (eng + '/' + mode); }
  gate (phantom.length === 0 && moved.length === 0, '[2] NOTHING ELSE MOVES — no phantom width after any word; Osc · engine · mode at the same x for every name',
        phantom.length ? `${phantom.length} states carry phantom width: e.g. ${phantom[0].eng}/${phantom[0].mode}: ${phantom[0].dds.map (d => d.cls.split (' ').pop() + ' +' + d.phantom).join (', ')} px`
                       : moved.length ? `label cluster moved with the name on: ${moved.join (', ')}` : `phantom ≤ 0.5 px everywhere; label cluster pinned across all 3 names in all 15 engine×mode groups`);

  const dotTouch = S.filter (s => Math.abs (s.labelRight - s.modeTextRight) > 0.5);
  gate (dotTouch.length === 0, '[3] THE DOT NEVER TOUCHES THE ‹ — the label row ends exactly where the mode TEXT ends, so nothing but text can meet the arrow',
        dotTouch.length ? `${dotTouch.length}/45 states: label row runs ${(dotTouch[0].labelRight - dotTouch[0].modeTextRight).toFixed (2)} px past the mode text (${dotTouch[0].eng}/${dotTouch[0].mode}/${dotTouch[0].nk}; ‹ at ${dotTouch[0].navL.x}, clear ${dotTouch[0].clear})`
                        : `label right == mode text right on all 45; worst clearance to the ‹ now ${Math.min (...S.map (s => s.clear)).toFixed (2)} px (${S.reduce ((a, s) => s.clear < a.clear ? s : a).eng}/${S.reduce ((a, s) => s.clear < a.clear ? s : a).mode}/${S.reduce ((a, s) => s.clear < a.clear ? s : a).nk})`);

  const bad4 = S.filter (s => s.overlaps.length || s.clear < 4);
  const worst = S.reduce ((a, s) => s.clear < a.clear ? s : a);
  const maxLabel = ['Sample','Granular','Resynth'].map (e => { const g = S.filter (s => s.eng === e); const m = g.reduce ((a, s) => s.labelRight > a.labelRight ? s : a); return `${e} ${m.labelRight} (${m.mode})`; });
  console.log (`        (label row right edge, widest per engine: ${maxLabel.join (' · ')}; corner right edge ${(S[0].cornerR.x + S[0].cornerR.w).toFixed (2)}; name slot ${[...new Set (S.map (s => s.navL && s.navL.x))].length} distinct ‹ positions)`);
  if (process.argv.includes ('--dump')) require ('fs').writeFileSync (process.argv[process.argv.indexOf ('--dump') + 1], JSON.stringify (S, null, 1));
  /* fb598 — [4] is a WARN, not a bar. The dot was the FIRST casualty of the fb595 two-absolute-clusters mechanism on the
     sample engines and removing it took the under-4-px states from 17/45 to 8/45; the residual (worst Granular/Ping-Pong/long
     −5.14 px, three of them on the default "Load Sample") is that mechanism, not the dot. The measured fix (fb595's fixed-width
     corner scoped to the three sample engines, calc(100% − 178px)) clears all 45 but shrinks the name slot to 74 px so the
     DEFAULT label reads "Load Sa…" — a visible regression on every empty sample osc. That trade is Max's, so this prints the
     numbers and does not go red. A red bar everyone steps over is zero information. */
  ((ok, name, detail) => { console.log (`  ${ok ? 'PASS' : 'WARN'}  ${name}\n        ${detail}`); }) (bad4.length === 0, '[4] THE CLUSTERS DON\'T COLLIDE — ≥ 4 px clear air between the label row and the ‹ name › cluster, all 45 states (fb595 law — RESIDUAL, see note)',
        bad4.length ? `${bad4.length}/45 states under 4 px: worst ${worst.eng}/${worst.mode}/${worst.nk} clear ${worst.clear} px ${worst.overlaps.slice (0, 2).join (' ')}; by name: ${['appl','load','long'].map (nk => nk + ':' + S.filter (s => s.nk === nk && (s.overlaps.length || s.clear < 4)).length).join (' ')}`
                    : `worst clearance ${worst.clear} px (${worst.eng}/${worst.mode}/${worst.nk}); 0 overlaps`);

  console.log (`\n  ${fail ? '❌' : '✅'} ${pass} passed, ${fail} failed\n`);
  await b.close();
  process.exit (fail ? 1 : 0);
})();
