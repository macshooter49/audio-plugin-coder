// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_arrow_gate.js — fb607: THE WAVETABLE ‹ › ARROWS MUST STEP, ON A REAL BACKEND.
//
//    node Tests/wt_arrow_gate.js [page.html]          ARROW_MUT=revert  → must go RED
//
//  Max: "the arrows to select wavetables do NOT work now, i try to move back n forth and no shit
//  is happening, i need to manually select it."
//
//  🚨 WHY EVERY EXISTING GATE STAYED GREEN THROUGH THIS. fb606 changed the `listImports` native
//  from a bare JSON ARRAY OF NAMES to a structured OBJECT and did not update __wtAllCats's reader.
//  `legacy.map` on an object throws — inside a `.then(success, failure)`, whose failure handler
//  CANNOT catch a throw raised by its own success handler — so cb() was never called and the arrow
//  was simply dead. But with NO BACKEND the `pj` helper times out at 250 ms, `legacy` falls back to
//  [], and `.map` is perfectly fine. **A headless page without natives cannot see this class of
//  bug at all.** That is why this gate stubs the natives with the REAL payload shapes rather than
//  running bare, and why it asserts the CALLBACK FIRED, not merely that nothing threw.
//
//  THE BARS
//   · the fb606 shape (listImports = OBJECT) — what the shipping plugin actually returns
//   · the pre-fb606 shape (listImports = ARRAY) — old registries must keep working
//   both must deliver categories AND move the selection.
const puppeteer = require('puppeteer-core');
const PAGE = process.argv[2];
const MUT  = process.env.ARROW_MUT || '';

// fb606's real shapes. listImports is an OBJECT now (it used to be a bare array of names).
const LIST_IMPORTS_NEW = JSON.stringify({
  root: '/Users/x/Library/WavesCrate/Terrain/Wavetables', exists: true, total: 2, dirs: 0, depth: 0,
  subs: [], items: [{ name: 'Managed A', path: '/tmp/a.wav', rel: '' },
                    { name: 'Managed B', path: '/tmp/b.wav', rel: '' }],
  truncated: false, cap: '', depthCap: 6, ms: 3 });
const LIST_IMPORTS_OLD = JSON.stringify(['Legacy One', 'Legacy Two']);   // pre-fb606

const LIST_WT_IMPORTS = JSON.stringify({
  kind: 1,
  files: [{ name: 'Loose One', path: '/tmp/loose.wav' }],
  folders: [{ name: 'TERRAIN-WAVETABLES', path: '/Users/x/Desktop/TERRAIN-WAVETABLES',
              kind: 'user', count: 3, dirs: 2, depth: 1, subs: ['CHAOS', 'SPECTRAL'],
              items: [{ name: 'TERRA CHIRIKOV', path: '/d/CHAOS/TERRA CHIRIKOV.wav', rel: 'CHAOS' },
                      { name: 'TERRA TILT ARC', path: '/d/SPECTRAL/TERRA TILT ARC.wav', rel: 'SPECTRAL' },
                      { name: 'TERRA SIERPINSKI', path: '/d/SPECTRAL/TERRA SIERPINSKI.wav', rel: 'SPECTRAL' }],
              truncated: false, cap: '' }],
  factory: { root: '/f', exists: true, total: 2, note: '',
             cats: [{ dir: 'Chaos', name: 'Chaos', cat: 'Chaos', path: '/f/Chaos', kind: 'factory', count: 2,
                      items: [{ name: 'TERRA RULE110', path: '/f/Chaos/TERRA RULE110.wav', rel: '' },
                              { name: 'TERRA SHATTER', path: '/f/Chaos/TERRA SHATTER.wav', rel: '' }] }] },
  builtin: { total: 0, cats: [] }, cats: [], scan: { ms: 4 } });

