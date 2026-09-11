// fb635_restale_gate.js — PRESET A→B SHOWS B (the "everything else" surfaces). Runs the relay-faithful harness (harness.js)
// against a page, loads real presets A→B the way loadPatchFromFile + afterPatchLoad do, and asks the page what it SHOWS.
//   node Tests/fb635_restale_gate.js [page.html]     (default: Source/ui/public/index.html; ~2.5 min)
//   RG_MUT=blend   the blend pills' repull re-read removed                 → [0] RED
//   RG_MUT=mirror  onOscSampleCleared calls clearLoaded again (it WRITES)   → [3] [3h] RED
// [3] and [3h] are the NO-MIRROR-WRITE law (the sweep skeptic's widening): after a load settles, no parameter may differ
// from what B stored beyond what a fresh boot of B writes — once for a browser load, once for a HOST restore (DAW undo,
// A/B, host preset recall: fb635's editor timer announces it, so a writing mirror would now fire on every one of them).
// [5] retired: the osc A WT selector is a transparent overlay (0/1224 pixels change with its opacity).
// Fixtures: Tests/fixtures/fb635_restale_presets.json (six of Max's presets, audio stripped) + fb635_relays.json.
// Every bar compares the LOADED page with a FRESH boot of B (the truth the page must reach), except [3]/[3h] (what the
// page WROTE, vs what a fresh boot writes) and [6] (the card-state log of one load).
const { execFileSync } = require('child_process'); const fs = require('fs'); const path = require('path'); const os = require('os');
const PAGE0 = path.resolve(process.argv[2] || process.env.PAGE || path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html'));
const MUT = process.env.RG_MUT || '';
process.on('uncaughtException', e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });   /* a crash is a broken run, never a red control */
(function () { let s = fs.readFileSync(PAGE0, 'utf8'); const sub = (a, b) => { if (s.split(a).length !== 2) { console.log('  MUTATION anchor not unique: ' + a.slice(0, 70)); process.exit(2); } s = s.replace(a, b); };
  if (MUT === 'blend')  sub("    T(() => window.__tiBlendRefresh && window.__tiBlendRefresh());\n", '');
  if (MUT === 'mirror') sub('if (S && S.clearVisual) S.clearVisual (); else if (S && S.clearLoaded) S.clearLoaded ();', 'if (S && S.clearLoaded) S.clearLoaded ();');
  const f = MUT ? path.join(os.tmpdir(), 'fb635_restale_' + MUT + '.html') : PAGE0; if (MUT) fs.writeFileSync(f, s); process.env.PAGE = f; })();
const H = path.join(__dirname, 'fb635_restale_harness.js'), OUT = fs.mkdtempSync(path.join(os.tmpdir(), 'fb635_restale_'));
const OPEN = 'try{["arp","glitch","drift","chop"].forEach(function(m){try{window.__openFlowCard&&window.__openFlowCard(m);}catch(e){}});}catch(e){} try{window.__openLfoCard&&window.__openLfoCard();}catch(e){} try{window.__openCrvCard&&window.__openCrvCard();}catch(e){} try{var l=document.querySelector(".lfo-ext .lsel .lane[data-n=\\"2\\"]"); l&&l.click();}catch(e){}';
const LOGW = 'window.__log=[]; var H=window.__H; ["setCardState","setSynParam"].forEach(function(n){ var f=H.NAT[n]; H.NAT[n]=function(){ if(window.__armed) window.__log.push(n+":"+String(arguments[0])+":"+String(arguments[1]).slice(0,60)); return f.apply(this,arguments); }; }); var sw=H.switchTo; H.switchTo=function(B){ window.__armed=1; return sw.call(H,B); };';
const PROBE = `{
  blend: ['a','b','c','d'].map(function(o){ var p=document.querySelector('#osc-'+o+'-device .back-only .blend-pills .blend-pill'); return p?(p.textContent+(p.classList.contains('act')?'*':'')):null; }).join(' '),
  phase: ['a','b','c','d'].map(function(o){ var b=document.querySelector('#osc-'+o+'-device .ph-val[data-ph="SYN_OSC_'+o.toUpperCase()+'_PHASE"] b'); return b?b.textContent:null; }).join(' '),
  crv: JSON.stringify(window.__crvPts?window.__crvPts():null).slice(0,80),
  lfoDot: (function(){ var b=[].slice.call(document.querySelectorAll('.lfo-ext .fxb')).filter(function(x){return x.textContent==='Dot'})[0]; return b?b.classList.contains('on'):null; })(),
  loopMode: ['A','B','C','D'].map(function(L){ return window.__H.cur.params['SYN_OSC_'+L+'_SAMPLE_LOOP_MODE']; }).join(','),
  writes: (function(){ var H=window.__H, o=[]; for (var k in H.B0) { var a=+H.cur.params[k], b=+H.B0[k]; if (!(Math.abs(a-b) <= 1e-6*Math.max(1,Math.abs(b)))) o.push(k); } return o; })(),
  log: (window.__log||[]).filter(function(e){ return /^setCardState/.test(e); })
}`;
function run(tag, args, env) {
  const f = path.join(OUT, tag + '.json');
  execFileSync('node', [H, ...args, f], { env: Object.assign({}, process.env, { BOOTW: '5000', LOADW: '6000', EXTRA: PROBE }, env), stdio: 'pipe', timeout: 180000 });
  return JSON.parse(fs.readFileSync(f)).extra;
}
function pair(tag, A, B, env) {   // fresh B and A→B, concurrently would be faster; kept serial for determinism
  return { fresh: run(tag + '_fresh', ['fresh', B], env), loaded: run(tag + '_load', ['load', A, B], env) };
}
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };
console.log('══ fb635 RESTALE GATE — PRESET A→B SHOWS B ══   page: ' + process.env.PAGE + (MUT ? '   mutation: ' + MUT : ''));

