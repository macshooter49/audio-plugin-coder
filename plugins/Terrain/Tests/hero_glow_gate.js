// ══ fb613 — THE UI MUST NOT GET SLOWER WHEN THE AUDIO GETS LOUDER ════════════════════════════
//   node Tests/hero_glow_gate.js [index.html]        GLOW_MUT=nosprite   (must go RED)
//
// Max: "sometimes when the plugin gets loud, UI starts to drop in frames/slow down, CPU however
// stays solid no matter what… the plugin likes to slow down visually when things GET LOUD."
//
// He was right, and the cause was the hero terrain's peak glow. The height map is a LINEAR
// function of the live sample, so a louder waveform lifts more of the 1024 grid cells over the
// 0.30 threshold AND lifts them higher — count and area both grow with level and they multiply.
// Measured before the fix: 34.9 glows/frame at silence, 404.3 on a clipped square, i.e. 24,258
// createRadialGradient calls a second and 169% of the canvas repainted in gradient fills. None of
// it on the audio thread — it burns in the WebView's renderer process, which the plugin's CPU
// meter does not watch. Hence "CPU stays solid".
//
// ⚠️ THE OBVIOUS DIAGNOSIS WAS WRONG AND WAS MEASURED WRONG-ON-PURPOSE FIRST: unclamped
// coordinates blowing up the rasteriser's bounding box. masterSoftClip caps the scope at 0.96605,
// worst-case sy is 28-32px on a 276px canvas, and 0 of 404 discs land off-canvas. There was
// nothing to clamp; that fix would have changed nothing.
//
// THE BAR THAT MATTERS IS [1]: the glow count must be BOUNDED, and it must be demonstrably
// CLAMPED when loud — a bound nothing ever reaches would prove nothing.
const puppeteer = require('puppeteer-core');
const PAGE = process.argv[2] || require('path').join(__dirname, '..') + '/Source/ui/public/index.html';
const MUT  = process.env.GLOW_MUT || '';
const GLOW_MAX = 96;

let PASS = 0, FAIL = 0;
const gate = (ok, name, detail) => {
  if (ok) { PASS++; console.log('  ✓ ' + name + (detail ? '   ' + detail : '')); }
  else    { FAIL++; console.log('  ✗ ' + name + (detail ? '   ' + detail : '')); }
};

(async () => {
  const b = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage(); await p.setViewport({ width: 1280, height: 900 });
  const errs = []; p.on('pageerror', e => errs.push(String(e).slice(0, 140)));
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1500));

  const R = await p.evaluate((MUT) => {
    if (typeof renderTerrain !== 'function') return { ERROR: 'renderTerrain is not on the page' };
    const C = CanvasRenderingContext2D.prototype;
    const N = { grad: 0, blit: 0, fill: 0 };
    const og = C.createRadialGradient, ob = C.drawImage, of = C.fill;
    C.createRadialGradient = function (...a) { N.grad++; return og.apply(this, a); };
    C.drawImage            = function (...a) { N.blit++; return ob.apply(this, a); };
    C.fill                 = function (...a) { N.fill++; return of.apply(this, a); };

    /* the un-cached path this change removed: a fresh gradient-filled canvas per glow. If bar [2]
       cannot see this, it cannot see a regression back to it either. */
    if (MUT === 'nosprite' && typeof heroGlowSprite === 'function') {
      window.heroGlowSprite = function () {
        const S = 64, cv = document.createElement('canvas'); cv.width = S; cv.height = S;
        const g = cv.getContext('2d');
        const gr = g.createRadialGradient(S/2, S/2, 0, S/2, S/2, S/2);
        gr.addColorStop(0, 'rgba(200,180,255,1)'); gr.addColorStop(1, 'rgba(200,180,255,0)');
        g.fillStyle = gr; g.fillRect(0, 0, S, S); return cv;
      };
    }

    const feed = (amp, square) => {
      const n = 128, a = new Float32Array(n);
      for (let i = 0; i < n; i++)
        a[i] = square ? (i % 2 ? amp : -amp) : Math.sin(i / n * Math.PI * 6) * amp;
      window.updateVisualization(0, a, 120);
    };
    const measure = (amp, square) => {
      feed(amp, square);
      renderTerrain(1000);                       // one warm frame (sprite build, layout)
      const g0 = N.grad, b0 = N.blit;
      let blits = 0, grads = 0;
      for (let f = 0; f < 4; f++) {
        const gA = N.grad, bA = N.blit;
        renderTerrain(2000 + f * 16);
        grads += N.grad - gA; blits += N.blit - bA;
      }
      return { glows: blits / 4, grads: grads / 4 };
    };

    const out = {
      silence: measure(0.02, false),
      mid:     measure(0.50, false),
      ceiling: measure(0.966, false),
      clipped: measure(0.966, true),
    };
    C.createRadialGradient = og; C.drawImage = ob; C.fill = of;
    return out;
  }, MUT);

  console.log('══ fb613 HERO PEAK GLOW ══   mutation: ' + (MUT || '(none)'));
  if (R.ERROR) { console.log('  ✗ ' + R.ERROR); await b.close(); process.exit(1); }
  for (const k of ['silence', 'mid', 'ceiling', 'clipped'])
    console.log('   %s%s  glows/frame %6.1f   radial gradients/frame %5.1f'
      .replace('%s%s', (k + '           ').slice(0, 11))
      .replace('%6.1f', R[k].glows.toFixed(1).padStart(6))
      .replace('%5.1f', R[k].grads.toFixed(1).padStart(5)));

  const worst = Math.max(R.ceiling.glows, R.clipped.glows);
  gate(worst <= GLOW_MAX + 0.01 && R.clipped.glows >= GLOW_MAX - 0.01,
       '[1] THE GLOW COUNT IS BOUNDED, AND THE BOUND DEMONSTRABLY FIRES WHEN LOUD',
       'worst ' + worst.toFixed(1) + ' of ' + GLOW_MAX + (R.clipped.glows >= GLOW_MAX - 0.01
         ? '  (a clipped square sits exactly on the cap — the clamp is doing work)'
         : '   ← nothing ever reached the cap, so this bar proves nothing'));

  const maxG = Math.max(R.silence.grads, R.mid.grads, R.ceiling.grads, R.clipped.grads);
  gate(maxG <= 1.01,
       '[2] THE GRADIENT IS BUILT ONCE, NOT PER GLOW',
       maxG.toFixed(1) + ' radial gradients per frame at the worst level'
       + (maxG <= 1.01 ? '  (the sprite is cached and blitted)'
                       : '   ← back to per-pixel shading per glow, which is the whole bug'));

  gate(R.silence.glows > 5 && R.silence.glows < GLOW_MAX,
       '[3] A QUIET SIGNAL STILL GLOWS — THE FIX IS A BUDGET, NOT A DELETION',
       R.silence.glows.toFixed(1) + ' glows at near-silence (unbudgeted, as before)');

  gate(R.ceiling.glows > R.silence.glows,
       '[4] AND LOUDER STILL MEANS MORE MOUNTAINS, UP TO THE BUDGET',
       R.silence.glows.toFixed(1) + ' → ' + R.ceiling.glows.toFixed(1) + ' glows');

  gate(errs.length === 0, '[5] NO PAGE ERRORS', errs.length ? errs.slice(0, 2).join(' | ') : 'clean');
  console.log('\n  ' + PASS + ' pass, ' + FAIL + ' fail' + (MUT ? '   (mutation: ' + MUT + ')' : ''));
  await b.close();
  process.exit(FAIL ? 1 : 0);
})();
