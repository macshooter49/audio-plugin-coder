// tp46 — A ROUTE THE PAGE JUST MADE KEEPS ITS MARK (page 1, stubbed backend). Max: "try to drag key onto the wavetable — you can't...
//   right-clicked the wavetable position and it's being modulated by envelope 2, but there's no line". Key → WT Pos in the 3D
//   waterfall view writes the route and marks the knob; a stale getSynthMod mirror ('[]', the reply of a poll that left before
//   the write landed) must not prune it for 4 s; Key → filter cutoff / resonance the same.
//                                                     NODE_PATH=Tests/node_modules node Tests/_tp46_mod_marks_gate.js
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
(async () => { const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656 }); await p.evaluateOnNewDocument(stubSrc + '\nstub();'); const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 120)));
  await p.goto('file://' + process.cwd() + '/Source/ui/public/index.html?page=1', { waitUntil: 'load' }); await sleep(2200);
  await p.evaluate(() => { ['a','b','c','d'].forEach(o => document.getElementById('osc-' + o + '-device').classList.remove('osc-off')); });
  const chk = (tag) => p.evaluate((tag) => { const el = window.__tiElForDest(2); const k = el && el.closest('.knob'); const r = k ? k.getBoundingClientRect() : null; const dev = document.getElementById('osc-a-device');
    return { tag, cls: dev.className, el: !!el, syn: k ? k.getAttribute('data-syn') : null, rect: r ? Math.round(r.width) + 'x' + Math.round(r.height) : null, vis: k ? getComputedStyle(k).visibility : null, marked: k ? k.classList.contains('sm-modded') : null, routes: window.__tiRoutes().filter(x => x.d === 2).map(x => x.s) }; }, tag);
  const R = []; R[0] = await chk('flat');
  await p.evaluate(() => { window.wtWaterfall.on.a = true; document.getElementById('osc-a-device').classList.add('wt3d'); try { window.wtWaterfall.kick(); } catch (e) {} }); await sleep(600);
  await p.evaluate(() => window.__tiAddSrc({ note: 1 }, 2)); await sleep(300); R[1] = await chk('wt3d + key route');
  // a stale mirror reply: the engine answers with NO routes 200 ms after the add
  await p.evaluate(() => { const g = window.Juce.getNativeFunction; window.Juce.getNativeFunction = (n) => n === 'getSynthMod' ? (() => Promise.resolve('[]')) : g(n); }); await sleep(2800); R[2] = await chk('after a stale [] mirror');
  const flt = await p.evaluate(async () => { const g = window.Juce.getNativeFunction; window.Juce.getNativeFunction = (n) => n === 'getSynthMod' ? (() => Promise.resolve('[]')) : g(n); window.__tiAddSrc({ note: 1 }, 0); window.__tiAddSrc({ env: 2 }, 54); await new Promise(r => setTimeout(r, 2800)); return { cut: window.__tiRoutes().filter(x => x.d === 0).map(x => x.s), res: window.__tiRoutes().filter(x => x.d === 54).map(x => x.s), cutEl: !!window.__tiElForDest(0), resEl: !!window.__tiElForDest(54) }; });
  let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };
  ok(R[1].el && R[1].syn === 'SYN_OSC_A_WT_FRAME' && R[1].marked && R[1].routes.indexOf('key') >= 0, '[1] Key → WT Pos (3D view) writes the route and marks the knob', JSON.stringify(R[1]));
  ok(R[2].marked && R[2].routes.indexOf('key') >= 0, '[2] a stale [] mirror reply does not prune it (the mark stays)', JSON.stringify(R[2]));
  ok(flt.cut.indexOf('key') >= 0 && flt.res.indexOf('env2') >= 0 && flt.cutEl && flt.resEl, '[3] Key → cutoff and Env 2 → resonance survive the same stale mirror', JSON.stringify(flt));
  ok(errs.length === 0, 'no page errors', errs.join('|'));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0); })();
