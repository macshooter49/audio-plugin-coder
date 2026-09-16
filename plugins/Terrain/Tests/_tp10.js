const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off'); try { window.__fxrAdd('reverb'); window.__flowSetChain(['glitch']); } catch (e) {} document.getElementById('syn-btn').click(); });
  await sleep(1800); await p.evaluate(() => window.__tpFit()); await sleep(300);
  const R = { recorded: await p.evaluate(() => (window.__fxrRackL || []).length) };
  const box = sel => p.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; }, sel);
  const pos = k => p.evaluate(k => { const n = window.__tpNodes().find(n => n.key === k); return n ? [n.x, n.y] : null; }, k);
  async function drag(x, y) { await p.mouse.move(x, y); await p.mouse.down(); await p.mouse.move(x + 25, y + 15, { steps: 5 }); await p.mouse.up(); await sleep(120); }
  // LFO: scope = the LFO's, edge = the node's
  let lb = await box('#tp-page .tp-node[data-key="lfo"]'); let p0 = await pos('lfo'); await drag(lb.x + lb.w / 2, lb.y + lb.h * 0.35); let p1 = await pos('lfo');
  R.lfoScopeMovesNode = p1[0] !== p0[0]; lb = await box('#tp-page .tp-node[data-key="lfo"]'); p0 = await pos('lfo'); await drag(lb.x + lb.w / 2, lb.y + 3); p1 = await pos('lfo'); R.lfoEdgeMovesNode = p1[0] !== p0[0];
  // FX: a pill toggles, the back panel swaps, the header drags
  const fxKey = await p.evaluate(() => window.__tpNodes().find(n => /^fx-reverb/.test(n.key)).key);
  const pill = await box('#tp-page .tp-node[data-key="' + fxKey + '"] .fxr-pill');
  const pillBefore = await p.evaluate(k => document.querySelector('#tp-page .tp-node[data-key="' + k + '"] .fxr-pill').className, fxKey);
  await p.mouse.click(pill.x + pill.w / 2, pill.y + pill.h / 2); await sleep(200);
  R.pillToggled = pillBefore !== await p.evaluate(k => { const e = document.querySelector('#tp-page .tp-node[data-key="' + k + '"] .fxr-pill'); return e ? e.className : 'gone'; }, fxKey);
  const sw = await box('#tp-page .tp-node[data-key="' + fxKey + '"] .fxr-swap');
  if (sw) { await p.mouse.click(sw.x + sw.w / 2, sw.y + sw.h / 2); await sleep(300); R.backPanel = await p.evaluate(k => { const c = document.querySelector('#tp-page .tp-node[data-key="' + k + '"] .fxr-dev'); return c ? c.classList.contains('swapped') : 'no card'; }, fxKey); }
  const hd = await box('#tp-page .tp-node[data-key="' + fxKey + '"] .fxr-grip'); p0 = await pos(fxKey); await drag(hd.x + hd.w / 2, hd.y + hd.h / 2); p1 = await pos(fxKey); R.fxHeaderDrags = p1[0] !== p0[0];
  // right-click: no menu on a module; a flow tile opens in place
  lb = await box('#tp-page .tp-node[data-key="lfo"]'); await p.mouse.click(lb.x + lb.w / 2, lb.y + lb.h / 2, { button: 'right' }); await sleep(150);
  R.lfoRightMenu = await p.evaluate(() => !!document.querySelector('#tp-page .tp-nmenu.on'));
  const tl = await box('#tp-page .tp-node[data-key="flow-glitch"]'); await p.mouse.click(tl.x + tl.w / 2, tl.y + tl.h / 2, { button: 'right' }); await sleep(400);
  R.flowRight = await p.evaluate(() => ({ ext: !!(window.__tpNodes().find(n => n.key === 'flow-glitch') || {}).ext, menu: !!document.querySelector('#tp-page .tp-nmenu.on'), floatingCard: [...document.querySelectorAll('.ti-card.open')].filter(c => !c.closest('.tp-node')).length }));
  // a cable dropped on nothing
  await p.evaluate(() => window.__tpFit()); await sleep(200);
  const port = await box('#tp-page .tp-node[data-key="lfo"] .tp-port.k-out'); await p.mouse.move(port.x + 9, port.y + 9); await p.mouse.down(); await p.mouse.move(40, 600, { steps: 8 }); await p.mouse.up(); await sleep(200);
  R.dropOnNothingMenu = await p.evaluate(() => !!document.querySelector('#tp-page .tp-add.on'));
  // the dice menu
  const dice = await box('#tp-page .tp-tools .g[data-t="dice"]'); await p.mouse.click(dice.x + 6, dice.y + 6, { button: 'right' }); await sleep(150);
  R.diceMenu = await p.evaluate(() => { const m = document.querySelector('#tp-page .tp-nmenu.on'); return m ? { caps: m.querySelectorAll('.ps').length, items: [...m.querySelectorAll('.pi')].map(x => x.textContent).join(','), seps: m.querySelectorAll('.sepd').length } : null; });
  await p.mouse.click(400, 620, { button: 'right' }); await sleep(150); R.menusOpen = await p.evaluate(() => document.querySelectorAll('#tp-page .tp-menu.on').length);
  // the eye: scaled, not resized
  await p.keyboard.press('Escape'); await p.evaluate(() => window.__tpViz(true)); await sleep(500);
  R.viz = await p.evaluate(() => [...document.querySelectorAll('#tp-page .tp-viz')].map(v => ({ c: String(v.className).split(' ')[0], vz: (v.style.getPropertyValue('--vz') || '').slice(0, 44), w: v.style.width })).filter(v => v.c !== 'tp-end'));
  R.glass = await p.evaluate(() => getComputedStyle(document.querySelector('#tp-page .tp-node > .tp-box')).backgroundImage.slice(0, 60));
  R.errs = errs; console.log(JSON.stringify(R, null, 0)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
