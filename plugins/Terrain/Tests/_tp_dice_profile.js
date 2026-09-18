// CPU profile of the Patcher dice at ~100 nodes (dev tool, not a gate). node Tests/_tp_dice_profile.js [index.html]
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
const SRC = process.argv[2] || (process.cwd() + '/Source/ui/public/index.html');
const fs = require('fs'); const sim = fs.readFileSync(__dirname + '/_ui_lockin_sim.js', 'utf8');
const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  await p.evaluateOnNewDocument(stubSrc + '\nstub();');
  await p.goto('file://' + SRC + '?page=5', { waitUntil: 'load' }); await sleep(2500);
  const n = await p.evaluate(async () => { const keys = window.__tpCatalog().map(x => x.k || x.key || x.id); let i = 0; for (let r = 0; r < 8 && window.__tpNodes().length < 100; r++) for (const k of keys) { if (window.__tpNodes().length >= 100) break; try { window.__tpAdd(k, 80 + (i % 10) * 180, 80 + Math.floor(i / 10) * 150); } catch (e) {} i++; await new Promise(r => setTimeout(r, 10)); } return window.__tpNodes().length; });
  console.log('nodes', n); await sleep(1500);
  const cdp = await p.target().createCDPSession(); await cdp.send('Profiler.enable'); await cdp.send('Profiler.setSamplingInterval', { interval: 200 }); await cdp.send('Profiler.start');
  const times = await p.evaluate(async () => { const out = []; for (let i = 0; i < 3; i++) { const t0 = performance.now(); window.__tpDice(); out.push(Math.round(performance.now() - t0)); await new Promise(r => setTimeout(r, 400)); } return out; });
  const { profile } = await cdp.send('Profiler.stop');
  const self = {}; const byId = {}; profile.nodes.forEach(nd => byId[nd.id] = nd);
  const dt = profile.timeDeltas; profile.samples.forEach((id, i) => { const nd = byId[id]; const cf = nd.callFrame; const k = (cf.functionName || '(anon)') + ' @' + cf.lineNumber; self[k] = (self[k] || 0) + (dt[i] || 0); });
  // inclusive by walking parents
  const parent = {}; profile.nodes.forEach(nd => (nd.children || []).forEach(c => parent[c] = nd.id));
  const incl = {}; profile.samples.forEach((id, i) => { const seen = {}; let cur = id; while (cur != null) { const cf = byId[cur].callFrame; const k = (cf.functionName || '(anon)') + ' @' + cf.lineNumber; if (!seen[k]) { seen[k] = 1; incl[k] = (incl[k] || 0) + (dt[i] || 0); } cur = parent[cur]; } });
  console.log('dice ms', times);
  console.log('SELF top:'); Object.entries(self).sort((a, b) => b[1] - a[1]).slice(0, 18).forEach(e => console.log('  ' + (e[1] / 1000).toFixed(0).padStart(6) + ' ms  ' + e[0]));
  console.log('INCLUSIVE top:'); Object.entries(incl).sort((a, b) => b[1] - a[1]).slice(0, 28).forEach(e => console.log('  ' + (e[1] / 1000).toFixed(0).padStart(6) + ' ms  ' + e[0]));
  await b.close();
})().catch(e => { console.log('FAIL', e); process.exit(1); });
