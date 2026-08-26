// fb508 — the arbiter's CONTRACT, v2 (the no-freeze design), tested against the SHIPPED page in
// Chrome (same engine; the arbiter is active because the UA is Windows):
//   1. the page NEVER visibly parks — rAF keeps running through idle (no freeze-frames)
//   2. cadence is PACED: ~60 fps regardless of panel rate (this rig's panel is 360 Hz)
//   3. __tiQuiet goes true after ~1.5 s of no input/notes (gates INVISIBLE work: native polls,
//      the C++ silence gate's JS mirrors) — and false again immediately on input or notes
//   4. a queued callback always runs (nothing is ever dropped)
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

  // 1+3: idle -> quiet flips, but NOTHING parks
  await new Promise(r => setTimeout(r, 3500));
  const s1 = await p.evaluate(() => ({ parked: !!window.__tiParked, quiet: !!window.__tiQuiet }));
  check('never parks at idle (no freeze-frames)', !s1.parked);
  check('__tiQuiet true after 1.5s idle (invisible work gated)', s1.quiet);

  // 2: cadence at idle is STILL ~60 (animations keep running), paced not panel-rate
  const idleFps = await p.evaluate(() => new Promise(res => {
    let n = 0; const t0 = performance.now();
    function tick(){ n++; if (performance.now() - t0 < 2000) window.requestAnimationFrame(tick); else res(n / 2); }
    window.requestAnimationFrame(tick);
  }));
  check(`idle cadence is paced ~60fps, not panel rate (measured ${idleFps.toFixed(0)}/s)`, idleFps > 30 && idleFps < 75);

  // 3b: input clears quiet fast
  await p.mouse.move(600, 400); await p.mouse.move(610, 410);
  await new Promise(r => setTimeout(r, 700));
  const s2 = await p.evaluate(() => !!window.__tiQuiet);
  check('input clears __tiQuiet (<700ms)', !s2);

  // 3c: notes clear quiet from a quiet state
  await new Promise(r => setTimeout(r, 3500));
  const s3a = await p.evaluate(() => !!window.__tiQuiet);
  check('re-quiets after activity ends', s3a);
  await p.evaluate(() => { window.__notesActive = 1; });
  await new Promise(r => setTimeout(r, 800));
  const s3b = await p.evaluate(() => !!window.__tiQuiet);
  check('a note clears __tiQuiet (<800ms)', !s3b);
  await p.evaluate(() => { window.__notesActive = 0; });

  // fb512: notes sounding + hands-off >2s -> the page paces to ~30 (NOT 60, and NOT the 15 the
  // fb510 hero / fb487 topo / fb492 analyzer alternators would compound to). A single note-on sets
  // lastAct via the __notesActive setter (fb505 instant-wake), so we wait past the 2s hands-off
  // window before measuring. This check directly guards the accumulator fix (the first fb512 draft
  // advanced lastFrame by a hardcoded 1/60, so 30 silently stayed ~60 — this would read ~60 and FAIL).
  await p.evaluate(() => { window.__notesActive = 1; });
  await new Promise(r => setTimeout(r, 2300));
  const playFps = await p.evaluate(() => new Promise(res => {
    let n = 0; const t0 = performance.now();
    function tick(){ n++; if (performance.now() - t0 < 2000) window.requestAnimationFrame(tick); else res(n / 2); }
    window.requestAnimationFrame(tick);
  }));
  check(`hands-off playback paces to ~30fps, not 60 (measured ${playFps.toFixed(0)}/s)`, playFps > 22 && playFps < 42);
  await p.evaluate(() => { window.__notesActive = 0; });

  // 4: callbacks never dropped
  const kept = await p.evaluate(() => new Promise(res => {
    let ran = false;
    window.requestAnimationFrame(() => { ran = true; });
    setTimeout(() => res(ran), 1500);
  }));
  check('a queued callback always runs (state persists)', kept);

  await b.close();
  const fails = results.filter(r => !r.pass).length;
  console.log(fails ? `\n  ${fails} FAILED\n` : '\n  ALL ARBITER CONTRACT CHECKS PASSED\n');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
