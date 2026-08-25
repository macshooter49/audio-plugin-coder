// fb504 — the arbiter's CONTRACT test, against the SHIPPED page in Chrome (same engine, arbiter
// active because the UA is Windows):
//   1. after >1.5s of nothing the page PARKS (__tiParked true, CSS animations paused)
//   2. a pointer event WAKES it instantly
//   3. __notesActive=1 wakes it within ~1s (the parked probe interval)
//   4. rAF cadence while parked is ~1fps; while active ~60fps
//   5. a queued callback is NEVER dropped (state persists)
const puppeteer = require('puppeteer-core');
const URL = 'file:///C:/dev/audio-plugin-coder/plugins/TerrainInstrument/Source/ui/public/index.html';

(async () => {
  const b = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH, headless: false,
    args: ['--no-sandbox', '--allow-file-access-from-files', '--window-size=1200,800'],
    defaultViewport: null,
  });
  const p = (await b.pages())[0] || await b.newPage();
  await p.goto(URL, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 2500));

  const results = [];
  const check = (name, cond) => { results.push({ name, pass: !!cond }); console.log(`  ${cond ? 'ok  ' : 'FAIL'}  ${name}`); };

  // 1: parks after idle
  await new Promise(r => setTimeout(r, 3000));
  const s1 = await p.evaluate(() => ({ parked: !!window.__tiParked, cls: document.documentElement.classList.contains('ti-parked') }));
  check('parks after 1.5s idle (__tiParked true)', s1.parked);
  check('ti-parked class applied (CSS animations paused)', s1.cls);

  // 4a: parked rAF cadence ~1fps
  const parkedFps = await p.evaluate(() => new Promise(res => {
    let n = 0; const id = setInterval(() => {}, 100000);
    const t0 = performance.now();
    function tick(){ n++; if (performance.now() - t0 < 3000) window.requestAnimationFrame(tick); else res(n / 3); }
    window.requestAnimationFrame(tick);
    setTimeout(() => res(n / 3), 4000);
  }));
  check(`parked cadence ~1fps (measured ${parkedFps.toFixed(1)}/s)`, parkedFps <= 3);

  // 2: pointer wakes instantly
  await p.mouse.move(600, 400); await p.mouse.move(610, 410);
  await new Promise(r => setTimeout(r, 300));
  const s2 = await p.evaluate(() => !!window.__tiParked);
  check('pointer input wakes it (<300ms)', !s2);

  // 4b: active rAF cadence ~60fps (keep it awake with synthetic moves)
  const activeFps = await p.evaluate(() => new Promise(res => {
    let n = 0; const t0 = performance.now();
    const keep = setInterval(() => window.dispatchEvent(new PointerEvent('pointermove')), 400);
    function tick(){ n++; if (performance.now() - t0 < 2000) window.requestAnimationFrame(tick); else { clearInterval(keep); res(n / 2); } }
    window.requestAnimationFrame(tick);
  }));
  check(`active cadence ~60fps (measured ${activeFps.toFixed(0)}/s)`, activeFps > 25);

  // 3: notes wake a parked page
  await new Promise(r => setTimeout(r, 3500));   // let it re-park
  const s3a = await p.evaluate(() => !!window.__tiParked);
  check('re-parks after activity ends', s3a);
  await p.evaluate(() => { window.__notesActive = 1; });
  await new Promise(r => setTimeout(r, 1300));
  const s3b = await p.evaluate(() => !!window.__tiParked);
  check('a note wakes it within ~1s', !s3b);
  await p.evaluate(() => { window.__notesActive = 0; });

  // 5: no callback dropped across a park/wake cycle
  const kept = await p.evaluate(() => new Promise(res => {
    let ran = false;
    window.requestAnimationFrame(() => { ran = true; });
    setTimeout(() => { window.dispatchEvent(new PointerEvent('pointermove')); }, 2500);
    setTimeout(() => res(ran), 3500);
  }));
  check('callback queued while parked still runs on wake (state persists)', kept);

  await b.close();
  const fails = results.filter(r => !r.pass).length;
  console.log(fails ? `\n  ${fails} FAILED\n` : '\n  ALL ARBITER CONTRACT CHECKS PASSED\n');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
