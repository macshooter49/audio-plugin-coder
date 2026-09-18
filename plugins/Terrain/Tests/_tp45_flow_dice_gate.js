// tp45 — THE FLOW CARDS' FLICKER (page 5, stubbed backend with a LATE native). Max: "every time I try to randomize the flow cards
//   they pop up and fade in on the patcher and then fade out, and on my main synth page they start flickering". The stub applies
//   every setSynParam LAT ms late (a dice roll's burst on the message thread) while getSynParams answers from what has landed: a
//   poll issued before the roll answers after it with the old chain. The pool relay and the slider shim must not undo a fresh write.
//   Rolls the dice ROLLS times and watches the chain / tiles / canvas for 3 s after each: a change after 800 ms is a flicker.
//   LAT=400 ROLLS=40 node Tests/_tp45_flow_dice_gate.js   (mutation: drop the __wAt / wAt guards → 3-4 of 40 rolls flicker)
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const PAGE = process.env.PG || '5', LAT = +(process.env.LAT || 400), ROLLS = +(process.env.ROLLS || 40);
const latency = (ms) => { const g = window.Juce.getNativeFunction; window.__pending = {}; window.Juce.getNativeFunction = (n) => { const f = g(n); if (n !== 'setSynParam' || !ms) return f; return (id, v) => new Promise(r => { setTimeout(() => { f(id, v); }, ms); r(0); }); }; };
(async () => { const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 }); await p.evaluateOnNewDocument(stubSrc + '\nstub();' + (LAT ? '(' + latency.toString() + ')(' + LAT + ');' : ''));
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 120)));
  await p.goto('file://' + process.cwd() + '/Source/ui/public/index.html?page=' + PAGE, { waitUntil: 'load' }); await sleep(2500);
  await p.evaluate(() => { ['a','b'].forEach(o => { const e = document.getElementById('osc-' + o + '-device'); if (e) e.classList.remove('osc-off'); }); });
  const state = () => p.evaluate(() => { window.__dbg = (window.__tpNodes ? window.__tpNodes().length : -1) + ":" + document.body.classList.contains("tp-open"); const ch = window.__flowChain().join(','); const tiles = [...document.querySelectorAll('#flow-modes .flow-mode')].map(t => t.dataset.mode + (t.classList.contains('act') ? '*' : '')).join(','); const nodes = window.__tpNodes ? window.__tpNodes().filter(n => /^flow-/.test(n.key)).map(n => n.key).join(',') : '-'; const pr = Object.keys(window.__params).filter(k => /^FLOW_CHAIN/.test(k)).sort().map(k => k.replace('FLOW_CHAIN_', '') + '=' + (+window.__params[k]).toFixed(2)).join(' '); return { ch, tiles, nodes, pr, dbg: window.__dbg }; });
  let unstable = 0;
  for (let r = 0; r < ROLLS; r++) {
    await p.evaluate(() => { try { window.__tpDice(); } catch (e) {} });
    const seq = []; for (let t = 0; t < 30; t++) { await sleep(100); const s = await state(); const key = s.ch + '|' + s.tiles + '|' + s.nodes; if (!seq.length || seq[seq.length - 1].key !== key) seq.push({ t: t * 100, key, s }); }
    const late = seq.filter(x => x.t >= 800);
    if (late.length >= 2 || seq.length > 4) { unstable++; console.log(`roll ${r}: ${seq.length} states, ${late.length} changes after 800 ms`); seq.forEach(x => console.log(`   t=${x.t} chain[${x.s.ch}] nodes[${x.s.nodes}] tiles[${x.s.tiles}]`)); }
  }
  const fin = await state(); console.log(`page ${PAGE} lat ${LAT}: ${ROLLS} rolls, ${unstable} unstable; final chain [${fin.ch}] dbg ${fin.dbg} params ${fin.pr}; errors ${errs.length} ${errs.slice(0, 3).join(' | ')}`);
  await b.close(); console.log((unstable === 0 && errs.length === 0 ? '  PASS  ' : '  FAIL  ') + 'no roll flickers after it lands (' + unstable + ' of ' + ROLLS + ')'); process.exit(unstable === 0 && errs.length === 0 ? 0 : 1); })();
