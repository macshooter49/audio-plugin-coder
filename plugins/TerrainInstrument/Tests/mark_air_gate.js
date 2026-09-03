// ══════════════════════════════════════════════════════════════════════════════════════════════
//  mark_air_gate.js — fb571: THE MODULATION MARK KEEPS AIR UNDER ITS WORD, AND A MACRO'S MARK IS THE WORD.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/mark_air_gate.js [page.html]
//
//  Max (2026-09-02, with a screenshot): "the modulation on stuff like the mix and Baby looks way too
//  close and cluttered... put it down a little, one or two pixels, to where it's not so close to the
//  bottom of the word... still close enough to where it doesn't look out of place." And: "once I
//  rename something the letters get smaller — Baby is smaller than Macro 2."
//
//  MEASURED BEFORE (Chromium, the shipped 820 scale): the line sat 2.5 px under the baseline on every
//  surface — a pixel under the letters, touching a descender; the macro's mark measured the KNOB+LABEL
//  wrapper (the word selector did not know .vm-ml), so it was the knob's width, not the word's; the
//  macro labels were 7 px while every other synth knob word is 8 px.
//
//  THE BARS
//   1  LABEL SIZE — a macro's label is the synth page's one knob-word size: computed font of .vm-ml ==
//      .knob-label (8 px), renamed or not; 'Baby' and 'Macro 2' share one font and one box height.
//   2  THE MARK IS THE WORD — the macro mark's left/width equal the WORD's text rect, not the wrapper.
//   3  AIR — on a synth knob (Fold), a macro (Baby) and a rack dial (Mix) the line's top sits 4.5 px
//      under the word's ink (box +2.5 on the 8 px words, box +3.5 on the rack's 7 px word; was +0.5
//      everywhere); on the envelope shelf chip it is UNCHANGED at +0.5 (Max: "I think it's fine").
//   4  NOT CRAMMED — under every rack mark (both faces, all 16 kinds) the nearest painted INK below
//      the line (text, a border, a background, a canvas, the card's edge — not a dial's SVG box, whose
//      arc is inset and is measured by Tests/probe_modmarks.js: >= 15 px after the nudge) is
//      >= 4 px away; the marks still land (0 missing) and the fb453 audit's matcher finds them.
//   5  ONE RULE — window.__markAir answers 2 for .knob-label/.vm-ml/.fxr-lab/.lb and 0 for the .ec chip.
//
//  PROOF THE BARS CAN FAIL:  MARK_AIR_MUTATE=1 (markAir returns 0)  → bars 3, 5 red
//                            MARK_AIR_MUTATE=2 (.vm-ml back to 7 px)  → bar 1 red
//                            MARK_AIR_MUTATE=3 (.vm-ml out of the word selector) → bar 2 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const MUT = +(process.env.MARK_AIR_MUTATE || 0);
const KINDS = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'];
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('  (mutation ' + MUT + ' landed: 1 site)'); };
  if (MUT === 1) sub("  function markAir(lb){ try{ var c=lb&&lb.classList; if(!c) return 2;", "  function markAir(lb){ return 0; try{ var c=lb&&lb.classList; if(!c) return 2;");
  if (MUT === 2) sub("        #syn-panel .vm-macro .vm-ml{ font-size:8px;", "        #syn-panel .vm-macro .vm-ml{ font-size:7px;");
  if (MUT === 3) sub("el.querySelector('.knob-label,.fxr-lab,.lb,.vm-ml'))||el;", "el.querySelector('.knob-label,.fxr-lab,.lb'))||el;");
  const p = path.join(os.tmpdir(), 'mark_air_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}
const STUB = () => { const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0);}),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}}; };

