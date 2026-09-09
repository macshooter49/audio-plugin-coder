// ══ fb610 — DOES A NEWLY PICKED TABLE ACTUALLY REDRAW? ════════════════════════════════════════
//   node Tests/wt_stale_gate.js [path/to/index.html]
//   STALE_MUT=noclear   fb611 — the wait never clears when the table lands → bar [6] reds
//   STALE_MUT=nostamp   the pre-fb610 signature, consistent on BOTH sides → bar [2] reds
//   STALE_MUT=notick    the stamp on only one side → the picture re-bakes for ever → bar [1] reds
//
// Max: "the tables take a FEW SECONDS. Serum's take NONE. WE NEED IT TO BE INSTANT."
// It was never the bake — the shipping Wavetable.h bakes a real 128-frame factory table in 32.3 ms
// (Tests/wt_bake_bench.cpp). It was a RACE WITH NO RETRY:
//   loadWavetableByPath queues the build on a pool and returns IMMEDIATELY  →  the JS .then()
//   fetches the waterfall while the build is still running and gets the OLD table  →  maybeRebake
//   then compares a signature made of warp/fold/spectral/FM/harm, none of which changed, so the
//   stale picture is declared fresh and never asked for again.
// It corrected only when some unrelated value drifted, which is why it was "a few seconds" and
// never the same length twice.
//
// This drives the SHIPPING wtWaterfall.maybeRebake and counts fetches. The bar that matters is
// [2]: a payload identical in every way EXCEPT which table it is must trigger a re-bake.
const puppeteer = require('puppeteer-core');
const PAGE = process.argv[2] || require('path').join(__dirname, '..') + '/Source/ui/public/index.html';
const MUT  = process.env.STALE_MUT || '';

let PASS = 0, FAIL = 0;
const gate = (ok, name, detail) => {
  if (ok) { PASS++; console.log('  ✓ ' + name + (detail ? '   ' + detail : '')); }
  else    { FAIL++; console.log('  ✗ ' + name + (detail ? '   ' + detail : '')); }
};