// [0] blend pills — Corinthians (no blends) → Code Veronica V2 (osc A: FM from S) and back
const p0 = pair('blend', 'Corinthians', 'Code Veronica V2', { PRE: OPEN });
const p0b = pair('blendback', 'Code Veronica V2', 'Corinthians', { PRE: OPEN });
gate(p0.loaded.blend === p0.fresh.blend && p0b.loaded.blend === p0b.fresh.blend, '[0] BLEND PILLS (B1-B4, every osc back) show the LOADED preset',
  `C→V2 fresh "${p0.fresh.blend}" loaded "${p0.loaded.blend}" · V2→C fresh "${p0b.fresh.blend}" loaded "${p0b.loaded.blend}"`);
// [1] phase pills — Clic Up (180°) → Location (0°)
const p1 = pair('phase', 'Clic Up', 'Location', { PRE: OPEN });
gate(p1.loaded.phase === p1.fresh.phase, '[1] PHASE PILLS (osc back) show the LOADED preset', `fresh "${p1.fresh.phase}" loaded "${p1.loaded.phase}"`);
// [2] distortion curve — Don_t Go (drawn curve) → Corinthians (none: the processor cleared it)
const p2 = pair('crv', 'Don_t Go', 'Corinthians', { PRE: OPEN });
gate(p2.loaded.crv === p2.fresh.crv, '[2] THE CURVE CARD drops a drawn curve the loaded preset does not carry', `fresh ${p2.fresh.crv} · loaded ${p2.loaded.crv}`);
// [3] the load writes NOTHING back: an empty osc keeps the loop mode the preset saved (Location stores One-Shot = 0)
const w3 = p1.loaded.writes.filter(k => p1.fresh.writes.indexOf(k) < 0);
gate(w3.length === 0 && p1.loaded.loopMode === p1.fresh.loopMode, '[3] A LOAD WRITES NOTHING OVER THE PRESET — no parameter differs from what Location stored beyond a fresh boot (onOscSampleCleared is a mirror)',
  `written by the load: ${w3.slice(0, 8).join(', ') || 'none'}${w3.length > 8 ? ' …(' + w3.length + ')' : ''} · LOOP_MODE A-D fresh ${p1.fresh.loopMode} · load ${p1.loaded.loopMode}`);
const p3h = pair('host', 'Clic Up', 'Location', { PRE: OPEN, HOST: '1' });
const w3h = p3h.loaded.writes.filter(k => p3h.fresh.writes.indexOf(k) < 0);
gate(w3h.length === 0, '[3h] A HOST RESTORE WRITES NOTHING EITHER — the same load announced as a host restore (DAW undo / A-B / preset recall)',
  `written by the host restore: ${w3h.slice(0, 8).join(', ') || 'none'}${w3h.length > 8 ? ' …(' + w3h.length + ')' : ''}`);
// [4] LFO motion on a NON-custom shape — Location with LFO2 as a Sine (its stored motion keeps Dot)
const p4 = pair('lfo', 'Clic Up', 'Location', { PRE: OPEN, MODS: JSON.stringify({ Location: { LFO2_SHAPE: 0 } }) });
gate(p4.loaded.lfoDot === true && p4.fresh.lfoDot === true, '[4] LFO CARD MOTION FLAGS show the loaded LFO (Dot on LFO 2, shape Sine)', `fresh Dot ${p4.fresh.lfoDot} · loaded Dot ${p4.loaded.lfoDot}`);
// [5] retired (fb635): the WT selector is a transparent overlay; its opacity is invisible.
// [6] no phantom card states: Bloodlust carries chop+gli only; after V2→Bloodlust no arp/rbn state may be written
const p6 = run('card_load', ['load', 'Code Veronica V2', 'Bloodlust'], { PRE: OPEN + LOGW });
const phantom = (p6.log || []).filter(e => /^setCardState:(arp|rbn):/.test(e));
gate(phantom.length === 0, '[6] A LOAD CREATES NO CARD STATE the preset does not carry (arp/rbn absent in Bloodlust)', `setCardState after load: ${(p6.log || []).map(e => e.split(':')[1]).join(',') || 'none'}`);
console.log(`\n  ${pass}/${pass + fail} PASS`);
process.exit(fail ? 1 : 0);