(async () => {
  const P = mutatedPage();
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:760, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  console.log('\n══ fb571 — THE MARK KEEPS AIR UNDER ITS WORD · A MACRO\'S MARK IS THE WORD ══');
  console.log('   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  // the synth-page surfaces: the Macros view, a renamed macro, routes on Fold / Baby / Env 1 Dly, a racked Flanger's Mix
  const S = await pg.evaluate(async (KINDS) => {
    const mb = [...document.querySelectorAll('#syn-panel button, #syn-panel .pill, #syn-panel div')].find(e => e.textContent.trim() === 'Macros' && e.children.length === 0); if (mb) mb.click();
    await new Promise(r => setTimeout(r, 300)); window.__macroRename(1, 'Baby');
    try { window.__fxAdd('fla'); window.__fx4Tick(); } catch(e){}
    await new Promise(r => setTimeout(r, 300));
    window.__tiAddRoute(0, 1, 4); window.__tiAddRoute(0, 1, 1878); window.__tiAddRoute(0, 1, 481);
    const mixCell = [...document.querySelectorAll('#syn-panel .fxr-dev .fxr-knob[data-mod-dest]')].find(c => /^Mix$/.test((c.querySelector('.fxr-lab')||{}).textContent||''));
    if (mixCell) window.__tiAddRoute(0, 1, +mixCell.getAttribute('data-mod-dest'));
    for (let i = 0; i < 4; i++) { window.__ulTick(); await new Promise(r => requestAnimationFrame(r)); }
    const uls = [...document.querySelectorAll('.sm-ul')].filter(u => u.style.display !== 'none');
    const meas = (lab) => { const rg = document.createRange(); rg.selectNodeContents(lab); const tr = rg.getBoundingClientRect();
      const probe = document.createElement('span'); probe.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline;'; lab.appendChild(probe); const base = probe.getBoundingClientRect().bottom; probe.remove();
      const air = window.__markAir ? window.__markAir(lab) : -9;
      const u = uls.find(u => { const r = u.getBoundingClientRect(); return Math.abs(r.left - tr.left) < 0.75 && Math.abs(r.top - (tr.bottom - 1 + air)) < 0.75; });
      const ur = u ? u.getBoundingClientRect() : null;
      return { text: lab.textContent.trim(), font: getComputedStyle(lab).fontSize, boxH: +tr.height.toFixed(2), left: +tr.left.toFixed(2), width: +tr.width.toFixed(2), baseline: +base.toFixed(2), boxBottom: +tr.bottom.toFixed(2),
               air, found: !!u, ulLeft: ur ? +ur.left.toFixed(2) : null, ulWidth: ur ? +ur.width.toFixed(2) : null, lineTop: ur ? +(ur.top + 1.5).toFixed(2) : null }; };
    const mls = [...document.querySelectorAll('#syn-panel .vm-ml')];
    const fold = [...document.querySelectorAll('#syn-panel .knob-label')].find(l => l.textContent.trim() === 'Fold');
    const chip = document.querySelector('#syn-panel [data-mod-dest="481"]');
    const mix = mixCell ? mixCell.querySelector('.fxr-lab') : null;
    return { baby: meas(mls[0]), macro2: meas(mls[1]), fold: fold ? meas(fold) : null, chip: chip ? meas(chip) : null, mix: mix ? meas(mix) : null,
             wrapperW: +mls[0].parentElement.getBoundingClientRect().width.toFixed(2), knobLabelFont: fold ? getComputedStyle(fold).fontSize : '?',
             table: { knob: window.__markAir(fold), mac: window.__markAir(mls[0]), rack: mix ? window.__markAir(mix) : null, chip: chip ? window.__markAir(chip) : null,
                      lb: window.__markAir({ classList: { contains: (c) => c === 'lb' } }) } };
  }, KINDS);
  const B = S.baby, M2 = S.macro2;
  chk(B.font === S.knobLabelFont && M2.font === S.knobLabelFont && B.boxH === M2.boxH, '1  LABEL SIZE: "Baby" and "Macro 2" share the synth page\'s knob-word size (' + S.knobLabelFont + ') and one box height',
      'Baby ' + B.font + '/' + B.boxH + 'px · Macro 2 ' + M2.font + '/' + M2.boxH + 'px · .knob-label ' + S.knobLabelFont);
  chk(B.found && Math.abs(B.ulLeft - B.left) < 0.75 && Math.abs(B.ulWidth - B.width) < 0.75 && B.width < S.wrapperW - 4,
      '2  THE MARK IS THE WORD: the macro mark spans "Baby" (' + B.width + ' px), not its knob wrapper (' + S.wrapperW + ' px)', B.found ? 'mark left ' + B.ulLeft + ' width ' + B.ulWidth : 'NO MARK on the word');
  const airOK = (m, want) => m && m.found && Math.abs((m.lineTop - m.boxBottom) - (0.5 + want)) < 0.05 && (want ? (m.lineTop - m.baseline) >= 4 : true);
  const desc = (n, m) => n + (m ? ' ' + (m.found ? (m.lineTop - m.baseline).toFixed(1) + ' under baseline (box+' + (m.lineTop - m.boxBottom).toFixed(1) + ')' : 'NO MARK') : ' (absent)');
  chk(airOK(S.fold, 2) && airOK(S.baby, 2) && airOK(S.mix, 3) && airOK(S.chip, 0),
      '3  AIR: Fold / Baby / rack Mix all sit 4.5 px under their ink (+2 / +2 / +3 on the box); the envelope chip is unchanged',
      [desc('Fold', S.fold), desc('Baby', S.baby), desc('Mix', S.mix), desc('Dly chip', S.chip)].join(' · '));
  chk(S.table.knob === 2 && S.table.mac === 2 && S.table.rack === 3 && S.table.lb === 2 && S.table.chip === 0,
      '5  ONE RULE FOR THE EYE: __markAir → 2 on the 8 px words (knob, macro, flow cell), 3 on the rack\'s 7 px word, 0 on the envelope chip', JSON.stringify(S.table));

  // 4 · every rack mark, both faces: the room under the line
  const R = await pg.evaluate(async (KINDS) => {
    KINDS.forEach(k => { try { window.__fxAdd(k); } catch(e){} }); try { window.__fx4Tick(); } catch(e){}
    await new Promise(r => setTimeout(r, 400));
    const all = [];
    for (const open of [false, true]) {
      document.querySelectorAll('.fxr-dev').forEach(c => { const has = !!c.querySelector('.fxr-back'); c.classList.toggle('swapped', open && has); });
      await new Promise(r => setTimeout(r, 900));   // the face flip animates: the first card measured after the swap read 0.14 / 2.14 px once (its dials mid-flight)
      for (const kind of KINDS) {
        const D = window.__fxrDevs(); const i = D.findIndex(x => x.core === kind); if (i < 0) continue;
        const card = document.querySelectorAll('.fxr-dev')[i]; if (!card || (open && !card.querySelector('.fxr-back'))) continue;
        const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
        window.__tiPruneFxRoutes(0, 1e9);
        const cells = []; card.querySelectorAll('[data-mod-dest]').forEach(c => { if (c.classList.contains('fxr-knob') === !!open) return; const r = c.getBoundingClientRect(); if (!r.width || !r.height) return; window.__tiAddRoute(1, 0, +c.getAttribute('data-mod-dest')); cells.push(c); });
        window.__selMod = { env:1 }; window.__ulTick();
        await new Promise(r => requestAnimationFrame(r)); await new Promise(r => requestAnimationFrame(r)); window.__ulTick();   // let the face swap and the routes settle: a mark placed mid-reflow measured 0.14 px once
        const uls = [...document.querySelectorAll('.sm-ul')].filter(u => u.style.display !== 'none');
        for (const c of cells) { const lab = c.querySelector('.fxr-lab'); if (!lab) continue; const rg = document.createRange(); rg.selectNodeContents(lab); const LR = rg.getBoundingClientRect();
          const air = window.__markAir(lab);
          const u = uls.find(u => { const r = u.getBoundingClientRect(); return Math.abs(r.left - LR.left) < 0.75 && Math.abs(r.top - (LR.bottom - 1 + air)) < 0.75; });
          if (!u) { all.push({ kind, face: open ? 'back' : 'front', word: lab.textContent.trim(), missing: true }); continue; }
          const ur = u.getBoundingClientRect(); const lineBottom = ur.top + 3.0; let room = 1e9, who = '';
          const CR = card.getBoundingClientRect(); if (CR.bottom - 1 - lineBottom < room) { room = CR.bottom - 1 - lineBottom; who = 'card bottom'; }
          card.querySelectorAll('*').forEach(e => { if (e === u || u.contains(e) || lab.contains(e) || c.contains(e)) return; const r = e.getBoundingClientRect(); if (!r.width || !r.height) return;
            if (r.right <= ur.left || r.left >= ur.right || r.top < lineBottom - 0.01) return; const cs = getComputedStyle(e);
            if (/^(svg|SVG|path|circle|line|rect|g)$/.test(e.tagName) || e.closest('svg')) return;   // a dial's SVG box starts right under the line but its ARC is inset: the fb453 audit measures the arc itself (>= 15 px after the nudge)
            const paints = (e.textContent && e.textContent.trim() && e.children.length === 0) || cs.borderTopWidth !== '0px' || (cs.backgroundColor !== 'rgba(0, 0, 0, 0)' && cs.backgroundColor !== 'transparent') || e.tagName === 'CANVAS';
            if (!paints) return; const d = r.top - lineBottom; if (d < room) { room = d; who = e.tagName + '.' + (e.getAttribute('class') || '') + ' in .' + ((e.parentElement && e.parentElement.getAttribute('class')) || '') + ' @' + r.top.toFixed(1) + ',' + r.left.toFixed(1) + ' line@' + lineBottom.toFixed(1); } });
          all.push({ kind, face: open ? 'back' : 'front', word: lab.textContent.trim(), room: +room.toFixed(2), who }); }
      }
    }
    return all;
  }, KINDS);
  const missing = R.filter(a => a.missing), marks = R.filter(a => !a.missing).sort((a, b) => a.room - b.room);
  const tight = marks[0];
  chk(marks.length >= 150 && missing.length === 0 && tight && tight.room >= 4,
      '4  NOT CRAMMED: under every rack mark (both faces) the nearest painted ink below the line is >= 4 px away (a dial\'s arc is the audit\'s job); every routed dial carries a mark',
      marks.length + ' marks, ' + missing.length + ' missing; tightest: ' + marks.slice(0, 3).map(t => t.room + ' px ' + t.kind + '/' + t.word + ' (' + t.face + ', next: ' + t.who + ')').join(' · '));
  chk(errs.length === 0, '6  zero page errors', errs.length ? errs.join(' | ') : '');
  console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══\n');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
