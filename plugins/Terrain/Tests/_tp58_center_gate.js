// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp58 — THE CROSSHAIR TEST: EVERY LETTER SITS ON BOTH OF ITS BOX'S CENTRE LINES.
//
//  Max: "make sure the LETTER IS IN THE MIDDLE OF THE BOX, draw a line thru middle and the top
//  like a horizontal and vertical middle line, and the LETTER NEEDS TO BE IN THE CENTER OF IT,
//  run that test and fix."
//
//  This is that test, in numbers. For every letter-in-a-box on the page it screenshots the box at
//  4x, finds the GLYPH'S INK bounding box, and reports how far the ink centre sits from the box's
//  own centre lines. Not the text node's layout rect — the INK — because a line box reserves
//  descender room under a capital that has no descender, and that gap is exactly the error a
//  layout-rect test cannot see.
//
//  🚨 TWO WAYS THIS MEASUREMENT LIES, AND BOTH BIT THE FIRST CUT:
//   1. CROPPING. Insetting far enough to miss a border also clips the glyph, and every letter then
//      reports the SAME offset — the crop's centre, not the ink's. Four different letters agreeing
//      to three decimal places is the tell. The border is excluded by COLOUR instead (a coloured
//      border has a large channel spread; white-ish ink does not), so nothing is cropped.
//   2. ASSUMING THE BOX IS IN THE MIDDLE OF THE CLIP. A screenshot clip takes integer pixels, so
//      the box sits up to half a pixel off-centre inside it — the same size as the error. The box
//      centre is computed from where the box ACTUALLY is inside the clip.
//
//  TOLERANCE IS HALF A CSS PIXEL, and that is a measurement limit, not a taste: at 4x the ink
//  bbox quantises to 0.125 px, antialiasing thresholds move it again, and sub-pixel vertical
//  padding on these boxes is not even monotone (0.15 px of padding moved a measured ink 1.0 px).
//  Below half a pixel this method is fitting noise.
//
//    node Tests/_tp58_center_gate.js
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
const fs = require('fs'), path = require('path');
const sim = fs.readFileSync(process.cwd() + '/Tests/_ui_lockin_sim.js', 'utf8');
const stubSrc = sim.slice(sim.indexOf('const stub = () => {'), sim.indexOf('// ── the instruments'));
const cpp = fs.readFileSync('Source/PluginEditor.cpp', 'utf8');
const i0 = cpp.indexOf('const juce::String heroOverlay = juce::String (R"TIHX(');
const j0 = cpp.indexOf('html = html.replace ("</body>", heroOverlay', i0);
const ov = [...cpp.slice(i0, j0).matchAll(/R"TIHX\(([\s\S]*?)\)TIHX"/g)].map(m => m[1]).join('');
const html = fs.readFileSync('Source/ui/public/index.html', 'utf8').replace('</body>', ov + '</body>');
const PAGE = path.join(require('os').tmpdir(), 'probe58f.html'); fs.writeFileSync(PAGE, html);
const fakeSample = () => { const N = 900, mn = [], mx = [];
  for (let i = 0; i < N; i++) { const e = 0.35 + 0.5*Math.abs(Math.sin(i/70)); mx.push(e); mn.push(-e); }
  window.onSampleLoaded({ filename:'Drum Loop.wav', lengthSamples:220500, peaksMin:mn, peaksMax:mx, rootMidiNote:60 }); };

