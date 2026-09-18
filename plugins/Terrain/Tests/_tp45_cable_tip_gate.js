// tp45 — WHAT IS CONNECTED TO WHAT (page 5, stubbed backend). Max: "hover over our node cables and it shows what's being connected to
//   what... a little x to detach it" · the Voices module "exactly like the synth page, popped out; I'm supposed to be able to click on
//   the name and drag it" · "I don't want the dice to light up with my mouse hovering over it, the same color as the settings".
//   [1] hovering a cable shows the chip naming both ends and lights that cable; [2] its × cuts an editable cable (A's output cable →
//   SYN_OSC_A_OUT 0, the cable gone); [3] hovering a module lights every cable it owns; [4] the Voices module's Velocity label starts a
//   mod-source drag on the canvas (sm-dragging); [5] its pills keep the synth page's border; [6] the header dice wears the gear's ink and
//   does not change on hover.                                          NODE_PATH=Tests/node_modules node Tests/_tp45_cable_tip_gate.js
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs'); const path = require('path');
const SRC = process.argv[2] || path.resolve(__dirname, '../Source/ui/public/index.html');
const sim = fs.readFileSync(path.join(__dirname, '_ui_lockin_sim.js'), 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };
(async () => { const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 }); await p.evaluateOnNewDocument(stubSrc + '\nstub();'); const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + SRC + '?page=1', { waitUntil: 'load' }); await sleep(2000);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off'); window.__params['SYN_OSC_A_OUT'] = 1; });
  await p.evaluate(() => window.setActivePanel('tp')); await sleep(1500); await p.evaluate(() => { try { window.__tpAdd('voice', 40, 400); } catch (e) {} window.__tpFit(); }); await sleep(800);
  const mid = (id) => p.evaluate((id) => { const g = id ? document.querySelector('#tp-page .tp-cables .cable[data-id="' + id + '"]') : document.querySelector('#tp-page .tp-cables .cable'); if (!g) return null; const core = g.querySelector('.core'); const L = core.getTotalLength(); const pt = core.getPointAtLength(L / 2); const m = core.getScreenCTM(); return { x: m.a * pt.x + m.c * pt.y + m.e, y: m.b * pt.x + m.d * pt.y + m.f, id: g.getAttribute('data-id') }; }, id);
  // [1] hover a cable
  const c0 = await mid(null); await p.mouse.move(c0.x, c0.y); await sleep(350); const t1 = await p.evaluate(() => window.__tpCableTip());
  ok(t1.on && t1.cable === c0.id && /→/.test(t1.text) && t1.lit.indexOf(c0.id) >= 0, '[1] hovering a cable shows the chip naming both ends and lights it', JSON.stringify(t1));
  // [2] the × cuts A's output cable
  const ids = await p.evaluate(() => [...document.querySelectorAll('#tp-page .tp-cables .cable')].map(g => g.getAttribute('data-id')));
  const dry = ids.find(i => /^osc-a\.out0>out\.in0$/.test(i)) || ids.find(i => /^osc-a\./.test(i));
  // a point of that cable no module covers (cables run under the modules)
  const c2 = await p.evaluate(async (id) => { const g = document.querySelector('#tp-page .tp-cables .cable[data-id="' + id + '"]'); const core = g.querySelector('.core'); const L = core.getTotalLength(); const m = core.getScreenCTM();
    for (let f = 0.05; f < 0.96; f += 0.05) { const pt = core.getPointAtLength(L * f); const x = m.a * pt.x + m.c * pt.y + m.e, y = m.b * pt.x + m.d * pt.y + m.f; const e = document.elementFromPoint(x, y); if (e && e.closest && e.closest('.cable') === g) return { x, y, f }; } return null; }, dry);
  if (c2) { await p.mouse.move(c2.x, c2.y); await sleep(350); }
  const xr = await p.evaluate(() => { const x = document.querySelector('#tp-ctip .x'); const r = x.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, vis: r.width > 0 }; });
  await p.mouse.move(xr.x, xr.y); await sleep(120); await p.mouse.down(); await p.mouse.up(); await sleep(900);
  const after = await p.evaluate((id) => ({ gone: !document.querySelector('#tp-page .tp-cables .cable[data-id="' + id + '"]:not(.gone)'), out: window.__params['SYN_OSC_A_OUT'], tip: window.__tpCableTip().on }), dry);
  ok(!!c2 && xr.vis && after.gone && after.out === 0 && !after.tip, '[2] the chip\'s × cuts A\'s output cable (SYN_OSC_A_OUT 0, the cable leaves, the chip closes)', JSON.stringify({ dry, xr, after }));
  // [3] hover a module
  const nb = await p.evaluate(() => { const n = window.__tpNodeByKey('osc-a'); const r = n.wrap.getBoundingClientRect(); return { x: r.left + 10, y: r.top + 10 }; });
  await p.mouse.move(nb.x, nb.y); await sleep(250); const lit = await p.evaluate(() => window.__tpCableTip().lit);
  ok(lit.length >= 1 && lit.every(i => /osc-a/.test(i)), '[3] hovering Oscillator A lights every cable it owns and no other', JSON.stringify(lit));
  // [4] the Voices module: Velocity drags as a mod source on the canvas
  const vl = await p.evaluate(() => { const e = document.querySelector('#tp-page [data-drag-vel]'); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  let dragging = false; if (vl) { await p.mouse.move(vl.x, vl.y); await p.mouse.down(); await p.mouse.move(vl.x + 30, vl.y + 30, { steps: 5 }); await sleep(120); dragging = await p.evaluate(() => document.body.classList.contains('sm-dragging')); await p.mouse.up(); await sleep(200); }
  ok(!!vl && dragging, '[4] the Voices module\'s Velocity label starts a mod-source drag on the canvas', JSON.stringify({ vl, dragging }));
  // [5] the pills keep the synth page's border
  const pill = await p.evaluate(() => { const a = document.querySelector('#tp-page .tp-voice .voice-toggle'); const cs = a ? getComputedStyle(a) : null; return cs ? { bw: cs.borderTopWidth, bs: cs.borderTopStyle } : null; });
  ok(pill && pill.bw !== '0px' && pill.bs !== 'none', '[5] the Voices module\'s pills keep their border on the canvas', JSON.stringify(pill));
  // [6] the dice = the gear
  const dice = await p.evaluate(() => { const d = document.getElementById('dice-btn'); const cs = getComputedStyle(d); const ref = getComputedStyle(document.documentElement).getPropertyValue('--text-secondary').trim(); const c = document.createElement('div'); c.style.color = ref; document.body.appendChild(c); const refC = getComputedStyle(c).color; c.remove(); return { color: cs.color, ref: refC }; });
  const dr = await p.evaluate(() => { const r = document.getElementById('dice-btn').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; }); await p.mouse.move(dr.x, dr.y); await sleep(200);
  const hov = await p.evaluate(() => getComputedStyle(document.getElementById('dice-btn')).color);
  ok(dice.color === dice.ref && hov === dice.color, '[6] the header dice wears the gear\'s ink and keeps it under the pointer', JSON.stringify({ dice, hov }));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0); })().catch(e => { console.log('FAIL', e); process.exit(1); });