const STUB = (liw, li) => {
  const fns = {
    listWtImports: () => Promise.resolve(liw),
    listImports:   () => Promise.resolve(li),
    loadWavetableByPath: () => Promise.resolve(true),
    loadImportedWavetable: () => Promise.resolve(true),
    setWavetableName: () => Promise.resolve(true),
    clearWavetable: () => Promise.resolve(true),
  };
  const mine = { getNativeFunction: (n) => fns[n], backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  let held = mine;
  Object.defineProperty(window, 'Juce', { configurable: true,
    get() { return held; },
    set(v) { held = Object.assign({}, v || {}, { getNativeFunction: mine.getNativeFunction }); } });
  window.__JUCE__ = { backend: mine.backend, initialisationData: { vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [], __juce__functions: [] } };
};

(async () => {
  const b = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });

  const run = async (label, liPayload) => {
    const p = await b.newPage();
    await p.setViewport({ width: 1120, height: 760 });
    const errs = [];
    p.on('pageerror', e => errs.push(e.message));
    await p.evaluateOnNewDocument(STUB, LIST_WT_IMPORTS, liPayload);
    await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
    await new Promise(r => setTimeout(r, 1200));
    await p.evaluate(() => { const sp = document.getElementById('syn-panel');
                             if (sp) sp.style.display = 'block'; window.dispatchEvent(new Event('resize')); });
    await new Promise(r => setTimeout(r, 2000));

    if (MUT === 'revert') {           // mutation: put the old array-only reader back
      await p.evaluate(() => {
        const orig = window.__wtAllCats;
        window.__wtAllCats = function (o, cb) {
          const f = window.Juce.getNativeFunction('listImports');
          Promise.resolve(f()).then(js => { const legacy = JSON.parse(js);
            cb(legacy.map(nm => ({ kind:'import', name:nm, path:null })));   // throws on an object
          });
        };
      });
    }

    const r = await p.evaluate(async () => {
      const out = {};
      out.cats = await new Promise(res => {
        let done = false;
        const t = setTimeout(() => { if (!done) { done = true; res('*** CALLBACK NEVER FIRED ***'); } }, 3000);
        try { window.__wtAllCats('a', c => { if (done) return; done = true; clearTimeout(t);
                res(c.map(x => x.label + ':' + (x.items||[]).length).join(' · ')); }); }
        catch (e) { done = true; clearTimeout(t); res('THREW: ' + e.message); }
      });
      const sel = document.getElementById('osc-a-preset-select');
      const before = sel ? sel.value : null;
      window.wtStepPreset('a', 1);
      await new Promise(r2 => setTimeout(r2, 1600));
      out.stepped = sel && sel.value !== before;
      out.from = before; out.to = sel ? sel.value : null;
      return out;
    });
    await p.close();
    console.log('\n── ' + label + ' ──');
    console.log('   categories : ' + r.cats);
    console.log('   arrow      : ' + (r.stepped ? 'MOVED ' + r.from + ' -> ' + r.to : '*** DID NOT MOVE ***'));
    if (errs.length) console.log('   pageerror  : ' + errs.slice(0,2).join(' | '));
    return r;
  };

  console.log('══ ARROW PROBE 2 — with a real backend payload ══   mutation: ' + (MUT || '(none)'));
  const a = await run('fb606 shape (listImports = OBJECT)  <- what Max\'s plugin returns', LIST_IMPORTS_NEW);
  const c = await run('pre-fb606 shape (listImports = ARRAY) — must still work', LIST_IMPORTS_OLD);
  await b.close();

  const ok = a.cats !== '*** CALLBACK NEVER FIRED ***' && !String(a.cats).startsWith('THREW')
          && c.cats !== '*** CALLBACK NEVER FIRED ***' && a.stepped && c.stepped;
  console.log('\n  VERDICT: ' + (ok ? 'PASS — both shapes deliver categories and the arrow steps'
                                    : '*** FAIL ***'));
  process.exit(ok ? 0 : 1);
})();