// ── the crosshair test: a letter's INK must sit on the box's own centre lines ────────────────
async function centring(p, sel, label) {
  const boxes = await p.evaluate(s => [...document.querySelectorAll(s)].map(el => {
    const r = el.getBoundingClientRect();
    return { t:(el.textContent||'').trim().slice(0,2), x:r.x, y:r.y, w:r.width, h:r.height }; }), sel);
  const out = [];
  for (const bx of boxes) {
    if (!bx.w || !bx.h || bx.x < 0 || bx.y < 0) continue;
    const pad = 5;
    const clip = { x: Math.floor(bx.x) - pad, y: Math.floor(bx.y) - pad,
                   width: Math.ceil(bx.w) + pad*2 + 1, height: Math.ceil(bx.h) + pad*2 + 1 };
    if (clip.x < 0 || clip.y < 0) continue;
    const b64 = await p.screenshot({ clip, encoding: 'base64' });
    const r = await p.evaluate(async (b64, m) => {
      const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
      const cv = document.createElement('canvas'); cv.width = img.width; cv.height = img.height;
      const c = cv.getContext('2d'); c.drawImage(img, 0, 0);
      const d = c.getImageData(0,0,cv.width,cv.height).data, S = cv.width / m.clipW;
      // ink = near-neutral bright pixels strictly inside the border (a purple/coloured border has
      // a big channel spread and is excluded by that, not by an inset that would crop the glyph)
      // The search window is CENTRED ON THE BOX and no wider than it needs to be. A single letter
      // is a few px across however wide its box is, so a wide button's border (a near-neutral
      // white line, indistinguishable from ink by colour) is excluded by never being looked at —
      // and a small chip, where the glyph nearly fills the box, still gets its whole glyph.
      const winW = Math.min(m.bw - 4, Math.max(14, m.bw * 0.45));
      const winH = Math.min(m.bh - 4, Math.max(14, m.bh * 0.45));
      const cx0 = (m.bx + m.bw/2 - m.clipX) * S, cy0 = (m.by + m.bh/2 - m.clipY) * S;
      const x0 = Math.max(0, Math.round(cx0 - winW/2*S)), x1 = Math.min(cv.width,  Math.round(cx0 + winW/2*S));
      const y0 = Math.max(0, Math.round(cy0 - winH/2*S)), y1 = Math.min(cv.height, Math.round(cy0 + winH/2*S));
      // ⚠️ THE THRESHOLD IS ADAPTIVE, and it has to be: an inactive pad is painted at opacity .55,
      // so a fixed cut that finds the lit letters reports "no ink" on the dim ones — which is a
      // gate that passes or fails on which pad happens to be selected. Take the brightest neutral
      // pixel in the box and cut at 62% of the way from the ground to it.
      let lo=255, hi=0;
      for (let y=y0; y<y1; y++) for (let x=x0; x<x1; x++) {
        const o=(y*cv.width+x)*4, R=d[o],G=d[o+1],B=d[o+2];
        const lum=(R+G+B)/3, sp=Math.max(R,G,B)-Math.min(R,G,B);
        if (sp<45) { if(lum<lo)lo=lum; if(lum>hi)hi=lum; } }
      if (hi-lo < 25) return { err:'no ink' };
      const cut = lo + (hi-lo)*0.62;
      let miX=1e9,maX=-1,miY=1e9,maY=-1;
      for (let y=y0; y<y1; y++) for (let x=x0; x<x1; x++) {
        const o=(y*cv.width+x)*4, R=d[o],G=d[o+1],B=d[o+2];
        const lum=(R+G+B)/3, sp=Math.max(R,G,B)-Math.min(R,G,B);
        if (lum>cut && sp<45) { if(x<miX)miX=x; if(x>maX)maX=x; if(y<miY)miY=y; if(y>maY)maY=y; } }
      if (maX<0) return { err:'no ink' };
      return { S, cx:(miX+maX)/2, cy:(miY+maY)/2, iw:(maX-miX+1)/S, ih:(maY-miY+1)/S };
    }, b64, { pad, clipW: clip.width, clipX: clip.x, clipY: clip.y, bx: bx.x, by: bx.y, bw: bx.w, bh: bx.h });
    if (r.err) { out.push({ t:bx.t, err:r.err }); continue; }
    const bcx = (bx.x + bx.w/2 - clip.x) * r.S, bcy = (bx.y + bx.h/2 - clip.y) * r.S;
    out.push({ t:bx.t, dx:+((r.cx-bcx)/r.S).toFixed(3), dy:+((r.cy-bcy)/r.S).toFixed(3),
               box:+bx.w.toFixed(1)+'x'+(+bx.h.toFixed(1)), ink:+r.iw.toFixed(1)+'x'+(+r.ih.toFixed(1)) });
  }
  console.log('\n' + label + '  (' + sel + ')');
  out.forEach(function (o) { console.log('   ' + String(o.t||'?').padEnd(3) + (o.err ? o.err :
    ('box ' + String(o.box).padEnd(12) + ' ink ' + String(o.ink).padEnd(11)
     + ' dx=' + String(o.dx).padStart(7) + '  dy=' + String(o.dy).padStart(7)))); });
  return out;
}
const TOL = 0.5;
let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless:'new', args:['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 4 });
  await p.evaluateOnNewDocument(stubSrc + '\nstub();');
  const errs = []; p.on('pageerror', e => errs.push(e.message.slice(0,160)));
  await p.goto('file://' + PAGE, { waitUntil:'load' }); await sleep(2500);
  await p.evaluate(() => document.documentElement.setAttribute('data-theme','dark'));
  await p.evaluate(() => document.getElementById('mix-btn').click()); await sleep(1400);
  await p.evaluate(fakeSample); await sleep(800);
  const fams = [
    ['#trigger-context .layer-status-dot',        'the LAYER menu A/B/C/D'],
    ['#mix-stem-area .stem-buttons .stem-btn',    'the STEM menu A/B/C/D'],
    ['#ti-layer-pads .ti-layer-pad',              'the hero A/B/C/D pads'],
  ];
  let bar = 0;
  for (const [sel, name] of fams) {
    const r = await centring(p, sel, name);
    const bad = r.filter(o => o.err || Math.abs(o.dx) > TOL || Math.abs(o.dy) > TOL);
    ok(r.length >= 4 && !bad.length,
       '[' + (bar++) + '] ' + name + ' — every letter is on BOTH centre lines (±' + TOL + ' px)',
       r.map(o => o.t + ' dx=' + o.dx + ' dy=' + o.dy).join('  ·  '));
  }
  ok(errs.length === 0, '[' + bar + '] NO PAGE ERRORS', errs.slice(0,3).join(' | ') || 'clean');
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed');
  await b.close();
  process.exit(fail ? 1 : 0);
})();
