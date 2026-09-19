// tp47 — THE MOD MARK ALWAYS READS, AND THE FILTER GRID IS PLAYABLE ON THE CANVAS. Max: "the filter is NOT showing the modulation
//   sources, you see ENV 2 but I see NO ATTENUATION LINE... we need to ALWAYS see mod sources" · "make it where i move the filter with
//   my mouse on the patcher just like i do on the synth page... when i touch the edges i can move it around".
//   [1] Env 2 → cutoff at 11 % depth: the underline sits under the cutoff emblem, shown, its territory ≥ 6 px and purple, at rest;
//   [2] the same mark after the filter is adopted by the Patcher; [3] a drag across the filter's grid (.fxy) on the canvas does not
//   move the node; a drag from its top edge does.               NODE_PATH=Tests/node_modules node Tests/_tp47_marks_gate.js
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms)); const fs = require('fs'); const path = require('path');
const SRC = process.argv[2] || path.resolve(__dirname, '../Source/ui/public/index.html');
const sim = fs.readFileSync(path.join(__dirname, '_ui_lockin_sim.js'), 'utf8'); const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
let pass = 0, fail = 0; const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l); } else { fail++; console.log('  FAIL  ' + l + (d ? '\n          ' + d : '')); } };
(async () => { const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 }); await p.evaluateOnNewDocument(stubSrc + '\nstub();'); const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0, 160)));
  await p.goto('file://' + SRC + '?page=1', { waitUntil: 'load' }); await sleep(2200);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off'); });
  const mark = () => p.evaluate(() => { const el = window.__tiElForDest(0); if (!el) return { el: false }; const r = el.getBoundingClientRect(); const uls = [...document.querySelectorAll('.sm-ul')].map(u => { const q = u.getBoundingClientRect(); return { u, q }; }).filter(x => x.q.width > 0 && Math.abs(x.q.left - r.left) < 6 && x.q.top >= r.bottom - 4 && x.q.top <= r.bottom + 12);
    if (!uls.length) return { el: true, ul: false, n: document.querySelectorAll('.sm-ul').length }; const u = uls[0].u; const sp = u.querySelector('.smu-span'), rail = u.querySelector('.smu-rail'); const cs = getComputedStyle(sp), cr = getComputedStyle(rail);
    return { el: true, ul: true, disp: getComputedStyle(u).display, spW: sp.getBoundingClientRect().width, spBg: cs.backgroundColor, railBg: cr.backgroundColor, w: u.getBoundingClientRect().width }; });
  await p.evaluate(async () => { const t = window.__tiAddSrc({ env: 2 }, 0); window.__tiSetDepth(t, 0, 0.11); await new Promise(r => setTimeout(r, 300)); try { window.__tiRepaint(); } catch (e) {} });
  const m1 = await mark();
  const purple = (c) => /rgba?\(183, 148, 255/.test(c);
  ok(m1.ul && m1.disp === 'block' && m1.spW >= 6 && purple(m1.spBg) && purple(m1.railBg), '[1] Env 2 → cutoff at 11 %: the underline sits under the cutoff emblem, shown at rest, territory ≥ 6 px and purple', JSON.stringify(m1));
  await p.evaluate(() => window.setActivePanel('tp')); await sleep(1500); await p.evaluate(() => { const n = window.__tpNodeByKey('filter'); const pg = document.getElementById('tp-page').getBoundingClientRect(); window.__tpSetView(-(n.x) + 60, -(n.y) + 120, 1); try { window.__tiRepaint(); } catch (e) {} }); await sleep(500);
  const m2 = await mark();
  ok(m2.ul && m2.disp === 'block' && m2.spW >= 6, '[2] the same mark under the emblem once the filter sits on the Patcher', JSON.stringify(m2));
  const g = await p.evaluate(() => { const n = window.__tpNodeByKey('filter'); const f = n.wrap.querySelector('.fxy'); const w = n.wrap.getBoundingClientRect(); const r = f ? f.getBoundingClientRect() : null; return r ? { fx: r.left + r.width / 2, fy: r.top + r.height / 2, tx: w.left + w.width * 0.3, ty: w.top + 3, x: n.x, y: n.y } : null; });
  if (g) { await p.mouse.move(g.fx, g.fy); await p.mouse.down(); await p.mouse.move(g.fx + 40, g.fy + 20, { steps: 6 }); await p.mouse.up(); await sleep(200); }
  const after1 = await p.evaluate(() => { const n = window.__tpNodeByKey('filter'); return { x: n.x, y: n.y }; });
  if (g) { await p.mouse.move(g.tx, g.ty); await p.mouse.down(); await p.mouse.move(g.tx + 40, g.ty + 20, { steps: 6 }); await p.mouse.up(); await sleep(200); }
  const after2 = await p.evaluate(() => { const n = window.__tpNodeByKey('filter'); return { x: n.x, y: n.y, down: window.__tpLastDown }; });
  ok(!!g && after1.x === g.x && after1.y === g.y && (after2.x !== g.x || after2.y !== g.y), '[3] a drag across the filter\'s grid plays the grid (the node stays); a drag from its top edge moves the node', JSON.stringify({ g, after1, after2 }));
  ok(errs.length === 0, 'no page errors', errs.join(' | '));
  await b.close(); console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0); })().catch(e => { console.log('FAIL', e); process.exit(1); });
