// tp11 — Max's 2026-09-15 afternoon list, headless: fonts · chip · panes (card first opened ON the canvas) · padding · Randomizer seat/cable/roll/menu ·
// port + cable unpatch · card drags from blank space · tape machine dests · the eye (blink, unclipped, screen)
const puppeteer = require('puppeteer-core');
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errs = []; p.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: { 'flow-glitch': 1 } })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.getElementById('osc-a-device').classList.remove('osc-off');
    try { window.__fxrAdd('reverb'); window.__fxrAdd('cmp'); window.__flowSetChain(['glitch']); } catch (e) {} const t = document.getElementById('tape-toggle'); if (t && t.classList.contains('off')) t.click(); });
  await sleep(300); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2000); await p.evaluate(() => window.__tpFit()); await sleep(400);
  const R = {}; const box = sel => p.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; }, sel);
  const N = k => p.evaluate(k => window.__tpNodes().find(n => n.key === k) || null, k);
  R.nodes = await p.evaluate(() => window.__tpNodes().map(n => n.key + '@' + n.x + ',' + n.y + ' ' + n.w + 'x' + n.h));
  // panes: the card opened ON the canvas — every tab, the same card height and the same port
  await p.evaluate(() => window.__tpExt('flow-glitch', true)); await sleep(900);
  R.panes = await p.evaluate(async () => { const c = document.querySelector('#tp-page .ti-card.gli-ext'); if (!c) return 'no ext card'; const w = c.closest('.tp-node'); const out = [];
    for (const t of c.querySelectorAll('.tabs .tab')) { t.click(); await new Promise(r => setTimeout(r, 80)); const pt = w.querySelector('.tp-port.k-in'); out.push([t.dataset.p, c.offsetHeight, w.offsetHeight, pt ? pt.style.top : '']); } return out; });
  // fonts + chip
  R.font = await p.evaluate(async () => { const pg = document.getElementById('tp-page'), r = pg.getBoundingClientRect(); pg.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: r.left + 40, clientY: r.bottom - 40 })); await new Promise(r => setTimeout(r, 150));
    const e = document.querySelector('#tp-page .tp-add .cats .row'); const s = e ? getComputedStyle(e) : null; document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' })); return s ? s.fontWeight + ' ' + s.fontSize + ' ' + s.letterSpacing : 'none'; });
  await p.keyboard.press('Escape');
  R.chip = await p.evaluate(() => { const c = document.querySelector('#tp-page .ti-card.gli-ext .chip.cur') || document.querySelector('#tp-page .ti-card.gli-ext .chip'); if (!c) return null; const s = getComputedStyle(c); return { border: s.borderColor, num: s.fontVariantNumeric, color: s.color }; });
  // padding: the osc node is the device + 24, its dots 17 px under the device
  R.pad = await p.evaluate(() => { const w = document.querySelector('#tp-page .tp-node[data-key="osc-a"]'), d = w.querySelector('.device'), m = w.querySelector('.tp-port.k-mod'); const wr = w.getBoundingClientRect(), dr = d.getBoundingClientRect(), z = window.__tpView().z; return { wrapH: Math.round(wr.height / z), devH: Math.round(dr.height / z), gapBelowDevice: Math.round((wr.bottom - dr.bottom) / z), modPorts: w.querySelectorAll('.tp-port.k-mod').length, topPad: Math.round((dr.top - wr.top) / z) }; });
  // Randomizer seat: right-aligned under Out, bottom right, overlapping nothing
  R.rnd = await p.evaluate(() => { const ns = window.__tpNodes(), r = ns.find(n => n.key === 'rnd'), o = ns.find(n => n.key === 'out'); const hit = ns.filter(n => n.key !== 'rnd' && !(n.x + n.w <= r.x || r.x + r.w <= n.x || n.y + n.h <= r.y || r.y + r.h <= n.y)).map(n => n.key);
    return { rnd: [r.x, r.y, r.w, r.h], out: [o.x, o.y, o.w, o.h], rightAligned: r.x + r.w === o.x + o.w, below: r.y > o.y + o.h, overlaps: hit, ports: document.querySelectorAll('#tp-page .tp-node[data-key="rnd"] .tp-port').length }; });
  // a Randomizer cable: rnd's port → osc A's top port
  await p.evaluate(() => window.__tpFit()); await sleep(300);
  let a = await box('#tp-page .tp-node[data-key="rnd"] .tp-port.k-out'), t = await box('#tp-page .tp-node[data-key="osc-a"] .tp-port.k-rin');
  await p.mouse.move(a.x + a.w / 2, a.y + a.h / 2); await p.mouse.down(); await p.mouse.move(t.x + t.w / 2, t.y + t.h / 2, { steps: 12 }); await p.mouse.up(); await sleep(300);
  R.rlinks = await p.evaluate(() => window.__tpLayout().rlinks); R.rCable = await p.evaluate(() => window.__tpCables().filter(c => /rnd\.out0>osc-a\.rin0/.test(c)));
  // roll it: the middle of the map
  const P0 = await p.evaluate(() => ['SYN_OSC_A_WARP_AMOUNT', 'SYN_OSC_A_WT_PRESET', 'SYN_OSC_A_UDETUNE', 'SYN_OSC_A_OCT'].map(id => window.__tpParam(id)));
  const rb = await box('#tp-page .tp-node[data-key="rnd"]'); await p.mouse.click(rb.x + rb.w / 2, rb.y + rb.h / 2); await sleep(900);
  const P1 = await p.evaluate(() => ['SYN_OSC_A_WARP_AMOUNT', 'SYN_OSC_A_WT_PRESET', 'SYN_OSC_A_UDETUNE', 'SYN_OSC_A_OCT'].map(id => window.__tpParam(id)));
  R.roll = { warpChanged: P0[0] !== P1[0], presetChanged: P0[1] !== P1[1], detuneKept: P0[2] === P1[2], octKept: P0[3] === P1[3], toast: await p.evaluate(() => document.querySelector('#tp-page .tp-toast').textContent) };
  // its right-click = the reach, no captions
  await p.mouse.click(rb.x + rb.w / 2, rb.y + rb.h / 2, { button: 'right' }); await sleep(200);
  R.rndMenu = await p.evaluate(() => { const m = document.querySelector('#tp-page .tp-nmenu.on'); return m ? { items: [...m.querySelectorAll('.pi')].map(x => x.textContent).join(','), caps: m.querySelectorAll('.ps').length } : null; });
  await p.keyboard.press('Escape'); await sleep(100);
  // a port unpatches on right-click: LFO 1 → Osc A WT Pos, then right-click that dot
  await p.evaluate(() => { window.__tiAddRoute(0, 1, 2); window.__tpSync(); }); await sleep(300);
  const hasR = () => p.evaluate(() => (window.__tiRoutes() || []).some(r => r.s === 'lfo1' && r.d === 2));
  R.routeMade = await hasR(); const mp = await box('#tp-page .tp-node[data-key="osc-a"] .tp-port.k-mod[data-dest="2"]');
  if (mp) { await p.mouse.click(mp.x + mp.w / 2, mp.y + mp.h / 2, { button: 'right' }); await sleep(300); } R.portRightClickUnpatched = mp ? !(await hasR()) : 'no port';
  // a cable click selects it (no browser), ⌫ cuts it
  await p.evaluate(() => { window.__tiAddRoute(0, 1, 2); window.__tpSync(); }); await sleep(300);
  const cp = await p.evaluate(() => { const g = [...document.querySelectorAll('#tp-page .tp-cables .cable')].find(g => /osc-a\.mod/.test(g.dataset.id)); if (!g) return null; const h = g.querySelector('.hit'), L = h.getTotalLength(), m = h.getScreenCTM(); for (let f = 0.1; f < 0.95; f += 0.05) { const q = h.getPointAtLength(L * f), x = q.x * m.a + m.e, y = q.y * m.d + m.f, e = document.elementFromPoint(x, y); if (e === h) return { x, y }; } return null; });
  if (cp) { await p.mouse.click(cp.x, cp.y); await sleep(150); R.cableClickOpensBrowser = await p.evaluate(() => !!document.querySelector('#tp-page .tp-add.on')); R.cableSelected = await p.evaluate(() => !!document.querySelector('#tp-page .tp-cables .cable.sel')); await p.keyboard.press('Backspace'); await sleep(300); R.cableDeleted = !(await hasR()); }
  // a card moves from blank space; a knob does not move it
  await p.evaluate(() => window.__tpFit()); await sleep(200);
  const fk = await p.evaluate(() => window.__tpNodes().find(n => /^fx-reverb/.test(n.key)).key);
  const core = await box('#tp-page .tp-node[data-key="' + fk + '"] .fxr-core'); let n0 = await N(fk);
  await p.mouse.move(core.x + core.w / 2, core.y + core.h / 2); await p.mouse.down(); await p.mouse.move(core.x + core.w / 2 + 30, core.y + core.h / 2 + 12, { steps: 6 }); await p.mouse.up(); await sleep(150);
  let n1 = await N(fk); R.cardBlankDrags = n1.x !== n0.x;
  const dial = await box('#tp-page .tp-node[data-key="' + fk + '"] .fxr-dial'); n0 = await N(fk);
  await p.mouse.move(dial.x + dial.w / 2, dial.y + dial.h / 2); await p.mouse.down(); await p.mouse.move(dial.x + dial.w / 2 + 20, dial.y + dial.h / 2 - 20, { steps: 6 }); await p.mouse.up(); await sleep(150);
  n1 = await N(fk); R.knobDoesNotDrag = n1.x === n0.x;
  // the tape machine's knobs are dests with dots
  R.tape = await p.evaluate(() => { const w = document.querySelector('#tp-page .tp-node[data-key="tape"]'); if (!w) return 'no tape node'; return { dests: [...w.querySelectorAll('.fx-knob-group')].map(g => g.getAttribute('data-mod-dest')), dots: w.querySelectorAll('.tp-port.k-mod').length, name: window.__destShortName ? window.__destShortName(1887) : '' }; });
  await p.evaluate(() => { window.__tiAddRoute(0, 1, 1888); window.__tpSync(); }); await sleep(300);
  R.tapeCable = await p.evaluate(() => window.__tpCables().filter(c => /tape\.mod/.test(c)));
  // the eye: it blinks, the LFO's scope is not clipped, the glitch card shows its screen
  await p.evaluate(() => window.__tpViz(true)); await sleep(60); R.blink = await p.evaluate(() => document.getElementById('tp-page').classList.contains('vfade')); await sleep(700);
  R.eye = await p.evaluate(() => { const w = document.querySelector('#tp-page .tp-node[data-key="lfo"]'), s = w.querySelector('.mv-scope'); const wr = w.getBoundingClientRect(), sr = s.getBoundingClientRect(); let clip = null, e = s.parentElement; while (e && e !== w) { if (getComputedStyle(e).overflow !== 'visible') { clip = (e.className || e.id || e.tagName).toString().slice(0, 30); break; } e = e.parentElement; }
    const g = document.querySelector('#tp-page .tp-node[data-key="flow-glitch"] .tp-viz'); return { lfoInside: sr.left >= wr.left - 1 && sr.right <= wr.right + 1 && sr.top >= wr.top - 1 && sr.bottom <= wr.bottom + 1, lfoW: Math.round(sr.width), clippedBy: clip, glitchViz: g ? (g.className || '').toString().slice(0, 30) : null, vfade: document.getElementById('tp-page').classList.contains('vfade') }; });
  await p.screenshot({ path: process.argv[3] + '/tp11-eye.png' });
  await p.evaluate(() => window.__tpViz(false)); await sleep(700);
  R.unclipRestored = await p.evaluate(() => [...document.querySelectorAll('#tp-page .tp-node *')].filter(e => e.style && e.style.overflow === 'visible').length);
  await p.screenshot({ path: process.argv[3] + '/tp11-canvas.png' });
  R.errs = errs; console.log(JSON.stringify(R, null, 1)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
