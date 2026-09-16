const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 2 });
  const errs = [], logs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message)); p.on('console', m => { const t = m.text(); if (/\[tp\]/.test(t)) logs.push(m.type() + ': ' + t.slice(0, 300)); });
  const html = process.argv[2], out = process.argv[3]; const R = {};
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tiDiceSel', JSON.stringify({ arp: 0, chop: 1, gli: 1, rbn: 0, 'osc:a': 1 })); localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + html + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); ['b','c','d'].forEach(o => document.getElementById('osc-' + o + '-device').classList.add('osc-off')); document.getElementById('osc-a-device').classList.remove('osc-off'); try { window.__fxrAdd('reverb'); } catch (e) {} try { window.__flowSetChain(['arp', 'glitch']); } catch (e) {} document.getElementById('syn-btn').click(); }); await sleep(1600);
  const nodes = () => p.evaluate(() => window.__tpNodes().map(n => n.key + ':' + n.w + 'x' + n.h + (n.ext ? '*' : '')));
  R.s1 = await p.evaluate(() => ({ nodes: window.__tpNodes().map(n => n.key + ':' + n.w + 'x' + n.h), pwr: document.querySelectorAll('#tp-page .tp-pwr, #tp-page .tp-msel').length, rndCv: !!document.querySelector('#tp-page .tp-node[data-key="rnd"] .reso-cv'), rndSize: (n => n && n.offsetWidth + 'x' + n.offsetHeight)(document.querySelector('#tp-page .tp-node[data-key="rnd"]')),
    flowTiles: [...document.querySelectorAll('#tp-page .tp-node[data-kind="flow"] .flow-mode')].map(t => t.dataset.mode), modesInCanvas: !!document.querySelector('#tp-page .tp-nodes > #flow-modes'), hiddenTiles: [...document.querySelectorAll('#tp-page .tp-nodes > #flow-modes > .flow-mode')].map(t => t.dataset.mode + ':' + getComputedStyle(t).display),
    oscX: document.querySelectorAll('#tp-page .tp-node[data-kind="osc"] .tp-x').length, fxPwr: [...document.querySelectorAll('#tp-page .fxr-pwr')].map(e => getComputedStyle(e).display), cats: null, wf: window.wtWaterfall ? window.wtWaterfall.on : null,
    flowMods: document.querySelectorAll('#tp-page .tp-node[data-kind="flow"] .tp-port.k-mod').length, willChange: getComputedStyle(document.querySelector('#tp-page .tp-world')).willChange, cables: window.__tpCables().length, view: window.__tpView() }));
  await p.evaluate(() => window.__tpFit()); await sleep(250); await p.screenshot({ path: out + '-fit.png' });
  try {
  // ── presence: remove osc B (X), add tape Porta, the deck, a Cassette card; flow ext / min
  await p.evaluate(() => { window.__tpRemove('osc-b'); }); await sleep(120);
  R.s2 = { afterRemoveB: await nodes(), bHome: await p.evaluate(() => { const d = document.getElementById('osc-b-device'); return { off: d.classList.contains('osc-off'), inNode: !!d.closest('.tp-node'), parent: d.parentElement && (d.parentElement.className || d.parentElement.id) }; }) };
  await p.evaluate(() => { window.__tpAdd('tape:1', 600, 900); window.__tpAdd('deck', 900, 900); window.__tpAdd('tapefx:Cassette', 1300, 900); window.__tpAdd('osc:b', 380, 200); }); await sleep(400);
  R.s3 = { nodes: await nodes(), tape: await p.evaluate(() => ({ machine: (typeof state !== 'undefined') ? state.tapeMachine : null, tapeOn: !document.getElementById('tape-toggle').classList.contains('off'), loopOn: !document.getElementById('loop-toggle').classList.contains('off'), devs: window.__fxrDevs().filter(Boolean).map(d => d.core + ':' + d.type + ':' + d.on), msel: document.querySelectorAll('#tp-page .tp-msel').length, deckStatus: (e => e && getComputedStyle(e).display)(document.querySelector('#tp-page .deck-status')), feedPos: (e => { const r = e.getBoundingClientRect(), n = e.closest('.tp-node').getBoundingClientRect(); return [(r.left - n.left) / window.__tpView().z | 0, (r.top - n.top) / window.__tpView().z | 0]; })(document.querySelector('#tp-page .tape-feed-btn')), undoPos: (e => { const r = e.getBoundingClientRect(), n = e.closest('.tp-node').getBoundingClientRect(); return [(n.right - r.right) / window.__tpView().z | 0, (r.top - n.top) / window.__tpView().z | 0]; })(document.querySelector('#tp-page .tape-undo-btn')), knobLabels: [...document.querySelectorAll('#tp-page #tape-loop-knobs .knob-label')].map(l => l.textContent + '/' + getComputedStyle(l).textTransform), gapTransportKnobs: (() => { const t = document.querySelector('#tp-page .tape-transport').getBoundingClientRect(), k = document.querySelector('#tp-page #tape-loop-knobs').getBoundingClientRect(); return ((k.top - t.bottom) / window.__tpView().z) | 0; })(), tapeTypePill: (e => e && getComputedStyle(e).display)(document.querySelector('#tp-page .tp-node[data-core="tape"] .fxr-type')) })) };
  await p.evaluate(() => { const n = window.__tpNodes().find(n => n.key === 'tapeloop'); if (n) window.__tpSetView(-(n.x - 20) * 1, -(n.y - 20) * 1, 1); }); await sleep(300); await p.screenshot({ path: out + '-deck.png', clip: { x: 0, y: 40, width: 460, height: 380 } });
  await p.evaluate(() => { const n = window.__tpNodes().find(n => n.key === 'tape'); if (n) window.__tpSetView(-(n.x - 20), -(n.y - 20), 1); }); await sleep(300); await p.screenshot({ path: out + '-tape.png', clip: { x: 0, y: 40, width: 300, height: 320 } });
  await p.evaluate(() => { window.__tpExt('flow-glitch', true); }); await sleep(400);
  R.s4 = { ext: await nodes(), hdr: await p.evaluate(() => { const c = document.querySelector('#tp-page .tp-node[data-key="flow-glitch"] .ti-card'); return c ? { x: getComputedStyle(c.querySelector('.h .x')).display, min: !!c.querySelector('.h .tp-min'), pop: getComputedStyle(c.querySelector('.h .pop')).display, tileHidden: getComputedStyle(document.querySelector('#tp-page .tp-node[data-key="flow-glitch"] .flow-mode')).display } : null; }) };
  await p.evaluate(() => { const n = window.__tpNodes().find(n => n.key === 'flow-glitch'); if (n) window.__tpSetView(-(n.x - 10) * 0.9, -(n.y - 10) * 0.9, 0.9); }); await sleep(300); await p.screenshot({ path: out + '-glitch-ext.png', clip: { x: 0, y: 40, width: 420, height: 460 } });
  await p.evaluate(() => { document.querySelector('#tp-page .tp-node[data-key="flow-glitch"] .ti-card .h .tp-min').click(); }); await sleep(300);
  R.s4.afterMin = await nodes(); R.s4.cardHome = await p.evaluate(() => { const c = document.querySelector('.ti-card.gli-ext'); return c ? c.parentElement.tagName + ':' + c.classList.contains('open') : 'none'; });
  } catch (e) { R["ERR "+'presence: remove osc B (X), add tape Por'] = String(e).slice(0,200); }
  console.error("stage done:", 'presence: remove osc B (X), add tape Por');

  try {
  // ── the randomizer: a click in the middle rolls (spy on __tpRollOsc), a drag from the edge moves
  await p.evaluate(() => { window.__rollCount = 0; const f = window.__tpRollOsc; window.__tpRollOsc = function (o) { window.__rollCount++; return f(o); }; const n = window.__tpNodes().find(n => n.key === 'rnd'); if (n) window.__tpSetView(-(n.x - 40), -(n.y - 40), 1); }); await sleep(250);
  const rr = await p.evaluate(() => { const r = document.querySelector('#tp-page .tp-node[data-key="rnd"]').getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; });
  await p.mouse.click(rr.x + rr.w / 2, rr.y + rr.h / 2); await sleep(200);
  const rollCount = await p.evaluate(() => window.__rollCount);
  const before = await p.evaluate(() => window.__tpNodes().find(n => n.key === 'rnd'));
  await p.mouse.move(rr.x + 6, rr.y + rr.h / 2); await p.mouse.down(); await p.mouse.move(rr.x + 60, rr.y + rr.h / 2 + 40, { steps: 6 }); await p.mouse.up(); await sleep(150);
  const after = await p.evaluate(() => window.__tpNodes().find(n => n.key === 'rnd'));
  R.s5 = { rollCount, moved: [after.x - before.x, after.y - before.y], rollAfterDrag: await p.evaluate(() => window.__rollCount) };
  await p.screenshot({ path: out + '-rnd.png', clip: { x: 0, y: 40, width: 360, height: 300 } });
  } catch (e) { R["ERR "+'the randomizer: a click in the middle ro'] = String(e).slice(0,200); }
  console.error("stage done:", 'the randomizer: a click in the middle ro');

  try {
  // ── drift: zoom about a fixed cursor; then 7 in + 7 out returns every node to the same place
  await p.evaluate(() => window.__tpFit()); await sleep(200);
  const cursor = { x: 420, y: 330 }; await p.mouse.move(cursor.x, cursor.y);
  const pos0 = await nodes(); const v0 = await p.evaluate(() => window.__tpView());
  const w0 = await p.evaluate((c) => { const r = document.getElementById('tp-page').getBoundingClientRect(), v = window.__tpView(); return { x: (c.x - r.left - v.x) / v.z, y: (c.y - r.top - v.y) / v.z }; }, cursor);
  let worst = 0, worstBox = 0;
  for (let i = 0; i < 14; i++) { await p.keyboard.down('Control'); await p.mouse.wheel({ deltaY: i < 7 ? -90 : 90 }); await p.keyboard.up('Control'); await sleep(40);
    const m = await p.evaluate((c, w0) => { const pg = document.getElementById('tp-page').getBoundingClientRect(), v = window.__tpView(); const w = { x: (c.x - pg.left - v.x) / v.z, y: (c.y - pg.top - v.y) / v.z }; const n = window.__tpNodes()[2]; const el = document.querySelector('#tp-page .tp-node[data-key="' + n.key + '"]').getBoundingClientRect(); const ex = pg.left + v.x + n.x * v.z, ey = pg.top + v.y + n.y * v.z; return { pt: Math.hypot(w.x - w0.x, w.y - w0.y), box: Math.max(Math.abs(el.left - ex), Math.abs(el.top - ey), Math.abs(el.width - n.w * v.z)) }; }, cursor, w0);
    worst = Math.max(worst, m.pt); worstBox = Math.max(worstBox, m.box); }
  const pos1 = await nodes(); const v1 = await p.evaluate(() => window.__tpView());
  R.s6 = { worstPt: +worst.toFixed(4), worstBox: +worstBox.toFixed(3), nodesSame: JSON.stringify(pos0) === JSON.stringify(pos1), zBack: +Math.abs(v1.z - v0.z).toFixed(4) };
  } catch (e) { R["ERR "+'drift: zoom about a fixed cursor; then 7'] = String(e).slice(0,200); }
  console.error("stage done:", 'drift: zoom about a fixed cursor; then 7');

  try {
  // ── crisp: at z=2 every canvas in a node should hold a bitmap that matches its screen size
  await p.evaluate(() => { const v = window.__tpView(); window.__tpSetView(v.x, v.y, 2); }); await sleep(900);
  R.s7 = await p.evaluate(() => { const dpr = window.devicePixelRatio, out = []; document.querySelectorAll('#tp-page .tp-node canvas').forEach(c => { const r = c.getBoundingClientRect(); if (r.width < 8 || r.height < 8) return; const ratio = c.width / (r.width * dpr); const n = c.closest('.tp-node'); out.push({ n: n.dataset.key, c: (c.id || c.className || '').toString().slice(0, 24), css: Math.round(r.width) + 'x' + Math.round(r.height), bmp: c.width + 'x' + c.height, ratio: +ratio.toFixed(2) }); }); return { blurry: out.filter(o => o.ratio < 0.85), crisp: out.filter(o => o.ratio >= 0.85).length, all: out.length }; });
  await p.evaluate(() => { const n = window.__tpNodes().find(n => n.key === 'osc-a'); if (n) window.__tpSetView(-(n.x - 10) * 2, -(n.y - 10) * 2, 2); }); await sleep(600); await p.screenshot({ path: out + '-z2.png', clip: { x: 0, y: 40, width: 700, height: 220 } });
  await p.evaluate(() => { const n = window.__tpNodes().find(n => n.key === 'tape'); if (n) window.__tpSetView(-(n.x - 10) * 2, -(n.y - 10) * 2, 2); }); await sleep(600); await p.screenshot({ path: out + '-z2-tape.png', clip: { x: 0, y: 40, width: 480, height: 520 } });
  } catch (e) { R["ERR "+'crisp: at z=2 every canvas in a node sho'] = String(e).slice(0,200); }
  console.error("stage done:", 'crisp: at z=2 every canvas in a node sho');

  try {
  // ── the eye: every picture fills its glass; the envelope shows no words
  await p.evaluate(() => window.__tpFit()); await sleep(200); await p.evaluate(() => window.__tpViz(true)); await sleep(700);
  R.s8 = await p.evaluate(() => { const z = window.__tpView().z, out = {}; window.__tpNodes().forEach(n => { const w = document.querySelector('#tp-page .tp-node[data-key="' + n.key + '"]'); const v = w.querySelector('.tp-viz'); if (!v) { out[n.key] = 'no viz'; return; } const a = v.getBoundingClientRect(), b = w.getBoundingClientRect(); out[n.key] = [Math.round((a.left - b.left) / z), Math.round((a.top - b.top) / z), Math.round(a.width / z), Math.round(a.height / z)].join(','); });
    const envTexts = [...document.querySelectorAll('#tp-page .tp-node[data-key="env"] .tp-viz text, #tp-page .tp-node[data-key="env"] .tp-viz .env-read-hud')].map(t => getComputedStyle(t).visibility); const envVisibleText = [...document.querySelectorAll('#tp-page .tp-node[data-key="env"] *')].filter(e => e.children.length === 0 && e.textContent.trim() && getComputedStyle(e).visibility === 'visible').map(e => e.textContent.trim().slice(0, 12));
    return { fit: out, envTexts, envVisibleText, fltCv: (c => c && c.width + 'x' + c.height)(document.querySelector('#tp-page .tp-node[data-key="filter"] .filt-bg canvas')), sampViz: !!document.querySelector('#tp-page .tp-node[data-kind="osc"] .samp-disp.tp-viz') }; });
  await p.screenshot({ path: out + '-viz.png' });
  await p.evaluate(() => { const n = window.__tpNodes().find(n => n.key === 'tape'); if (n) window.__tpSetView(-(n.x - 20), -(n.y - 20), 1); }); await sleep(400); await p.screenshot({ path: out + '-viz-tape.png', clip: { x: 0, y: 40, width: 300, height: 320 } });
  R.s8.tapeViz = await p.evaluate(() => { const c = document.getElementById('tapeMechCanvas'), n = document.querySelector('#tp-page .tp-node[data-key="tape"]'); if (!c || !n) return null; const a = c.getBoundingClientRect(), b = n.getBoundingClientRect(), z = window.__tpView().z; return { css: c.style.width + 'x' + c.style.height, bmp: c.width + 'x' + c.height, box: [Math.round((a.left - b.left) / z), Math.round((a.top - b.top) / z), Math.round(a.width / z), Math.round(a.height / z)].join(',') }; });
  await p.evaluate(() => window.__tpFit()); await sleep(200);
  await p.evaluate(() => window.__tpViz(false)); await sleep(300);
  R.s8.unfit = await p.evaluate(() => [...document.querySelectorAll('#tp-page .tp-viz')].filter(v => v.style.width).length);
  } catch (e) { R["ERR "+'the eye: every picture fills its glass; '] = String(e).slice(0,200); }
  console.error("stage done:", 'the eye: every picture fills its glass; ');

  try {
  // ── the dice: no dead nodes, a compressor last, the canvas tidied
  await p.evaluate(() => window.__tpGenerate()); await sleep(2600);
  R.s9 = await p.evaluate(() => { const devs = window.__fxrDevs().filter(Boolean); const on = ['a', 'b', 'c', 'd'].filter(o => !document.getElementById('osc-' + o + '-device').classList.contains('osc-off')); const keys = window.__tpNodes().map(n => n.key); return { on, oscNodes: keys.filter(k => /^osc-/.test(k)), fx: devs.map(d => d.core + ':' + d.on + ':' + (d.route || []).join('')), fxNodes: keys.filter(k => /^fx-/.test(k)), chain: window.__flowChain(), flowNodes: keys.filter(k => /^flow-/.test(k)), tapeOn: !document.getElementById('tape-toggle').classList.contains('off'), tapeNode: keys.includes('tape'), loopNode: keys.includes('tapeloop'), routes: window.__tiRoutes().length, mono: window.__tpParam('SYN_MONO'), engines: on.map(o => +window.__tpParam('SYN_OSC_' + o.toUpperCase() + '_ENGINE').toFixed(2)) }; });
  await p.screenshot({ path: out + '-dice.png' });
  } catch (e) { R["ERR "+'the dice: no dead nodes, a compressor la'] = String(e).slice(0,200); }
  console.error("stage done:", 'the dice: no dead nodes, a compressor la');

  try {
  // ── close: everything goes home
  await p.evaluate(() => document.getElementById('syn-btn').click()); await sleep(600);
  R.s10 = await p.evaluate(() => ({ nodesLeft: document.querySelectorAll('#tp-page .tp-node').length, modesHome: (m => m && m.parentElement.id + '/' + m.querySelectorAll('.flow-mode').length)(document.getElementById('flow-modes')), visHome: !!document.querySelector('#flow-device .flow-vis .reso-cv'), oscHome: ['a', 'b', 'c', 'd'].map(o => !!document.getElementById('osc-' + o + '-device').closest('.ti-syn-page, #syn-panel') && !document.getElementById('osc-' + o + '-device').closest('.tp-node')), tapeHome: !document.getElementById('fxPageTape').closest('.tp-node'), loopHome: !document.getElementById('tape-loop-content').closest('.tp-node'), cards: [...document.querySelectorAll('.ti-card')].map(c => c.parentElement.tagName + ':' + c.classList.contains('open')), reel: (c => c.style.width + '/' + c.width)(document.getElementById('tape-reel-canvas')), modalOpt: !!document.querySelector('#osc-a-engine-select option[value="6"]'), tpOpen: document.body.classList.contains('tp-open') }));
  } catch (e) { R["ERR close"] = String(e).slice(0,200); }
  R.errs = errs; R.logs = logs.slice(0, 12);
  require('fs').writeFileSync(out + '-result.json', JSON.stringify(R, null, 1)); console.log(JSON.stringify(R)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
