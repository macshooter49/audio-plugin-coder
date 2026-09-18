// ══════════════════════════════════════════════════════════════════════════════════════════════
//  dice_rules_gate.js — tp23 · THE DICE'S RULES, EXERCISED, NOT GREPPED.
//
//      node Tests/dice_rules_gate.js Source/ui/public/index.html
//
//  WHY THIS FILE EXISTS.  Max's rules for the dice are statistical ("only on crazy", "cap at 6",
//  "never past 20"), and a regex cannot tell you whether a branch is REACHABLE — only whether a
//  string is present. So this gate lifts the actual decision code OUT of the shipping index.html
//  and RUNS it, tens of thousands of times, across every aim and every reach. It is the shipped
//  text that executes here: if someone edits the engine choice, this runs the edit.
//
//  What it holds the dice to (Max, tp23):
//    · "take away the granular and the sample engine from even being thought of in the dice" —
//      Sample / Granular / Resynth appear at NO reach but CRAZY, and DO appear on crazy.
//    · "the randomization cap should be at 6 engines. Not the whole 8" — never more than 6, and
//      the top of the reach really can get there (a cap nothing approaches is a dead rule).
//    · "the delay feedback should always be under 20" — knobTop pins delay knob 1, at every reach.
//    · percussion is gone: not an aim, not an ENV row, not a sample category.
//    · FX and Modulation are AIMS in the one list, not actions at the bottom of the menu.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs = require('fs');
const SRC = fs.readFileSync(process.argv[2] || 'Source/ui/public/index.html', 'utf8');