(async () => {
  const b = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage(); await p.setViewport({ width: 1200, height: 820 });
  const errs = []; p.on('pageerror', e => errs.push(String(e).slice(0, 140)));
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1400));

  if (MUT === 'noclear')   // the fb611 wait state gets set and never taken off again
    await p.evaluate(() => { const f = window.onWavetableImported;
      window.onWavetableImported = function (o, n) { const d = document.getElementById('osc-' + o + '-preset-display');
        const op = d && d.style.opacity, ti = d && d.title; f.call(window, o, n);
        if (d) { d.style.opacity = op; d.title = ti; } }; });

  const R = await p.evaluate((MUT) => {
    const W = window.wtWaterfall;
    if (!W) return { ERROR: 'wtWaterfall is not on the page' };

    // the 14 values __wtDisp pushes, in PluginEditor.cpp's order; [13] is the table stamp
    let disp = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 111111];
    const payload = () => ({ n: 4, p: 8, nf: 4, sc: 8192, ms: 0.4,
      wm: disp[0], wa: disp[1], w2m: disp[2], w2a: disp[3], fs: disp[4], fa: disp[5],
      sa: disp[6], st: disp[7], bl: disp[8], lo: disp[9], hi: disp[10], fm: disp[11],
      hm: disp[12], tg: disp[13], d: new Array(32).fill(0) });

    let fetches = 0;
    W.fetch = function (o) { fetches++; W.cache[o] = payload(); W.busy[o] = false; };   // stand in for the native round trip
    window.__wtDisp = [disp.slice(), disp.slice(), disp.slice(), disp.slice()];

    /* ⚠️ `nostamp` MUST STRIP BOTH SIDES, or it tests the wrong failure. Take the stamp off only
       the cached side and the two signatures can never agree, so the picture re-bakes at the
       MAXIMUM rate for ever — a real bug, and the one `notick` covers below, but the opposite of
       what shipped. The pre-fb610 code was perfectly consistent at thirteen fields; that is the
       state to reproduce, and in it bar [2] is the only thing that can notice. */
    if (MUT === 'nostamp') {                      // the pre-fb610 signature, consistent on both sides
      const cut = f => function (o) { return f.call(W, o).split(',').slice(0, 13).join(','); };
      W.cachedSig = cut(W.cachedSig); W.shapeSig = cut(W.shapeSig);
    }
    if (MUT === 'notick')                         // only the live feed loses it — signatures never agree
      W.shapeSig = function (o) { const d = window.__wtDisp[{a:0,b:1,c:2,d:3}[o]];
        return d ? d.slice(0, 13).map(v => (+v).toFixed(3)).join(',') : ''; };

    const sync = () => { window.__wtDisp = [disp.slice(), disp.slice(), disp.slice(), disp.slice()]; };
    let t = 1000;
    const step = () => { t += 500; W.maybeRebake('a', t); };   // well past the 16 ms throttle

    W.cache.a = payload(); W.busy.a = false; W.lastReq.a = 0;   // a picture already on screen
    const out = {};

    fetches = 0; step(); out.idle = fetches;                    // nothing moved

    // THE EVENT: the async bake published a different table. Every other value is identical —
    // this is exactly what a wavetable pick produces.
    disp[13] = 222222; sync(); fetches = 0; step(); out.newTable = fetches;

    // and a knob move still works (this never broke)
    W.cache.a = payload(); disp[1] = 0.5; sync(); fetches = 0; step(); out.knob = fetches;

    out.sigHasStamp = /222222|111111/.test(W.cachedSig('a') + '|' + W.shapeSig('a'));
    return out;
  }, MUT);

  console.log('══ fb610 STALE WAVETABLE PICTURE ══   mutation: ' + (MUT || '(none)'));
  if (R.ERROR) { console.log('  ✗ ' + R.ERROR); await b.close(); process.exit(1); }

  gate(R.idle === 0, '[1] AN UNCHANGED TABLE DOES NOT RE-BAKE (the throttle still holds)',
       'fetches = ' + R.idle);
  gate(R.newTable === 1,
       '[2] A NEW TABLE, EVERYTHING ELSE IDENTICAL, RE-BAKES AT ONCE',
       'fetches = ' + R.newTable + (R.newTable ? '' : '   *** the picture stays on the OLD table — this IS the bug ***'));
  gate(R.knob === 1, '[3] AND A KNOB MOVE STILL RE-BAKES AS IT ALWAYS DID', 'fetches = ' + R.knob);
  gate(R.sigHasStamp === true, '[4] THE TABLE STAMP REALLY REACHES THE SIGNATURE',
       R.sigHasStamp ? 'present on both sides' : '*** neither signature carries it ***');
  // ── fb611 — and the WAIT itself is visible while a cloud-evicted file downloads ─────────────
  const fetchStates = await p.evaluate(() => {
    const d = document.getElementById('osc-a-preset-display');
    if (!d || !window.onWavetableFetching) return { ERROR: 'no display or no onWavetableFetching' };
    const R = {};
    window.onWavetableFetching('a', 'TERRA CHIRIKOV', true);            // still in iCloud
    R.cloudName = d.textContent; R.cloudDim = d.style.opacity; R.cloudTitle = /iCloud/.test(d.title || '');
    window.onWavetableImported('a', 'TERRA CHIRIKOV');                  // it landed
    R.clearedDim = d.style.opacity; R.clearedTitle = d.title;
    window.onWavetableFetching('a', 'TERRA CANTOR', false);             // a LOCAL file — 3.8 ms
    R.localName = d.textContent; R.localDim = d.style.opacity;
    window.onWavetableImported('a', 'TERRA CANTOR');
    return R;
  });
  gate(!fetchStates.ERROR && fetchStates.cloudName === 'TERRA CHIRIKOV'
       && fetchStates.cloudDim === '0.5' && fetchStates.cloudTitle === true,
       '[5] A CLOUD-EVICTED FILE NAMES ITSELF AT ONCE AND SHOWS IT IS WAITING',
       fetchStates.ERROR || ('name "' + fetchStates.cloudName + '"  opacity ' + fetchStates.cloudDim
         + '  says-iCloud ' + fetchStates.cloudTitle));
  gate(fetchStates.clearedDim === '' && fetchStates.clearedTitle === '',
       '[6] AND THE WAIT CLEARS WHEN THE TABLE LANDS',
       'opacity "' + fetchStates.clearedDim + '"  title "' + fetchStates.clearedTitle + '"');
  gate(fetchStates.localName === 'TERRA CANTOR' && fetchStates.localDim === '',
       '[7] A LOCAL FILE NAMES ITSELF WITHOUT DIMMING (3.8 ms would only flicker)',
       'name "' + fetchStates.localName + '"  opacity "' + fetchStates.localDim + '"');

  gate(errs.length === 0, '[8] NO PAGE ERRORS', errs.length ? errs.slice(0, 2).join(' | ') : 'clean');

  console.log('\n  ' + PASS + ' pass, ' + FAIL + ' fail' + (MUT ? '   (mutation: ' + MUT + ')' : ''));
  await b.close();
  process.exit(FAIL ? 1 : 0);
})();
