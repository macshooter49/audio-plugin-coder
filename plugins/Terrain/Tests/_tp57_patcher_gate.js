// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp57 — THE PATCHER'S THREE: the toolbar lives UNDER the modules, the map DRAGS, and the hover
//  chip's two names are DOORS.
//    node Tests/_tp57_patcher_gate.js <abs index.html>
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  /* ⚠️ THE WHOLE PLUGIN HAS TO FIT. The page scales itself to the window WIDTH (measured: a 1200 px
     window gives #tp-page a 1200 px screen box over an 820 px client box, a zoom of 1.4634), so a
     820-tall window cuts 140 px off the bottom — and the MINI-MAP lives there. A real pointer aimed
     at it lands outside the document and the map reads dead; the first cut of [2] failed for exactly
     that reason and nothing was wrong with the code. 1200 x 990 clears 656 x 1.4634 = 960. */
  const p = await b.newPage(); await p.setViewport({ width: 1200, height: 990, deviceScaleFactor: 2 });
  const errs = []; p.on('pageerror', e => errs.push(String(e).slice(0, 150)));
  await p.evaluateOnNewDocument(() => { try { localStorage.setItem('tpLayout', JSON.stringify({ v: 6, pos: {}, ext: {} })); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1900);
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark');
    const d = document.getElementById('osc-a-device'); if (d) d.classList.remove('osc-off'); });
  await sleep(500); await p.evaluate(() => window.setActivePanel('tp')); await sleep(2600);

  // ── [0] 🚨 A MODULE PARKED OVER THE TOOLBAR COVERS IT ─────────────────────────────────────
  //  The real question is not "what is the z-index" but "what does the pixel under the toolbar
  //  belong to" — so this drags a module onto the toolbar and asks the document.
  const covered = await p.evaluate(async () => {
    const tools = document.querySelector('#tp-page .tp-tools');
    const eye = tools.querySelector('.g[data-t="viz"]');
    const r = eye.getBoundingClientRect(), cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    const before = document.elementFromPoint(cx, cy);
    // park a module exactly over the eye
    const keys = [...document.querySelectorAll('#tp-page .tp-node')].map(w => w.dataset.key);
    const key = keys.find(k => /^osc-/.test(k)) || keys[0];
    const n = window.__tpNodeByKey(key);
    const v = window.__tpView();
    // world coords that land the node's middle on the eye.  ⚠️ screen px are NOT page px: the page
    // carries its own zoom, so the screen offset has to be divided by it before the view transform.
    const pg = document.getElementById('tp-page'), pr = pg.getBoundingClientRect();
    const zoom = pr.width / pg.clientWidth;
    n.x = Math.round(((cx - pr.left) / zoom - v.x) / v.z - n.w / 2);
    n.y = Math.round(((cy - pr.top) / zoom - v.y) / v.z - n.h / 2);
    n.wrap.style.left = n.x + 'px'; n.wrap.style.top = n.y + 'px';
    await new Promise(r2 => setTimeout(r2, 200));
    const after = document.elementFromPoint(cx, cy);
    return { before: before ? (before.closest('.tp-tools') ? 'toolbar' : (before.closest('.tp-node') ? 'node' : before.className)) : null,
             after:  after  ? (after.closest('.tp-tools')  ? 'toolbar' : (after.closest('.tp-node')  ? 'node' : after.className))  : null,
             toolsZ: getComputedStyle(document.querySelector('#tp-page .tp-tools')).zIndex,
             worldZ: getComputedStyle(document.querySelector('#tp-page .tp-world')).zIndex };
  });
  ok(covered.before === 'toolbar' && covered.after === 'node',
     '[0] 🚨 A MODULE OVER THE TOOLBAR COVERS IT — the emblems live on the dotted ground, never on a card',
     JSON.stringify(covered));

  // ── [1] AND THEY ARE STILL ABOVE THE BACKGROUND ──────────────────────────────────────────
  const aboveGrid = await p.evaluate(() => {
    const tz = +getComputedStyle(document.querySelector('#tp-page .tp-tools')).zIndex;
    const gz = +getComputedStyle(document.querySelector('#tp-page .tp-grid')).zIndex;
    const wz = +getComputedStyle(document.querySelector('#tp-page .tp-world')).zIndex;
    return { tz, gz, wz };
  });
  ok(aboveGrid.gz < aboveGrid.tz && aboveGrid.tz < aboveGrid.wz,
     '[1] GRID < TOOLBAR < MODULES — the one ordering that gives Max what he asked for', JSON.stringify(aboveGrid));

  // ── [2] 🚨 THE MAP DRAGS ─────────────────────────────────────────────────────────────────
  //  Before tp57 the map listened for mousedown and NOTHING else — holding and moving did nothing
  //  at all until you released and clicked again, which is what "laggy" feels like from outside.
  const mini = await p.evaluate(() => { const m = document.querySelector('#tp-page .tp-mini').getBoundingClientRect();
    return { x: m.left + m.width / 2, y: m.top + m.height / 2, w: m.width, h: m.height }; });
  const v0 = await p.evaluate(() => window.__tpView());
  await p.mouse.move(mini.x, mini.y); await p.mouse.down();
  const seen = [];
  for (let k = 1; k <= 5; k++) { await p.mouse.move(mini.x + k * 4, mini.y + k * 2); await sleep(45);
    seen.push(await p.evaluate(() => { const v = window.__tpView(); return [Math.round(v.x), Math.round(v.y)]; })); }
  await p.mouse.up(); await sleep(150);
  const distinct = new Set(seen.map(s => s.join(','))).size;
  ok(distinct >= 4, '[2] 🚨 THE MAP FOLLOWS THE POINTER WHILE HELD — five moves, five different views',
     distinct + ' distinct views across 5 moves: ' + JSON.stringify(seen));

  // ── [3] AND A DRAG FRAME DOES NOT REBUILD THE WHOLE MAP ──────────────────────────────────
  //  drawMini used to throw away every cable and node rect to move one rectangle. The static ink
  //  now lives in its own <g> and only the viewport rect is written per frame.
  const cheap = await p.evaluate(async () => {
    const svg = document.querySelector('#tp-page .tp-mini svg');
    const g = svg.querySelector('g.ms'); if (!g) return { err: 'no static layer' };
    const before = g.firstElementChild;
    const v = window.__tpView();
    const tfs = [];
    for (let i = 0; i < 6; i++) { window.__tpSetView(v.x - i * 40, v.y - i * 25, v.z);
      await new Promise(r => setTimeout(r, 20)); tfs.push(g.getAttribute('transform')); }
    return { kept: g.firstElementChild === before, kids: g.children.length,
             rect: !!svg.querySelector('rect.mv'), moved: new Set(tfs).size > 1 };
  });
  ok(cheap.kept === true && cheap.rect === true && cheap.moved === true,
     '[3] A PAN REUSES THE MAP\'S STATIC INK — the same DOM nodes survive six view changes, only its transform moves',
     JSON.stringify(cheap));

  // ── [4] 🚨 THE HOVER CHIP'S NAMES ARE DOORS ──────────────────────────────────────────────
  const chip = await p.evaluate(async () => {
    const cables = window.__tpDerive() || [];
    const c = cables.find(x => x.t === 'a' && x.from && x.to && x.from.key !== 'midi');
    if (!c) return { err: 'no audio cable', n: cables.length };
    const el = document.querySelector('#tp-page .tp-cables .cable[data-id="' + c.id + '"] .hit');
    if (!el) return { err: 'no hit path for ' + c.id };
    const r = el.getBoundingClientRect();
    el.dispatchEvent(new MouseEvent('pointerenter', { bubbles: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 }));
    await new Promise(x => setTimeout(x, 120));
    const tip = document.getElementById('tp-ctip');
    const gos = tip ? [...tip.querySelectorAll('.go')] : [];
    return { on: !!(tip && tip.classList.contains('on')), names: gos.map(g => g.textContent),
             target: c.to.key, targetName: gos.length === 2 ? gos[1].textContent : null };
  });
  ok(!chip.err && chip.on && chip.names.length === 2,
     '[4] 🚨 THE CHIP SHOWS TWO CLICKABLE NAMES, not one string', JSON.stringify(chip));

  // ── [5] AND CLICKING ONE CENTRES THE CANVAS ON THAT MODULE ───────────────────────────────
  const moved = await p.evaluate(async (key) => {
    /* measured in the PAGE's own coordinate space (clientWidth), which is the space view.x lives in */
    const pg = document.getElementById('tp-page');
    const n = window.__tpNodeByKey(key);
    const off = () => { const v = window.__tpView();
      const sx = v.x + (n.x + n.w / 2) * v.z, sy = v.y + (n.y + n.h / 2) * v.z;
      return Math.hypot(sx - pg.clientWidth / 2, sy - pg.clientHeight / 2); };
    const before = off();
    const tip = document.getElementById('tp-ctip');
    const gos = [...tip.querySelectorAll('.go')];
    gos[1].dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
    await new Promise(r => setTimeout(r, 700));
    return { before: Math.round(before), after: Math.round(off()) };
  }, chip.target);
  ok(moved.after <= 3 && moved.before > 10,
     '[5] 🚨 CLICKING A NAME GLIDES THAT MODULE TO THE MIDDLE — "boom, it moves the map over to it"',
     'centre offset ' + moved.before + ' px → ' + moved.after + ' px');

  ok(errs.length === 0, '[6] THE PAGE THREW NOTHING', errs.join(' | '));
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})();