let pass = 0, fail = 0;
const chk = (ok, what, detail) => { ok ? pass++ : fail++; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${what}`); if (!ok && detail) console.log(`        ${detail}`); };

/** Lift a balanced `var NAME={...};` (or a function body) out of the source, verbatim. */
function lift(startPat, opener = '{', closer = '}') {
  const i = SRC.search(startPat);
  if (i < 0) throw new Error('not found: ' + startPat);
  const j = SRC.indexOf(opener, i);
  let d = 0, k = j;
  for (; k < SRC.length; k++) {
    if (SRC[k] === opener) d++;
    else if (SRC[k] === closer && --d === 0) break;
  }
  return SRC.slice(i, k + 1);
}
/** Lift a run of source between two anchors (inclusive of the end line). */
function span(a, b) {
  const i = SRC.indexOf(a); if (i < 0) throw new Error('no anchor: ' + a);
  const j = SRC.indexOf(b, i); if (j < 0) throw new Error('no end: ' + b);
  return SRC.slice(i, j + b.length);
}

// ── the real tables and helpers, straight out of the page ────────────────────────────────────
const REACH  = eval('(' + lift(/var REACH=\{/).replace(/^var REACH=/, '') + ')');
const SAMPR  = eval('(' + lift(/var SAMPR=\{/).replace(/^var SAMPR=/, '') + ')');
const AIMS   = eval(span("var AIMS=[", "];").replace(/^var AIMS=/, '').replace(/,\s*LEVELS=.*$/s, '').replace(/;$/, ''));
const DLYMAX = eval(span("var DLY_FB_MAX=", ";").replace(/^var DLY_FB_MAX=/, '').replace(/;$/, ''));
// knobTop closes over DLY_FB_MAX in the page; hand it the real one rather than inlining a copy
const knobTop = new Function('DLY_FB_MAX', 'return (' +
  span("function knobTop(core,k,top)", "}").replace(/^function knobTop/, 'function') + ')')(DLYMAX);

const LEVELS = ['light', 'medium', 'heavy', 'wild', 'crazy'];
const SOUND_AIMS = AIMS.filter(a => a !== 'anything');

// deterministic-ish RNG so a failure is reproducible
let seed = 12345;
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const pick = a => a[Math.floor(rnd() * a.length)];

// ── 1 · THE ENGINE CHOICE — the shipped expression, run ──────────────────────────────────────
// read whatever `seng` is actually assigned — anchoring on the VALUE would make a changed value
// invisible to this gate (it did, once).
const engineSrc = (() => {
  const i = SRC.indexOf("var wildUp=(lvl==='wild'");   // the block starts at the reach test, not at `modal`
  const j = SRC.indexOf('var seng=', i);
  const k = SRC.indexOf(';', j);
  if (i < 0 || j < 0 || k < 0) throw new Error('engine block anchors not found');
  return SRC.slice(i, k + 1);
})();
const chooseEngine = new Function('aim', 'lvl', 'R', 'rnd', 'pick', 'SAMPR',
  engineSrc.replace(/\/\*[\s\S]*?\*\//g, '') + '\n return { modal:modal, samp:samp, fm:fm, harm:harm, seng:seng, engine: modal?6:samp?seng:harm?5:fm?4:0 };');

const seen = {}; LEVELS.forEach(l => seen[l] = new Set());
for (const lvl of LEVELS)
  for (const aim of SOUND_AIMS)
    for (let n = 0; n < 4000; n++)
      seen[lvl].add(chooseEngine(aim, lvl, REACH[lvl], rnd, pick, SAMPR).engine);

// tp29 — Max: "Light, medium, and heavy can literally just be oscillators, effects, all that, but no
// sort of like sampling engines ... all the sample engines can be crazy or wild, all of it."
const SAMPLE_FAMILY = new Set([1, 2, 3]);          // Sample · Granular · Resynth
for (const lvl of ['light', 'medium', 'heavy']) {
  const leaked = [...seen[lvl]].filter(e => SAMPLE_FAMILY.has(e));
  chk(leaked.length === 0, `[1] ${lvl}: no sample-based engine at all`,
      leaked.length ? `leaked ${leaked.join(', ')} (1=Sample 2=Granular 3=Resynth)` : '');
}
for (const lvl of ['wild', 'crazy']) {
  const got = [...seen[lvl]].filter(e => SAMPLE_FAMILY.has(e)).sort();
  chk(got.length === 3, `[1] ${lvl}: the WHOLE sample family is on the table (Sample, Granular, Resynth)`,
      `saw ${got.join('/') || 'none'}`);
}
chk(['light', 'medium', 'heavy', 'wild', 'crazy'].every(l => seen[l].has(6)),
    '[1] Modal stays at every reach (Max keeps it by name)');
for (const e of [0, 4, 5, 6]) {
  const name = { 0: 'Wavetable', 4: 'FM', 5: 'Additive', 6: 'Modal' }[e];
  chk([...seen.medium].includes(e), `[1] the synthesis pool still reaches ${name} at medium`);
}

// ── 2 · THE ENGINE CAP ───────────────────────────────────────────────────────────────────────
const capConst = Number(/var DICE_MAX_ENGINES=(\d+);/.exec(SRC)[1]);
chk(capConst === 6, `[2] the declared cap is 6 (found ${capConst})`);
const capSrc = span("var DICE_MAX_ENGINES=", "nOn=Math.min(Math.min(DICE_MAX_ENGINES,pool.length),nOn);");
const countOn = new Function('aim', 'lvl', 'big', 'rnd', 'oscList',
  capSrc.replace(/\/\*[\s\S]*?\*\//g, '') + '\n return nOn;');
const pool8 = () => ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];
let worst = 0, bestCrazy = 0;
for (const lvl of LEVELS) {
  const big = (lvl === 'heavy' || lvl === 'wild' || lvl === 'crazy');
  for (const aim of SOUND_AIMS)
    for (let n = 0; n < 4000; n++) {
      const v = countOn(aim, lvl, big, rnd, pool8);
      worst = Math.max(worst, v);
      if (lvl === 'crazy') bestCrazy = Math.max(bestCrazy, v);
    }
}
chk(worst <= 6, '[2] the dice never turns on more than 6 oscillators', `worst seen: ${worst}`);
chk(bestCrazy === 6, '[2] and crazy really does reach 6 (the cap is not decorative)', `crazy best: ${bestCrazy}`);

// ── 3 · DELAY FEEDBACK ───────────────────────────────────────────────────────────────────────
chk(DLYMAX <= 0.20, `[3] DLY_FB_MAX is ${DLYMAX} (<= 0.20 = 20 %)`);
let fbWorst = 0, otherMoved = false;
for (const top of [0.55, 0.7, 0.85, 0.95, 1]) {
  fbWorst = Math.max(fbWorst, knobTop('delay', 1, top));
  if (knobTop('delay', 0, top) === top && knobTop('reverb', 1, top) === top) otherMoved = true;
}
chk(fbWorst <= 0.20, '[3] delay knob 1 (Fdbk) is capped at every reach', `worst top: ${fbWorst}`);
chk(otherMoved, '[3] and the cap touches nothing else (delay Time, other cards, untouched)');
chk(/if\(f\.core==='delay'&&kk===1\) continue;/.test(SRC),
    '[3] delay feedback is struck off the modulation target pool too');
chk(/\(core==='delay'&&k===1\)\?Math\.min\(hi,\.20\)/.test(SRC),
    "[3] the rack's own dice carries the same cap (its own closure)");

// ── 4 · PERCUSSION IS GONE ───────────────────────────────────────────────────────────────────
chk(!AIMS.includes('percussion'), '[4] percussion is not an aim any more');
// Count mentions in CODE, not in prose: strip the comments first. Counting "expected" comment
// phrases instead made the test fail the moment a new note mentioned the word.
const diceRegion = SRC.slice(SRC.indexOf('var AIMS=['), SRC.indexOf('function openAimMenu'));
const codeOnly = diceRegion.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/(^|[^:])\/\/[^\n]*/g, '$1');
const leftovers = (codeOnly.match(/percussion/gi) || []).length;
chk(leftovers === 0, '[4] no live percussion branch survives in the dice',
    leftovers ? `${leftovers} mention(s) in executable code` : '');

// ── 5 · THE MENU ─────────────────────────────────────────────────────────────────────────────
chk(/var DTARGETS=\['fx','modulation'\]/.test(SRC), '[5] FX and Modulation are dice TARGETS');
chk(/AIMS\.concat\(DTARGETS\)\.map/.test(SRC), '[5] and they render in the ONE list with the aims');
chk(!/Randomize chain/.test(SRC), '[5] the old bottom-of-menu "Randomize chain" action is gone');
chk(/window\.__tpDice=function\(\)\{[^\n]{0,80}rollDice\(\)/.test(SRC) && !/<span class="g" data-t="dice"/.test(SRC),
    '[5] the ONE dice (the header\'s) dispatches through rollDice; the canvas has no dice tool of its own (tp39)');
chk(/function rollDice\(\)\{[\s\S]{0,220}layout\.aim/.test(SRC),
    '[5] rollDice reads layout.aim — so it persists and repeats without reopening the menu');

// ── 6 · FLOW: MORE THAN ONE CARD ─────────────────────────────────────────────────────────────
const FLOW_INST_MAX = eval('(' + span("FLOW_INST_MAX={", "}").replace(/^FLOW_INST_MAX=/, '') + ')');
const flowSrc = span("var FLOWBAG={", "chain=shuffle(chain);");
const buildChain = new Function('aim', 'R', 'rnd', 'pick', 'shuffle', 'ispan', 'FLOW_INST_MAX',
  'var rollInfo={}, chain=[];\n' + flowSrc.replace(/\/\*[\s\S]*?\*\//g, '') + '\n return chain;');   // tp36 — the flow step now prices the roll through rollInfo (a module var)
const shuffle = a => { a = a.slice(); for (let i = a.length - 1; i > 0; i--) { const j = Math.floor(rnd() * (i + 1)); [a[i], a[j]] = [a[j], a[i]]; } return a; };
const ispan = r => Math.round(r[0] + rnd() * (r[1] - r[0]));
let sawMulti = false, sawTwoOfKind = false, overCeiling = null, maxLen = 0;
for (const lvl of LEVELS)
  for (const aim of SOUND_AIMS)
    for (let n = 0; n < 3000; n++) {
      const c = buildChain(aim, REACH[lvl], rnd, pick, shuffle, ispan, FLOW_INST_MAX);
      maxLen = Math.max(maxLen, c.length);
      if (c.length > 1) sawMulti = true;
      const kinds = {};
      for (const m of c) { const k = String(m).replace(/\d+$/, ''); kinds[k] = (kinds[k] || 0) + 1;
        if (kinds[k] > (FLOW_INST_MAX[k] || 1)) overCeiling = `${k} x${kinds[k]} (max ${FLOW_INST_MAX[k]})`; }
      if (Object.values(kinds).some(v => v >= 2)) sawTwoOfKind = true;
      if (new Set(c).size !== c.length) overCeiling = 'duplicate instance key in ' + c.join(',');
    }
chk(sawMulti, '[6] the dice puts MORE THAN ONE flow card in the chain');
chk(sawTwoOfKind, '[6] and two of the same kind (two arps, two glitches) really happen');
chk(overCeiling === null, "[6] no kind ever exceeds its instance ceiling", overCeiling || '');
chk(maxLen <= 8, '[6] the chain stays sane', `longest: ${maxLen}`);
chk(/__tiDiceMode\)\s*window\.__tiDiceMode\(cardPidOf\(m\),big\)/.test(SRC),
    '[6] every instance is diced on ITS OWN card (glitch 2 is not glitch 1 again)');
chk(/take\('flow'\)/.test(SRC), '[6] and flow instances are LFO targets — routed to something of their own');


// ── 7 · THE ONE-SHOT WHITELIST IS THE MEASUREMENT, NOT A HAND LIST ───────────────────────────
{
  const csvPath = require('path').join(__dirname, 'fixtures', 'oneshot_tuning.csv');
  const rows = fs.readFileSync(csvPath, 'utf8').trim().split('\n').slice(1).map(l => {
    const m = /^"([^"]+)",([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$/.exec(l);
    return m ? { path: m[1], cents: parseFloat(m[5]), uncertain: m[7].trim() === '1' } : null;
  }).filter(Boolean);
  const wantByCat = {};
  for (const r of rows) {
    if (r.uncertain || Math.abs(r.cents) > 15) continue;
    const [c, n] = [r.path.slice(0, r.path.indexOf('/')), r.path.slice(r.path.indexOf('/') + 1)];
    (wantByCat[c] = wantByCat[c] || []).push(n);
  }
  const baked = eval('(' + lift(/var TUNED_ONESHOTS=\{/).replace(/^var TUNED_ONESHOTS=/, '') + ')');
  const wantTotal = Object.values(wantByCat).reduce((a, v) => a + v.length, 0);
  const gotTotal = Object.values(baked).reduce((a, v) => a + v.length, 0);
  chk(gotTotal === wantTotal, `[7] the baked list is exactly the measured set (${gotTotal} vs ${wantTotal})`);
  let mismatch = null;
  for (const c of Object.keys(wantByCat)) {
    const a = [...wantByCat[c]].sort().join('|'), b = [...(baked[c] || [])].sort().join('|');
    if (a !== b) mismatch = c;
  }
  chk(mismatch === null, '[7] and category-for-category, file-for-file', mismatch ? `first mismatch:  ${mismatch}` : '');
  const anyOff = rows.some(r => !r.uncertain && Math.abs(r.cents) > 15);
  chk(anyOff, '[7] the CSV really does contain off-pitch files (a whitelist that excludes nothing is not one)');
  const flat = new Set(Object.values(baked).flat());
  chk(flat.size === gotTotal, '[7] no duplicate names inside the baked list');
  chk(/files=filesFor\(d,c,aim\)\.filter\(/.test(SRC) && /var f=pick\(files\), path=d\.path\+'\/'\+c\+'\/'\+f/.test(SRC) && /function filesFor\(d,c,aim\)\{ return aim==='drums'\?\(d\.cats\[c\]\|\|\[\]\)\.slice\(\):tunedFiles\(d,c\); \}/.test(SRC),
      '[7] oneShot loads from the FILTERED list (tuned; drums take every file), never from the raw scan');
  chk(!/DRUMCATS/.test(SRC), '[7] the drum path is gone entirely (Max: "we\'re not using drums")');
}


// ── 8 · THE DICE ON THE SYNTH PAGE ───────────────────────────────────────────────────────────
{
  const hdr = SRC.slice(SRC.indexOf('<div class="header-right">'), SRC.indexOf('</div>', SRC.indexOf('id="settings-btn"')));
  chk(/id="dice-btn"/.test(hdr), '[8] a dice button exists in the header');
  chk(hdr.indexOf('id="dice-btn"') < hdr.indexOf('id="settings-btn"'),
      '[8] it sits immediately beside the gear, and the gear stays last (its 16px inset)');
  chk(/class="settings-btn dice-btn"/.test(hdr),
      '[8] it wears the gear\'s own box, so header-right\'s gap spaces them equally');
  chk(/db\.addEventListener\('click'[\s\S]{0,240}window\.__tpDice\(\)/.test(SRC),
      '[8] left-click rolls through the Patcher module (no second copy of the dice)');
  chk(/db\.addEventListener\('contextmenu'[\s\S]{0,200}__tpDiceMenu/.test(SRC),
      '[8] right-click aims it');
  // the trap this guards: generate() ends by ADOPTING the rack cards onto the canvas. Fired from
  // the synth page with the canvas shut, that would move the user's cards out from under them.
  chk(/if\(isOpen\)\{ adoptFx\(\); syncPresence\(\); sync\(\); \}\s*\n\s*commit\(\);/.test(SRC),
      '[8] a roll with the Patcher SHUT never adopts or re-seats the canvas');
  chk(/if\(isOpen\) glideTidy\(\);/.test(SRC), '[8] and never re-tidies it');
  chk(/window\.__tpDice=function\(\)\{ try\{ build\(\); \}catch\(e\)\{\} try\{ rollDice\(\); \}/.test(SRC),
      '[8] the export builds the canvas DOM (which does not adopt) before rolling');
  chk(/function pageToast\(s\)/.test(SRC) && /if\(!isOpen\)\{ pageToast\(s\); return; \}/.test(SRC),
      '[8] and the roll still says what it did, with the canvas hidden');
}


// ── 9 · FLOW INSTANCES NEVER REACH THE SYNTH PAGE ────────────────────────────────────────────
//   Max's standing rule after tp23 put two glitches in a chain: "please don't add flow cards to the
//   synth page. Only add flow cards to the patcher page." The tile grid lives on the synth page
//   whenever the canvas is shut, so an instance tile added a row there and pushed the filter beside
//   it out of shape. And its number badge re-broke fb136, which had already removed numbers from
//   these tiles at Max's request.
{
  chk(/t\.classList\.add\('flow-inst'\)/.test(SRC),
      '[9] a minted instance tile is marked flow-inst');
  chk(/#syn-panel \.flow-mode\.flow-inst \{ display: none; \}/.test(SRC),
      '[9] and the synth page refuses it — no extra tile, no number, no shifted layout');
  chk(/#tp-page \.tp-node > \.tp-body > \.flow-mode\.flow-inst \{ display: flex; \}/.test(SRC),
      '[9] while the Patcher canvas still shows it as a node');
  const hideAt = SRC.indexOf('#syn-panel .flow-mode.flow-inst');
  const showAt = SRC.indexOf('#tp-page .tp-node > .tp-body > .flow-mode.flow-inst');
  chk(hideAt >= 0 && showAt > hideAt,
      '[9] the reveal comes AFTER the hide (#tp-page is nested inside #syn-panel)');
  // the dice button Max asked to keep, at the size he asked for
  chk(/\.dice-btn svg \{ width: 23px; height: 23px;/.test(SRC),
      '[9] the dice glyph matches the gear rather than reading small beside it');
  chk(/#dice-btn \+ \.settings-btn \{ margin-left: 0; \}/.test(SRC),
      '[9] and the two buttons sit together');
}


// ── 10 · A SAMPLER ALWAYS GETS A ONE-SHOT, OR STOPS BEING A SAMPLER ──────────────────────────
//   Max: "sometimes it randomizes stuff and there's no one shot ... I think we came to a fuck up."
//   A sample engine with nothing loaded is a silent oscillator in the patch. The load is async, so
//   the failure path has to write the engine back itself.
{
  chk(/function oneShot\(o,aim,onFail\)/.test(SRC), '[10] oneShot takes a failure callback');
  const body = SRC.slice(SRC.indexOf('function oneShot(o,aim,onFail)'), SRC.indexOf('function drumBlend('));
  // tp39 — every `return;` is a reported bail-out (fail();), a retry (attempt(k+1);), or the verify finding the sample landed
  const bare = (body.match(/[^\n]*\breturn;/g) || []).filter(l => !/fail\(\); return;|attempt\(k\+1\); return;|length>2\) return;|if\(!g\) return;/.test(l));
  chk(bare.length === 0, `[10] every bail-out reports it (${bare.length} unreported return; in oneShot)`, bare.join(' | ').slice(0, 200));
  chk(/if\(samp\)\{ rollSample[\s\S]{0,200}oneShot\(o,aim,function\(\)\{/.test(SRC),
      '[10] and the dice passes one when it picks a sampler');
  chk(/setP\(X\+'ENGINE', choiceNorm\(X\+'ENGINE', 0, 7\)\)/.test(SRC),
      '[10] whose fallback puts the oscillator back on a wavetable');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
