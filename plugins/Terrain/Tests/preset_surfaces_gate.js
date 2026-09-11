// ══════════════════════════════════════════════════════════════════════════════════════════════
//  preset_surfaces_gate.js — fb620: THE TWO SURFACES, DRIVEN LIKE A HAND WOULD DRIVE THEM.
//
//    node Tests/preset_surfaces_gate.js [page.html]
//
//  The page runs headless with the JUCE bridge stubbed the way the fb606 gate stubs it, plus the
//  fb618/fb619 preset natives answering from an in-memory catalogue (two banks, six presets). The
//  bars click the real furniture — the header name, the quick menu, Browse all, rows, the +, the
//  sheets — through the DOM, and ask what the plugin would ask: did the native fire with the right
//  arguments, did the page hear the load, did every widget re-seed, can a click LAND on the glass.
//
//  THE BARS
//   0  THE PAGE LAID OUT and the FX-era preset furniture is GONE (#preset-browser, the tag picker)
//   1  THE HEADER NAMES THE PATCH — getPresetMeta → "Pad - Glacier"
//   2  THE QUICK MENU — click the name: banks · Favourites · Import pack…; the current bank's list
//      grouped by type with the current preset marked; the box is hit-testable and inside the viewport
//   3  🚨 BROWSE ALL — the glass opens above the page: elementFromPoint at its centre is INSIDE #tp-b
//      (a hit test, never a z-index compare — fb606's law), six rows, the painters are frozen
//   4  SEARCH filters; the CHIPS are the selected preset's own type and style and SET them
//      (fb625 — they used to filter; a factory preset's chips show but never take an edit)
//   5  SELECT + INSPECTOR — a click selects and names the row; a factory note is read-only, a user
//      note is editable
//   6  🚨 A LOAD IS ONE CALL AND THE PAGE HEARS IT ONCE — two clicks → loadPatchFile(path) → (the C++
//      push) onPatchLoaded → the header changes, the glass closes, the painters wake, and EVERY
//      re-seed hook the page exposes fired exactly once
//   7  THE ARROWS STEP WITHIN THE BANK — › loads the next Terra preset, ‹ the previous
//   8  SAVE AS — + → Save as… → the sheet → savePresetToBank('User', meta) with the typed name and
//      styles → the header is the saved name (the catalogue rescanned)
//   9  SAVE — enabled only on a user preset; overwrites in place with the same name
//  10  FAVOURITE — the heart calls setFavourite(bank, name, true) and the row lights
//  11  DELETE ASKS — a sheet, never confirm(); Delete → deletePresetFile(path) → the row is gone
//  12  NO prompt()/confirm()/alert() ANYWHERE — WKWebView has no panels for them
//  13  ESCAPE closes the sheet, the menu, the browser; a mousedown outside closes the quick menu
//  14  fb622 — the preset cluster is CENTRED in the header and EQ/DLY have no button (code intact)
//  15  fb622 — selection is the WORDS (name, type, style go accent), never a fill, row or rail
//  16  fb624 — the Load BUTTON keeps the browser open; only a double-click on a row closes it
//  17  fb624 — the inspector: author under the name and editable, Effects/Author rows gone, the
//      type/style filters moved OUT of the top and into the scrolling column
//  18  fb625 — the header name CLOSES the browser (back to the synth), never stacks a menu on it
//  19  fb626 — every preset surface (quick menu, browser, sheet) stands the synth page's floating
//      overlays down: the modulation attenuator sits at the maximum z and belongs to a covered page
//  20  fb628 — every column is a lane: the author cannot be pushed into Carries by a long name
//  21  fb629 — the header has NO COLOUR OF ITS OWN: it takes the synth page's ground, then the
//      browser's glass. Proved in PIXELS across the y=44 seam, not by reading the stylesheet.
//  22  fb631 — the header carries the wordmark ALONE (the mark is parked in Design/mark/): no
//      img/svg in .header-left, TERRAIN dead centre, on the centreline; the preset cluster is at
//      the left, at the gear's inset ([14])
//  23  fb630 — every engine knob carries its VALUE inside the ring, in every engine and the noise
//      strip, and every glyph box sits inside the circle at a readable size; nothing else moved
//  24  fb631 — the readouts are BARE NUMBERS: WT Pos is the frame number from the live table's
//      count, bipolar arcs show distance from centre, no units, no decimals, fractions only for the
//      FM ratio, a choice keeps a word of ≤3 letters, Shape's label is its target waveform
//  25  fb630 — the native capture strip is told the browser's glass colour, composited from the
//      live tokens, when the browser opens, and told to drop it when it closes
//
//  MUTATION CONTROLS
//    TP_MUT=zorder   #syn-panel is raised over the browser → [3] must go RED (the hit test)
//    TP_MUT=lanes    the pre-fb628 world: .sm-ul reaches over the preset surfaces again and the
//                    author's lane loses its clearance → [19] and [20] must both go RED
//    TP_MUT=header   the header paints its own bar again and a mark is put back in it →
//                    [21] and [22] must both go RED
//    TP_MUT=knobval  the ring text is blown up to 14px and the stacked fraction hidden, and the
//                    strip's native call is swallowed → [23] [24] [25] must all go RED
//    TP_MUT=nopush   the courier loads but never pushes onPatchLoaded (the pre-fb620 world) →
//                    [6] and [7] must go RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const fs = require ('fs');
const puppeteer = require ('puppeteer-core');
const { decode, band, dist, hex } = require ('./png_pixels');
const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');
const MUT  = process.env.TP_MUT || '';

let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

const STUB = (MUT) => {
  window.__gateCalls = []; window.__panels = [];
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  // ── the catalogue the fb619 scan would return ─────────────────────────────────────────────
  const P = (bank, name, type, styles, c, factory) => ({ name, bank, author: factory ? 'Waves Crate' : 'Max', type, styles, note: factory ? 'Mod wheel opens the filter.' : '', fv: 1,
    carries: { wt: c[0], smp: c[1], ir: c[2], flow: c[3], lfo: c[4], nodes: c[5] }, path: '/tmp/Banks/' + bank + '/' + name + '.terrain', mtime: 1, factory: !!factory });
  window.__CAT = { banks: [
    { name: 'Terra', author: 'Waves Crate', version: '1', factory: true, dir: '/f/Terra', presets: [
      P('Terra', 'Glacier', 'Pad', 'Glassy,Evolving', [2,0,0,0,2,0], true), P('Terra', 'Cirrus', 'Pad', 'Bright,Wide', [1,0,0,0,1,0], true),
      P('Terra', 'Tectonic', 'Bass', 'Dark,Punchy', [1,0,0,1,0,0], true), P('Terra', 'Anvil', 'Lead', 'Metallic,Punchy', [1,0,0,2,0,0], true) ] },
    { name: 'User', author: 'Max', version: '1', factory: false, dir: '/tmp/Banks/User', presets: [
      P('User', 'Slatt', 'Bass', 'Dark,Dirty', [1,0,0,1,0,0], false), P('User', 'Monte Cristo', 'Keys', 'Warm,Clean', [1,0,0,0,0,0], false) ] } ],
    caps: { banks: 2, presets: 6, unreadable: 0, maxBanks: 200, maxPresets: 5000 }, userRoot: '/tmp/Banks', factoryRoot: '/f' };
  const metaOfPath = (p) => { for (const b of window.__CAT.banks) for (const r of b.presets) if (r.path === p) return { name: r.name, bank: r.bank, author: r.author, type: r.type, styles: r.styles, note: r.note, fv: 1 }; return null; };
  let curMeta = { name: 'Glacier', bank: 'Terra', author: 'Waves Crate', type: 'Pad', styles: 'Glassy,Evolving', note: 'Mod wheel opens the filter.', fv: 1 };
  const nf = (n) => (...a) => new Promise (r => {
    if (n === 'setBrowserGlass' && MUT === 'knobval') return r (0);   // fb630's control: the strip never hears
    window.__gateCalls.push ({ fn: n, args: a.map (String) });
    if (n === 'listPresets')   return r (JSON.stringify (window.__CAT));
    if (n === 'getPresetMeta') return r (JSON.stringify (curMeta));
    if (n === 'setPresetMeta') { try { curMeta = JSON.parse (a[0]); } catch (e) {} return r ('ok'); }
    if (n === 'loadPatchFile') { const m = metaOfPath (String (a[0])); if (! m) return r ('error:no such preset'); curMeta = m;
      if (MUT !== 'nopush') setTimeout (() => { if (window.onPatchLoaded) window.onPatchLoaded (JSON.stringify (m)); }, 0);   // the C++ afterPatchLoad push
      return r (JSON.stringify (m)); }
    if (n === 'savePresetToBank') { let m = {}; try { m = JSON.parse (a[1]); } catch (e) {} const bank = String (a[0]);
      let b = window.__CAT.banks.find (x => x.name === bank); if (! b) { b = { name: bank, author: m.author || '', version: '1', factory: false, dir: '/tmp/Banks/' + bank, presets: [] }; window.__CAT.banks.push (b); }
      if (b.factory) return r ('error:' + bank + ' is a factory bank');
      const row = P (bank, m.name, m.type, m.styles, [0,0,0,0,0,0], false); row.author = m.author; row.note = m.note || '';
      const at = b.presets.findIndex (x => x.name === m.name); if (at >= 0) b.presets[at] = row; else b.presets.push (row);
      curMeta = Object.assign ({}, m, { bank }); return r (row.path); }
    if (n === 'deletePresetFile') { for (const b of window.__CAT.banks) b.presets = b.presets.filter (x => x.path !== String (a[0])); return r ('ok'); }
    if (n === 'getFavourites') return r ('{}');
    if (n === 'getVocab')      return r ('');
    if (/^(setFavourite|setVocab|updatePresetMeta|createBank|renameBank|deleteBank|exportBank|exportPreset|exportPresetFile|importPack)$/.test (n)) return r ('ok');
    if (/getPresets/i.test (n)) return r ('[]');
    if (/Json|JSON|Names|Lanes|Shapes|Envs|Mod$/.test (n)) return r ('');
    r (0); });
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk, getNativeFunction: nf,
    backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  window.prompt = () => { window.__panels.push ('prompt'); return null; };
  window.confirm = () => { window.__panels.push ('confirm'); return false; };
  window.alert = () => { window.__panels.push ('alert'); };
};

(async () => {
  console.log ('══ preset_surfaces_gate (fb620) — THE TWO SURFACES ══   mutation: ' + (MUT || '(none)'));
  const b = await puppeteer.launch ({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 820, height: 656, deviceScaleFactor: 1 });
  const errors = []; p.on ('pageerror', e => errors.push (String (e.message || e).slice (0, 160)));
  await p.evaluateOnNewDocument (STUB, MUT);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1600));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel'); if (sp) { sp.classList.remove ('hidden'); sp.style.display = 'block'; } window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2000));
  if (MUT === 'zorder') await p.evaluate (() => { const st = document.createElement ('style'); st.textContent = '#syn-panel{z-index:5000 !important}'; document.head.appendChild (st); });
  /* fb628's control: put the PRE-fb628 world back — the modulation underline reaches over the
     preset surfaces again, and the author's lane loses its clearance so it can touch Carries. */
  /* fb629's control: give the header its bar back and shrink the mark to nothing */
  if (MUT === 'header') await p.evaluate (() => { const st = document.createElement ('style');
    st.textContent = '#header{background:#232340 !important;border-bottom:1px solid rgba(58,58,88,.5) !important}';
    document.head.appendChild (st);
    const m = document.createElement ('div'); m.className = 'brand-logo'; m.innerHTML = '<svg viewBox="0 0 10 10"><circle r="5" cx="5" cy="5" fill="#fff"/></svg>';
    document.querySelector ('#header .header-left').prepend (m); });
  /* fb630's control: the ring text can no longer fit, the fraction vanishes */
  if (MUT === 'knobval') await p.evaluate (() => { const st = document.createElement ('style');
    st.textContent = '#syn-panel .knob-ring .kv{font-size:14px !important} #syn-panel .knob-ring .kv.st{display:none !important}';
    document.head.appendChild (st); });
  if (MUT === 'lanes') await p.evaluate (() => { const st = document.createElement ('style');
    st.textContent = 'html.tp-browsing .sm-ul{display:block !important} .tp-row .c-au{margin-right:0 !important}';
    document.head.appendChild (st); });
  const calls = (fn) => p.evaluate (fn => window.__gateCalls.filter (c => c.fn === fn), fn);
  const wait = (ms) => new Promise (r => setTimeout (r, ms));
  const centreIn = (sel) => p.evaluate (sel => { const el = document.querySelector (sel); if (! el) return { ok: false, why: 'no ' + sel }; const r = el.getBoundingClientRect();
    const hit = document.elementFromPoint (r.left + r.width / 2, r.top + r.height / 2); const ok = !! (hit && el.contains (hit));
    return { ok, hit: hit ? (hit.id ? '#' + hit.id : hit.tagName + '.' + String (hit.className).split (' ')[0]) : 'nothing', box: [r.left | 0, r.top | 0, r.width | 0, r.height | 0].join ('×') }; }, sel);
  const rowByName = (name) => p.evaluateHandle (name => [...document.querySelectorAll ('#tp-rows .tp-row')].find (r => r.querySelector ('.c-nm .t').textContent.trim().endsWith (name)), name);
  /* fb625 — editing a chip rebuilds the row list, so a handle taken before the edit is DETACHED by
     the time it is clicked (puppeteer: "Node is either not clickable"). Find and click inside the
     page instead: the row's own handler is what is under test here, not hit-testing. */
  /* fb625 — clicking the header name while BROWSING closes the browser (Max's ask), so opening it
     has to check first instead of assuming the name always opens the quick menu. */
  const openBrowser = async () => {
    if (await p.evaluate (() => document.getElementById ('tp-b').classList.contains ('on'))) return;
    await p.click ('#preset-name'); await wait (140); await p.click ('#tp-q-browse'); await wait (260);
  };
  const clickRow = (name, n) => p.evaluate ((name, n) => {
    const r = [...document.querySelectorAll ('#tp-rows .tp-row')].find (x => x.querySelector ('.c-nm .t').textContent.trim().endsWith (name));
    if (! r) return false;
    for (let i = 0; i < (n || 1); i++) r.click();
    return true;
  }, name, n);
  const header = () => p.evaluate (() => document.getElementById ('preset-name').textContent.trim());

  // [0] laid out, FX-era gone
  const laid = await p.evaluate (() => ({ h: document.getElementById ('header') && document.getElementById ('header').offsetHeight,
    q: !! document.getElementById ('tp-q'), b: !! document.getElementById ('tp-b'), old: ['preset-browser', 'tag-picker', 'delete-confirm', 'save-prompt', 'preset-ctx-menu'].filter (id => document.getElementById (id)) }));
  gate (laid.h === 44 && laid.q && laid.b && laid.old.length === 0 && errors.length === 0, '[0] THE PAGE LAID OUT · the FX-era preset furniture is gone · no page errors',
    `header ${laid.h}px · #tp-q ${laid.q} · #tp-b ${laid.b} · old ids left: ${laid.old.join (',') || 'none'} · errors: ${errors.join (' | ') || 'none'}`);

  // [1] header
  gate (await header() === 'Pad - Glacier', '[1] THE HEADER NAMES THE PATCH (getPresetMeta)', 'reads "' + await header() + '"');

  // [2] quick menu
  await p.click ('#preset-name'); await wait (150);
  const q = await p.evaluate (() => { const m = document.getElementById ('tp-q'); const banks = [...m.querySelectorAll ('#tp-q-banks .pi .nm')].map (e => e.textContent.trim());
    const secs = [...m.querySelectorAll ('#tp-q-list .ps')].map (e => e.textContent.trim()); const cur = m.querySelector ('#tp-q-list .pi.cur .nm'); const r = m.getBoundingClientRect();
    return { on: m.classList.contains ('on'), banks, secs, cur: cur ? cur.textContent.trim() : null, inside: r.left >= 0 && r.top >= 40 && r.right <= 820 && r.bottom <= 656, box: [r.left | 0, r.top | 0, r.width | 0, r.height | 0].join ('×') }; });
  const qh = await centreIn ('#tp-q');
  gate (q.on && q.banks.join ('|') === 'Terra|User|Favourites|Import pack…' && q.secs.join ('|') === 'Bass|Lead|Pad' && q.cur === 'Glacier' && q.inside && qh.ok,
    '[2] THE QUICK MENU — banks · Favourites · Import; the bank\'s list by type with the current marked; hit-testable, inside the box',
    `on ${q.on} · banks ${q.banks.join (' / ')} · sections ${q.secs.join (' / ')} · cur ${q.cur} · box ${q.box} inside ${q.inside} · elementFromPoint → ${qh.hit}`);

  // [3] browse all — the hit test
  await p.click ('#tp-q-browse'); await wait (250);
  const bh = await centreIn ('#tp-b');
  const bst = await p.evaluate (() => ({ on: document.getElementById ('tp-b').classList.contains ('on'), qoff: ! document.getElementById ('tp-q').classList.contains ('on'),
    rows: document.querySelectorAll ('#tp-rows .tp-row:not(.off)').length, count: document.getElementById ('tp-count').textContent.trim(), frozen: window.__tiFrozen === true,
    z: getComputedStyle (document.getElementById ('tp-b')).zIndex, zs: getComputedStyle (document.getElementById ('syn-panel')).zIndex }));
  gate (bst.on && bst.qoff && bh.ok && bst.rows === 6 && /^6 presets/.test (bst.count) && bst.frozen,
    '[3] 🚨 BROWSE ALL — the glass opens ABOVE the page (elementFromPoint at its centre lands inside #tp-b), six rows, painters frozen',
    `on ${bst.on} · quick closed ${bst.qoff} · hit → ${bh.hit} (box ${bh.box}) · rows ${bst.rows} · "${bst.count}" · __tiFrozen ${bst.frozen} · z-index browser ${bst.z} vs #syn-panel ${bst.zs}`);

  // [4] search filters; the chips are the SELECTED PRESET's type and style, and they SET them
  await p.type ('#tp-search', 'glac'); await wait (80);
  const n1 = await p.evaluate (() => document.querySelectorAll ('#tp-rows .tp-row:not(.off)').length);
  await p.evaluate (() => { const s = document.getElementById ('tp-search'); s.value = ''; s.dispatchEvent (new Event ('input')); });
  await wait (60);
  {
    await clickRow ('Slatt'); await wait (140);      // a USER preset: Bass · Dark, Dirty
    const lit = () => p.evaluate (() => ({
      type:  [...document.querySelectorAll ('#tp-chips-type .tp-chip.active')].map (e => e.textContent.trim()),
      style: [...document.querySelectorAll ('#tp-chips-style .tp-chip.active')].map (e => e.textContent.trim()),
      hasAll: [...document.querySelectorAll ('#tp-chips-type .tp-chip')].some (e => e.textContent.trim() === 'All') }));
    const before = await lit();
    await p.evaluate (() => [...document.querySelectorAll ('#tp-chips-type .tp-chip')].find (c => c.textContent.trim() === 'Lead').click());
    await wait (200);
    const afterType = await lit();
    await p.evaluate (() => [...document.querySelectorAll ('#tp-chips-style .tp-chip')].find (c => c.textContent.trim() === 'Dark').click());
    await wait (200);
    const afterStyle = await lit();
    const writes = await calls ('updatePresetMeta');
    const patches = writes.map (c => { try { return JSON.parse (c.args[1]); } catch (e) { return {}; } });
    // and a FACTORY preset must not take an edit
    await clickRow ('Cirrus'); await wait (140);
    const nBefore = (await calls ('updatePresetMeta')).length;
    await p.evaluate (() => { const c = [...document.querySelectorAll ('#tp-chips-type .tp-chip')].find (x => x.textContent.trim() === 'Bass'); if (c) c.click(); });
    await wait (200);
    const nAfter = (await calls ('updatePresetMeta')).length;
    gate (n1 === 1 && ! before.hasAll
          && before.type.join () === 'Bass' && before.style.sort().join () === 'Dark,Dirty'
          && afterType.type.join () === 'Lead' && afterStyle.style.join () === 'Dirty'
          && patches.some (x => x.type === 'Lead') && patches.some (x => x.styles === 'Dirty')
          && nAfter === nBefore,
      '[4] SEARCH FILTERS · THE CHIPS ARE THIS PRESET’S TYPE AND STYLE, AND SET THEM (factory presets stay read-only)',
      `"glac" → ${n1} row · an All chip still there? ${before.hasAll} · lit at rest: type [${before.type}] style [${before.style}]`
      + ` · after clicking Lead: [${afterType.type}] · after toggling Dark off: [${afterStyle.style}]`
      + ` · wrote ${JSON.stringify (patches)} · a factory preset wrote ${nAfter - nBefore} times`);
  }

  // [5] select + inspector
  await clickRow ('Cirrus'); await wait (80);
  const ins = await p.evaluate (() => ({ sel: document.querySelector ('#tp-rows .tp-row.sel .c-nm .t').textContent.trim(), nm: document.querySelector ('#tp-insp .nm').textContent.trim(), ro: ! document.getElementById ('tp-note').hasAttribute ('contenteditable') }));
  await clickRow ('Slatt'); await wait (80);
  const ins2 = await p.evaluate (() => ({ nm: document.querySelector ('#tp-insp .nm').textContent.trim(), ed: document.getElementById ('tp-note').getAttribute ('contenteditable') === 'plaintext-only' }));
  gate (ins.sel === 'Cirrus' && ins.nm === 'Cirrus' && ins.ro && ins2.nm === 'Slatt' && ins2.ed,
    '[5] SELECT + INSPECTOR — a click names the row; a factory note is read-only, a user note is editable', `${ins.sel} / ${ins.nm} readonly ${ins.ro} · ${ins2.nm} editable ${ins2.ed}`);

  // [6] the load
  const hooks = await p.evaluate (() => { window.__hookCalls = {}; const names = ['__tiBlendRefresh', '__crvPull', '__tiSeedMacroNames', '__tiPullLfoShapes', '__tiPullDynEnvs', '__tiModRestoreStrict', '__tiPullArpLanes', '__tiRestoreNoiseSel'];
    const have = []; for (const n of names) { const real = window[n]; if (typeof real !== 'function') continue; have.push (n); window.__hookCalls[n] = 0;
      window[n] = function () { window.__hookCalls[n]++; return real.apply (this, arguments); }; }
    /* fb622 — the two the owner caught by eye. The FX rack rebuild and the wavetable view restore
       are spied whether or not the real widget initialised headless: what is being pinned is that
       repull() CALLS them, which is precisely what was missing — fxrRestoreChain already worked and
       nothing could reach it, so a loaded preset redrew the previous rack and painted "Add Effects". */
    { const real = window.__fxrRestoreChain; window.__hookCalls.__fxrRestoreChain = 0; have.push ('__fxrRestoreChain');
      window.__fxrRestoreChain = function () { window.__hookCalls.__fxrRestoreChain++; if (typeof real === 'function') return real.apply (this, arguments); }; }
    { window.wtWaterfall = window.wtWaterfall || {}; const real = window.wtWaterfall.restoreView;
      window.__hookCalls.restoreView = 0; have.push ('wtWaterfall.restoreView');
      window.wtWaterfall.restoreView = function (force) { if (force) window.__hookCalls.restoreView++; if (typeof real === 'function') return real.apply (this, arguments); }; }
    if (window.__tiCardPulls && window.__tiCardPulls.length) { have.push ('__tiCardPulls×' + window.__tiCardPulls.length); window.__hookCalls.cards = 0; window.__tiCardPulls = window.__tiCardPulls.map (f => () => { window.__hookCalls.cards++; return f(); }); }
    return have; });
  await clickRow ('Cirrus'); await wait (90); await clickRow ('Cirrus'); await wait (300);   // two clicks, as a hand does it
  const ld = await calls ('loadPatchFile');
  const after = await p.evaluate (() => ({ h: document.getElementById ('preset-name').textContent.trim(), open: document.getElementById ('tp-b').classList.contains ('on'), frozen: !! window.__tiFrozen, hooks: window.__hookCalls }));
  const hookOk = hooks.length >= 4 && Object.keys (after.hooks).every (k => after.hooks[k] === 1);
  gate (ld.length >= 1 && ld[ld.length - 1].args[0] === '/tmp/Banks/Terra/Cirrus.terrain' && after.h === 'Pad - Cirrus' && ! after.open && ! after.frozen && hookOk,
    '[6] 🚨 A LOAD IS ONE CALL AND THE PAGE HEARS IT ONCE — loadPatchFile(path) → onPatchLoaded → header, glass closed, painters awake, every re-seed hook fired once',
    `loadPatchFile ×${ld.length} (${ld.length ? ld[ld.length - 1].args[0] : '—'}) · header "${after.h}" · browser open ${after.open} · frozen ${after.frozen} · hooks present ${hooks.join (',')} · fired ${JSON.stringify (after.hooks)}`);

  // [7] arrows
  await p.click ('#preset-next'); await wait (200); const hN = await header(); const ldN = await calls ('loadPatchFile');
  await p.click ('#preset-prev'); await wait (200); const hP = await header();
  gate (ldN.length >= 2 && ldN[ldN.length - 1].args[0] === '/tmp/Banks/Terra/Tectonic.terrain' && hN === 'Bass - Tectonic' && hP === 'Pad - Cirrus',
    '[7] THE ARROWS STEP WITHIN THE BANK — › Tectonic, ‹ back to Cirrus', `› "${hN}" (${ldN.length ? ldN[ldN.length - 1].args[0] : '—'}) · ‹ "${hP}"`);

  // [8] save as
  await p.click ('#preset-save-btn'); await wait (120);
  const menuRows = await p.evaluate (() => [...document.querySelectorAll ('#tp-ctx .pi')].map (e => e.querySelector ('.nm').textContent.trim() + (e.classList.contains ('off') ? '(off)' : '')));
  await p.evaluate (() => document.querySelector ('#tp-ctx .pi[data-a=saveas]').click()); await wait (150);
  const sheetOn = await p.evaluate (() => document.getElementById ('tp-sheet').classList.contains ('on') && !! document.getElementById ('tp-sv-name'));
  /* fb627 — Type and Style are BOXES in the sheet now, so the gate clicks them the way a hand does */
  const sheetChips = () => p.evaluate (() => ({
    type:  [...document.querySelectorAll ('#tp-sv-type .tp-chip.active')].map (e => e.textContent.trim()),
    style: [...document.querySelectorAll ('#tp-sv-style .tp-chip.active')].map (e => e.textContent.trim()),
    typed: !! document.querySelector ('#tp-sv-type input') }));
  const chipsBefore = await sheetChips();   /* Save as… from Cirrus prefills its Type — Pad should already be lit */
  const clickChip = (where, word) => p.evaluate ((where, word) => {
    const c = [...document.querySelectorAll (where + ' .tp-chip')].find (x => x.textContent.trim() === word);
    if (c) c.click();
  }, where, word);
  await p.evaluate (() => { document.getElementById ('tp-sv-name').value = 'Gate Pad';
    document.getElementById ('tp-sv-bank').value = 'User';
    document.getElementById ('tp-sv-note').textContent = 'Written in the save sheet.'; });
  /* one click per turn: each one rebuilds the strip, so a second click in the same pass would land
     on a detached node — the same staleness the row list has */
  await clickChip ('#tp-sv-type', 'Keys');   await wait (70);
  await clickChip ('#tp-sv-style', 'Dark');  await wait (70);   /* one it does NOT have — clicking a lit one removes it */
  const chipsAfter = await sheetChips();
  await wait (60); await p.click ('#tp-sh-ok'); await wait (350);
  const sv = await calls ('savePresetToBank'); let svm = {}; try { svm = JSON.parse (sv[0].args[1]); } catch (e) {}
  const h8 = await header();
  gate (menuRows.join ('|') === 'Save(off)|Save as|Export preset|Init preset' && sheetOn && sv.length === 1 && sv[0].args[0] === 'User' && svm.name === 'Gate Pad' && svm.type === 'Keys' && /Wide/.test (svm.styles) && /Dark/.test (svm.styles) && svm.note === 'Written in the save sheet.' && h8 === 'Keys - Gate Pad'
          && ! chipsBefore.typed && chipsBefore.type.join () === 'Pad'
          && chipsAfter.type.join () === 'Keys' && chipsAfter.style.includes ('Dark') && chipsAfter.style.includes ('Wide'),
    '[8] SAVE AS — the sheet PICKS BOXES (no typing), carries the note, and savePresetToBank gets all of it',
    `menu ${menuRows.join (' / ')} · sheet ${sheetOn} · a type TEXT FIELD still there? ${chipsBefore.typed}`
    + ` · opened with type [${chipsBefore.type}] · after clicking boxes: type [${chipsAfter.type}] style [${chipsAfter.style}]`
    + ` · native ×${sv.length} bank ${sv[0] ? sv[0].args[0] : '—'} meta ${JSON.stringify (svm)} · header "${h8}"`);

  // [9] save (overwrite) on a user preset
  await p.click ('#preset-save-btn'); await wait (120);
  const saveOff = await p.evaluate (() => document.querySelector ('#tp-ctx .pi[data-a=save]').classList.contains ('off'));
  await p.evaluate (() => document.querySelector ('#tp-ctx .pi[data-a=save]').click()); await wait (300);
  const sv2 = await calls ('savePresetToBank'); let svm2 = {}; try { svm2 = JSON.parse (sv2[1].args[1]); } catch (e) {}
  gate (! saveOff && sv2.length === 2 && sv2[1].args[0] === 'User' && svm2.name === 'Gate Pad', '[9] SAVE — enabled on a user preset, overwrites in place under the same name', `Save off ${saveOff} · native ×${sv2.length} → ${sv2[1] ? sv2[1].args[0] : '—'} / ${svm2.name}`);

  // [10] favourite
  await openBrowser();
  await p.evaluate (() => { const r = [...document.querySelectorAll ('#tp-rows .tp-row')].find (x => x.querySelector ('.c-nm .t').textContent.trim().endsWith ('Slatt'));
    if (r) r.querySelector ('.c-fv').click(); }); await wait (100);
  const favc = await calls ('setFavourite'); const lit = await p.evaluate (() => { const r = [...document.querySelectorAll ('#tp-rows .tp-row')].find (r => r.querySelector ('.c-nm .t').textContent.trim().endsWith ('Slatt')); return r.querySelector ('.c-fv').classList.contains ('on'); });
  gate (favc.length === 1 && favc[0].args.join (',') === 'User,Slatt,true' && lit, '[10] FAVOURITE — the heart calls setFavourite(bank, name, true) and the row lights', `setFavourite(${favc[0] ? favc[0].args.join (', ') : '—'}) · lit ${lit}`);

  // [11] delete asks with a sheet
  const sl2 = await rowByName ('Monte Cristo'); const bb = await sl2.boundingBox(); await p.mouse.click (bb.x + 40, bb.y + bb.height / 2, { button: 'right' }); await wait (120);
  const ctxRows = await p.evaluate (() => [...document.querySelectorAll ('#tp-ctx .pi')].map (e => e.querySelector ('.nm').textContent.trim()));
  await p.evaluate (() => document.querySelector ('#tp-ctx .pi[data-a=del]').click()); await wait (120);
  const askText = await p.evaluate (() => document.getElementById ('tp-sheet').classList.contains ('on') ? document.querySelector ('#tp-sheet .tp-text').textContent.trim() : '');
  await p.click ('#tp-sh-ok'); await wait (300);
  const delc = await calls ('deletePresetFile'); const gone = await p.evaluate (() => ! [...document.querySelectorAll ('#tp-rows .tp-row')].some (r => r.querySelector ('.c-nm .t').textContent.trim().endsWith ('Monte Cristo')));
  gate (ctxRows.includes ('Delete') && /Delete.*Monte Cristo/.test (askText) && delc.length === 1 && /Monte Cristo\.terrain$/.test (delc[0].args[0]) && gone,
    '[11] DELETE ASKS — a sheet (never confirm()) → deletePresetFile(path) → the row is gone', `ctx ${ctxRows.join (' / ')} · asked "${askText}" · native ×${delc.length} · row gone ${gone}`);

  // [12] no panels
  const panels = await p.evaluate (() => window.__panels);
  gate (panels.length === 0, '[12] NO prompt()/confirm()/alert() ANYWHERE — WKWebView has no panels for them', panels.length ? panels.join (',') : 'none called');

  // [13] escape + outside
  await p.click ('#preset-save-btn'); await wait (80); await p.evaluate (() => document.querySelector ('#tp-ctx .pi[data-a=saveas]').click()); await wait (100);
  await p.keyboard.press ('Escape'); await wait (60); const s1 = await p.evaluate (() => document.getElementById ('tp-sheet').classList.contains ('on'));
  await p.keyboard.press ('Escape'); await wait (60); const b1 = await p.evaluate (() => document.getElementById ('tp-b').classList.contains ('on'));
  await p.click ('#preset-name'); await wait (80); await p.mouse.click (700, 500); await wait (60); const q1 = await p.evaluate (() => document.getElementById ('tp-q').classList.contains ('on'));
  gate (! s1 && ! b1 && ! q1, '[13] ESCAPE closes the sheet, then the browser; a mousedown outside closes the quick menu', `sheet ${s1} · browser ${b1} · quick ${q1}`);

  // [14] the header — the preset cluster is CENTRED and the two dead pills are gone
  {
    const h = await p.evaluate (() => {
      const cl = document.querySelector ('.header-preset'), hd = document.getElementById ('header');
      if (! cl || ! hd) return { ok: false, why: 'no .header-preset' };
      const r = cl.getBoundingClientRect(), hr = hd.getBoundingClientRect();
      const vis = id => { const e = document.getElementById (id); return !! (e && e.offsetParent !== null); };
      const nm = document.getElementById ('preset-name');
      /* fb631 — the experiment: the cluster's LEFT inset equals the gear's RIGHT inset, and the
         wordmark is the centred group instead */
      const gear = document.getElementById ('settings-btn').getBoundingClientRect(), wm = document.querySelector ('.header-left').getBoundingClientRect();
      return { leftInset: +(r.left - hr.left).toFixed (2), rightInset: +(hr.right - gear.right).toFixed (2),
               wmOff: Math.abs ((wm.left + wm.width / 2) - (hr.left + hr.width / 2)), eq: vis ('eq-btn'), dly: vis ('delay-btn'),
               syn: vis ('syn-btn'), size: nm ? getComputedStyle (nm).fontSize : '?' };
    });
    gate (h.leftInset != null && Math.abs (h.leftInset - h.rightInset) < 0.5 && h.wmOff <= 2 && ! h.eq && ! h.dly && h.syn,
      '[14] THE PRESET CLUSTER SITS AT THE GEAR\'S INSET, MIRRORED, AND THE WORDMARK IS CENTRED; EQ / DLY HAVE NO BUTTON',
      `cluster left inset ${h.leftInset}px vs gear right inset ${h.rightInset}px (must match) · wordmark centre offset ${h.wmOff == null ? '—' : h.wmOff.toFixed (1) + 'px'} (≤2)`
      + ` · EQ visible ${h.eq} · DLY visible ${h.dly} · SYN still visible ${h.syn} · name ${h.size}`);
  }

  // [15] selection is the WORDS, never a block
  {
    await openBrowser();
    await clickRow ('Slatt'); await wait (120);
    const g = await p.evaluate (() => {
      const row = document.querySelector ('#tp-rows .tp-row.sel'); if (! row) return null;
      const cs = getComputedStyle (row), nm = getComputedStyle (row.querySelector ('.c-nm'));
      const ty = getComputedStyle (row.querySelector ('.c-ty')), st = getComputedStyle (row.querySelector ('.c-st'));
      const other = document.querySelector ('#tp-rows .tp-row:not(.sel)');
      const rail = document.querySelector ('#tp-b .rail .ri.cur');
      return { bg: cs.backgroundColor, nm: nm.color, ty: ty.color, st: st.color,
               plain: other ? getComputedStyle (other.querySelector ('.c-nm')).color : '',
               railBg: rail ? getComputedStyle (rail).backgroundColor : '', railLine: rail ? getComputedStyle (rail).borderLeftColor : '' };
    });
    const clear = v => v === 'rgba(0, 0, 0, 0)' || v === 'transparent';
    gate (!! g && clear (g.bg) && g.nm === g.ty && g.ty === g.st && g.nm !== g.plain && clear (g.railBg),
      '[15] SELECTION IS THE WORDS — name, type and style go accent; no fill on the row OR the rail',
      g ? `row background ${g.bg} · name ${g.nm} · type ${g.ty} · style ${g.st} · an unselected name ${g.plain} · rail fill ${g.railBg}, rail line ${g.railLine}`
        : 'no selected row');
  }

  // [16] fb624 — the Load BUTTON keeps the browser open; only a double-click on a row closes it
  {
    await openBrowser();
    await clickRow ('Tectonic'); await wait (120);
    const before = await header();
    await p.click ('#tp-i-load'); await wait (300);
    const afterLoad = await p.evaluate (() => ({ open: document.getElementById ('tp-b').classList.contains ('on'),
                                                 h: document.getElementById ('preset-name').textContent.trim() }));
    await clickRow ('Anvil'); await wait (90); await clickRow ('Anvil'); await wait (300);
    const afterDbl = await p.evaluate (() => ({ open: document.getElementById ('tp-b').classList.contains ('on'),
                                                h: document.getElementById ('preset-name').textContent.trim() }));
    gate (afterLoad.open && afterLoad.h !== before && ! afterDbl.open && afterDbl.h === 'Lead - Anvil',
      '[16] THE LOAD BUTTON KEEPS THE BROWSER OPEN — only a double-click on a row closes it',
      `before "${before}" · after Load: open ${afterLoad.open}, header "${afterLoad.h}" · after a double-click: open ${afterDbl.open}, header "${afterDbl.h}"`);
  }

  // [17] fb624 — the inspector: the author under the name, no Effects/Author rows, filters moved in
  {
    await openBrowser();
    await clickRow ('Slatt'); await wait (150);
    const i = await p.evaluate (() => {
      const keys = [...document.querySelectorAll ('#tp-insp .kv .k')].map (e => e.textContent.trim());
      const au = document.getElementById ('tp-author');
      const sc = document.getElementById ('tp-iscroll');
      const chipsInside = !! (sc && sc.querySelector ('#tp-chips-type') && sc.querySelector ('#tp-chips-style'));
      const chipsAtTop = !! document.querySelector ('#tp-b > .chips');
      const acts = document.getElementById ('tp-acts');
      return { keys, author: au ? au.textContent.trim() : null, editable: !!(au && au.isContentEditable),
               chipsInside, chipsAtTop, scrolls: !!(sc && getComputedStyle (sc).overflowY === 'auto'),
               actsPinned: !!(acts && acts.parentElement && acts.parentElement.classList.contains ('insp')) };
    });
    gate (i.keys.join ('|') === 'Type|Style|Pack|Load|Size' && i.author === 'Max' && i.editable
          && i.chipsInside && ! i.chipsAtTop && i.scrolls && i.actsPinned,
      '[17] THE INSPECTOR — author under the name (editable), no Effects/Author rows, filters moved in, column scrolls',
      `kv rows ${i.keys.join (' / ')} · author "${i.author}" editable ${i.editable} · chips in the inspector ${i.chipsInside}, still at the top ${i.chipsAtTop} · scrolls ${i.scrolls} · Load pinned ${i.actsPinned}`);
  }

  // [18] fb625 — the header name CLOSES the browser instead of stacking a quick menu over it
  {
    await openBrowser();
    await p.click ('#preset-name'); await wait (200);
    const after = await p.evaluate (() => ({ browser: document.getElementById ('tp-b').classList.contains ('on'),
                                             quick: document.getElementById ('tp-q').classList.contains ('on') }));
    await p.click ('#preset-name'); await wait (200);
    const reopened = await p.evaluate (() => document.getElementById ('tp-q').classList.contains ('on'));
    gate (! after.browser && ! after.quick && reopened,
      '[18] THE HEADER NAME CLOSES THE BROWSER — it never stacks a quick menu on top of it',
      `while browsing, one click → browser ${after.browser}, quick menu ${after.quick} · a second click from the synth reopens the quick menu ${reopened}`);
  }

  // [19] fb626 — every preset surface clears the page's floating overlays, not just the browser
  {
    await p.keyboard.press ('Escape'); await wait (150); await p.keyboard.press ('Escape'); await wait (200);
    const cls = () => p.evaluate (() => document.documentElement.classList.contains ('tp-browsing'));
    const atRest = await cls();
    await p.click ('#preset-name'); await wait (200); const onQuick = await cls();
    await p.click ('#tp-q-browse'); await wait (280); const onBrowser = await cls();
    await p.click ('#preset-save-btn'); await wait (160);
    await p.evaluate (() => { const r = document.querySelector ('#tp-ctx .pi[data-a=saveas]'); if (r) r.click(); }); await wait (240);
    const onSheet = await cls();
    /* the attenuator is the one that bit him twice; prove the rule actually hides it */
    /* fb628 — every body-level fixed overlay, not just the attenuator: .sm-ul (the modulation
       UNDERLINE, white rails at z 2147483644) is the one that reached him three times. */
    const hidden = await p.evaluate (() => {
      const names = ['sm-att', 'sm-ul', 'sm-routes', 'sm-ghost', 'ti-card', 'mv-ext', 'warp-ext', 'filt-ext', 'mv-menu', 'mv-toast', 'samp-menu'];
      const left = [];
      for (const n of names) { const t = document.createElement ('div'); t.className = n; document.body.appendChild (t);
        if (getComputedStyle (t).display !== 'none') left.push (n); t.remove(); }
      /* and the preset system's own menus must SURVIVE the same rule */
      const mine = document.createElement ('div'); mine.className = 'pmenu tp-menu on'; document.body.appendChild (mine);
      const mineOk = getComputedStyle (mine).display !== 'none'; mine.remove();
      return { left, mineOk };
    });
    await p.keyboard.press ('Escape'); await wait (180); await p.keyboard.press ('Escape'); await wait (220);
    const backAtRest = await cls();
    gate (! atRest && onQuick && onBrowser && onSheet && hidden.left.length === 0 && hidden.mineOk && ! backAtRest,
      '[19] EVERY PRESET SURFACE STANDS THE PAGE’S FLOATING OVERLAYS DOWN (quick menu, browser, sheet)',
      `at rest ${atRest} · quick ${onQuick} · browser ${onBrowser} · sheet ${onSheet}`
      + ` · overlays still visible while up: ${hidden.left.length ? hidden.left.join (',') : 'none'}`
      + ` · the preset system's own .pmenu survives ${hidden.mineOk} · back at rest ${backAtRest}`);
  }

  // [20] fb628 — every column is a LANE: nothing can crowd Carries, a long name ellipsises
  {
    await openBrowser();
    const lanes = await p.evaluate (() => {
      const rows = [...document.querySelectorAll ('#tp-rows .tp-row:not(.off)')];
      if (! rows.length) return null;
      const worst = { gap: 1e9, over: 0 };
      for (const r of rows) {
        const nm = r.querySelector ('.c-nm'), au = r.querySelector ('.c-au'), car = r.querySelector ('.c-car');
        if (! nm || ! au || ! car) return { missing: true };
        const a = au.getBoundingClientRect(), c = car.getBoundingClientRect(), t = nm.querySelector ('.t');
        worst.gap = Math.min (worst.gap, c.left - a.right);        // author → carries clearance
        if (t && t.scrollWidth > t.clientWidth + 1) worst.over++;   // names that had to ellipsise
      }
      /* 🚨 the case Max named — "Alice in Wonderland … my name is coming very close to the
         wavetable emblem". No preset in the store is long enough to prove it, so FORCE it: shove a
         runaway name into the first row and demand the author does not move one pixel. */
      const r0 = rows[0], t0 = r0.querySelector ('.c-nm .t'), a0 = r0.querySelector ('.c-au');
      const before = a0.getBoundingClientRect().left, keep = t0.textContent;
      t0.textContent = 'Alice in Wonderland and the Very Long Preset Name That Keeps Going';
      const after = a0.getBoundingClientRect().left;
      const clipped = t0.scrollWidth > t0.clientWidth + 1;
      t0.textContent = keep;
      const heads = [...document.querySelectorAll ('#tp-b .hd [data-sort]')].map (e => e.dataset.sort);
      return { gap: worst.gap, ellipsised: worst.over, heads, rows: rows.length,
               shifted: Math.abs (after - before), clipped };
    });
    gate (!! lanes && ! lanes.missing && lanes.gap >= 6 && lanes.heads.join () === 'name,author,carries,type,style'
          && lanes.clipped && lanes.shifted < 0.5,
      '[20] EVERY COLUMN IS A LANE — a runaway name ellipsises instead of pushing, and Carries keeps its clearance',
      lanes ? `narrowest author→Carries gap ${lanes.gap == null ? '—' : lanes.gap.toFixed (1)}px across ${lanes.rows} rows (must be ≥ 6)`
              + ` · a 64-char name: ellipsised ${lanes.clipped}, author moved ${lanes.shifted.toFixed (1)}px (must be 0)`
              + ` · columns ${lanes.heads.join (' / ')}`
            : 'no rows');
  }


  // ── fb629 — THE HEADER TAKES THE PAGE'S COLOUR ────────────────────────────────────────────────
  // Max: "I'm tired of having the header be separate colours... it should blend in with everything."
  // The claim is about PIXELS, so the bar reads pixels. It samples an EMPTY column (x 180-270: past
  // "TERRAIN V1", short of the preset "+") on both sides of the y=44 line and demands they match.
  // The shipped plugin is served with data-theme="dark" injected by PluginEditor.cpp; the harness is
  // not, so the theme is set here — without it every html-level token reads LIGHT and the bar lies.
  // WHAT IS BEING COMPARED IS THE GROUND. At rest the header band is measured against the synth
  // page's DECLARED background rather than against the pixels under it: the OSC panel's own fill
  // begins immediately below y=44, so a pixel-to-pixel read there measures content, not a seam.
  // While browsing there IS empty glass to compare against, so that side is pixel-to-pixel.
  const rgb = (css) => (css.match (/\d+(\.\d+)?/g) || []).slice (0, 3).map (Number);
  const SHOT = path.join (require ('os').tmpdir(), 'tp_seam_' + process.pid + '.png');
  /* fb630 — the ground under the synth view is #plugin's radial vignette painted ONCE across header
     and page, so "same colour" is asked in the SAME COLUMN on both sides of y=44: the header band
     (y 24-40) against the page's bare top padding (y 46-56, above every device at y=56). Two columns:
     the far left where the vignette has faded flat, and x 300-330 where it is still rising. */
  const seam = async (hx0, hx1, bx0, bx1, by0 = 46, by1 = 56) => {
    await p.screenshot ({ path: SHOT, clip: { x: 0, y: 0, width: 820, height: 120 } });
    const img = decode (fs.readFileSync (SHOT));
    const above = band (img, 24, 40, hx0, hx1);
    const below = band (img, by0, by1, bx0, bx1);
    return { above, below, d: dist (above, below) };
  };
  {
    // bar [20] leaves the browser up and Escape does not reach it from here — press the product's
    // own close control, then PROVE we are at rest before measuring anything.
    await p.evaluate (() => { const x = document.getElementById ('tp-close'); if (x) x.click(); });
    await wait (300); await p.keyboard.press ('Escape'); await wait (220);
    await p.evaluate (() => { document.documentElement.setAttribute ('data-theme', 'dark');
                              document.body.classList.add ('ti-syn-open'); });
    await wait (280);
    const atRest = await p.evaluate (() => ! document.documentElement.classList.contains ('tp-glassup')
                                        && ! document.getElementById ('tp-b').classList.contains ('on'));
    const rest = await p.evaluate (() => {
      const cs = getComputedStyle (document.getElementById ('header'));
      const pg = document.querySelector ('#syn-panel .ti-syn-page');
      return { bg: cs.backgroundColor, border: cs.borderBottomWidth,
               plugin: getComputedStyle (document.getElementById ('plugin')).backgroundImage,
               panel:  getComputedStyle (document.getElementById ('syn-panel')).backgroundColor,
               page:   pg ? getComputedStyle (pg).backgroundColor : '?' };
    });
    const sEdge = await seam (180, 270, 180, 270);     // past "TERRAIN V1", short of the "+"
    const sMid  = await seam (540, 570, 540, 570);     // between the preset "›" and SYN
    const sRest = sEdge.d > sMid.d ? sEdge : sMid;      // report the worse of the two
    await openBrowser(); await wait (320);
    const brow = await p.evaluate (() => {
      const cs = getComputedStyle (document.getElementById ('header'));
      const gl = getComputedStyle (document.getElementById ('tp-b'));
      return { bg: cs.backgroundColor, blur: cs.backdropFilter || cs.webkitBackdropFilter,
               glassBg: gl.backgroundColor, glassBlur: gl.backdropFilter || gl.webkitBackdropFilter,
               flag: document.documentElement.classList.contains ('tp-glassup') };
    });
    const sBrow = await seam (180, 270, 350, 600, 50, 70); // browser: header vs empty glass between search and the count
    await p.keyboard.press ('Escape'); await wait (220);
    await p.evaluate (() => document.documentElement.removeAttribute ('data-theme'));
    try { fs.unlinkSync (SHOT); } catch (e) {}
    const clear = /rgba\(0, 0, 0, 0\)|transparent/.test (rest.bg);
    const onePainter = /radial-gradient/.test (rest.plugin) && /rgba\(0, 0, 0, 0\)/.test (rest.panel) && /rgba\(0, 0, 0, 0\)/.test (rest.page);
    gate (atRest && clear && rest.border === '0px' && onePainter && sEdge.d <= 2 && sMid.d <= 2
          && brow.flag && brow.bg === brow.glassBg && brow.blur === brow.glassBlur && sBrow.d <= 4,
      '[21] THE HEADER HAS NO COLOUR OF ITS OWN — it takes the page, then the glass',
      `at rest ${atRest} · own background ${rest.bg} · border ${rest.border}`
      + ` · one painter: #plugin ${/radial-gradient/.test (rest.plugin) ? 'carries the vignette' : 'HAS NO VIGNETTE'}, #syn-panel ${rest.panel}, .ti-syn-page ${rest.page}`
      + ` · seam left ${hex (sEdge.above)}|${hex (sEdge.below)} Δ${sEdge.d.toFixed (2)}, right ${hex (sMid.above)}|${hex (sMid.below)} Δ${sMid.d.toFixed (2)} (both ≤2, same column each side of y=44)`
      + ` · browsing: tp-glassup ${brow.flag}, header ${brow.bg} vs glass ${brow.glassBg}`
      + ` ${brow.bg === brow.glassBg ? 'SAME' : 'DIFFER'}, blur ${brow.blur === brow.glassBlur ? 'SAME' : 'DIFFER'}`
      + ` · seam Δ${sBrow.d.toFixed (2)} (must be ≤4)`);
  }

  // ── fb630 — NO MARK. Max: "I actually want to see what it looks like with no logo." ────────────
  // The wordmark stands alone at the header's left padding, on the centreline. The mark is parked in
  // Design/mark/ — if it comes back, this bar flips back to fb629's shape (readable size, centred,
  // theme swap); until then a mark in the header is a regression.
  {
    const m = await p.evaluate (() => {
      const left = document.querySelector ('#header .header-left'), nm = document.querySelector ('.brand-name'),
            h = document.getElementById ('header');
      if (! left || ! nm || ! h) return { missing: true };
      const marks = left.querySelectorAll ('img, svg, .brand-logo').length;
      const n = nm.getBoundingClientRect(), hr = h.getBoundingClientRect();
      const lr = left.getBoundingClientRect();
      return { marks, xOff: +Math.abs ((lr.left + lr.width / 2) - (hr.left + hr.width / 2)).toFixed (2),
               dCentre: +Math.abs ((n.top + n.height / 2) - (hr.top + hr.height / 2)).toFixed (2) };
    });
    gate (!! m && ! m.missing && m.marks === 0 && m.xOff <= 2 && m.dCentre < 1,
      '[22] THE HEADER CARRIES THE WORDMARK ALONE — no mark, dead centre, on the centreline',
      m && ! m.missing
        ? `marks in the header: ${m.marks} (must be 0) · wordmark group off the window centre by ${m.xOff}px (≤2) · centre off the bar's by ${m.dCentre}px`
        : 'header or wordmark missing');
  }


  // ── fb630 — THE VALUE LIVES INSIDE THE RING ─────────────────────────────────────────────────
  // Max: "take what we already did for the effect rack cards and just put them inside all of the
  // engines... use your design skill: all of them perfectly inside the circle. None outside. But not
  // too small — people need to be able to read it." Three numbers, three assertions: every visible
  // knob in every engine (and the noise strip) has a value; every glyph box's four corners lie inside
  // the ring's inner circle; no value is set below the rack's own smallest size. And the row did not
  // grow — the fixed-positions law.
  {
    const r = await p.evaluate (() => {
      const dev = document.getElementById ('osc-a-device');
      const all = ['engine-sample', 'engine-granular', 'engine-geode', 'engine-fm', 'engine-harm', 'engine-modal', 'swapped', 'uni-page'];
      const modes = [[], ['engine-sample'], ['engine-granular'], ['engine-geode'], ['engine-fm'], ['engine-harm'], ['engine-modal'], ['swapped'], ['uni-page']];
      const out = { seen: 0, noVal: [], outside: [], small: [], sizes: {} };
      const measure = (root, tag) => root.querySelectorAll ('.knob').forEach (k => {
        const ring = k.querySelector ('.knob-ring'); if (! ring || ! ring.offsetParent || ! ring.offsetWidth) return;
        const id = k.dataset.syn || (k.querySelector ('.knob-label') || {}).textContent || '?';
        out.seen++;
        const kv = ring.querySelector ('.kv');
        if (! kv || ! kv.textContent.trim()) { out.noVal.push (tag + ':' + id); return; }
        const rr = ring.getBoundingClientRect(), cx = rr.left + rr.width / 2, cy = rr.top + rr.height / 2, R = rr.width / 2 - 3;   // inside the 2px arc at r=10
        const fs = parseFloat (getComputedStyle (kv).fontSize); out.sizes[fs] = (out.sizes[fs] || 0) + 1;
        if (fs < 5.8) out.small.push (tag + ':' + id + '@' + fs);
        const nodes = kv.classList.contains ('st') ? [...kv.querySelectorAll ('i')] : [kv];
        for (const n of nodes) { const rg = document.createRange(); rg.selectNodeContents (n); const b = rg.getBoundingClientRect();
          let worst = 0; for (const [x, y] of [[b.left, b.top], [b.right, b.top], [b.left, b.bottom], [b.right, b.bottom]]) worst = Math.max (worst, Math.hypot (x - cx, y - cy));
          if (worst > R + 0.5) { out.outside.push (tag + ':' + id + ' "' + kv.textContent.trim() + '" ' + worst.toFixed (1) + '>' + R.toFixed (1)); break; } }
      });
      for (const m of modes) { all.forEach (c => dev.classList.remove (c)); m.forEach (c => dev.classList.add (c)); measure (dev, m.join ('+') || 'wt'); }
      all.forEach (c => dev.classList.remove (c));
      measure (document.getElementById ('noise-mod'), 'noise');
      const row = dev.querySelector ('.wt-knob-wrap .osc-knobs');
      out.rowH = row ? +row.getBoundingClientRect().height.toFixed (1) : -1;
      return out;
    });
    gate (r.seen >= 50 && r.noVal.length === 0 && r.outside.length === 0 && r.small.length === 0 && r.rowH === 38,
      '[23] EVERY ENGINE KNOB CARRIES ITS VALUE INSIDE THE RING — inside the circle, readable, and nothing moved',
      `${r.seen} visible knobs across 9 views + noise · without a value: ${r.noVal.length ? r.noVal.slice (0, 4).join (', ') : 'none'}`
      + ` · glyphs outside the circle: ${r.outside.length ? r.outside.slice (0, 3).join (' | ') : 'none'}`
      + ` · below 5.8px: ${r.small.length ? r.small.slice (0, 3).join (', ') : 'none'} · sizes ${JSON.stringify (r.sizes)} · knob row ${r.rowH}px (must stay 38)`);
  }

  // ── fb630 — what the readouts SAY ───────────────────────────────────────────────────────────
  {
    const r = await p.evaluate (() => {
      const f = window.__fmtRing, o = {};
      o.wt16  = f ('SYN_OSC_A_WT_FRAME', 1);                             // the bank default, before any push: the last of 16
      window.onWtFrames (0, 128);                                        // the editor timer's push: a Terra table
      o.wt128a = f ('SYN_OSC_A_WT_FRAME', 0); o.wt128z = f ('SYN_OSC_A_WT_FRAME', 1); o.wt128m = f ('SYN_OSC_A_WT_FRAME', 0.5);
      const rg = document.querySelector ('#syn-panel .knob[data-syn="SYN_OSC_A_WT_FRAME"] .knob-ring');
      o.live = rg && rg.__kv ? rg.__kv.textContent.replace (/\s+/g, '') : '?';   // the live ring, repainted by the push
      o.liveWant = rg ? String (1 + Math.round ((rg.__n || 0) * 127)) : '?';   // from what the knob actually holds
      o.panL = f ('SYN_OSC_A_PAN', 0); o.panC = f ('SYN_OSC_A_PAN', 0.5); o.panR = f ('SYN_OSC_A_PAN', 1);
      o.semi = f ('SYN_OSC_A_SEMI', 1); o.oct = f ('SYN_OSC_A_OCT', 0); o.key = f ('SYN_OSC_A_GRAIN_KEY', 0.5); o.warp = f ('SYN_OSC_A_WARP_AMOUNT', 0.48);
      o.scan = f ('SYN_OSC_A_GRAIN_SCAN', 0.5); o.ratio = f ('SYN_OSC_A_FM_RATIO', 0); o.ratio2 = f ('SYN_OSC_A_FM_RATIO', Math.sqrt (2.25 / 15.75));
      o.longest = 0; ['_WT_FRAME','_PAN','_SEMI','_OCT','_CENT','_GRAIN_KEY','_GRAIN_SCAN','_GRAIN_PITCH','_GRAIN_SIZE','_GRAIN_DENSITY','_HARM_COUNT','_FM_RATIO','_WARP_AMOUNT']
        .forEach (t => { for (let i = 0; i <= 20; ++i) { const v = f ('SYN_OSC_A' + t, i / 20); if (/[.a-zA-Z]{4,}|ms|Hz|%|[LR]\d/.test (v)) o.bad = (o.bad || '') + t + '=' + v + ' '; o.longest = Math.max (o.longest, v.length); } });
      const nz = [...document.querySelectorAll ('#noise-mod .noise-knobs .knob-ring .kv')].map (e => e.textContent.trim());
      o.noise = nz.join ('|');
      const lab = document.querySelector ('#syn-panel .knob[data-syn="SYN_OSC_A_GEODE_DISTILL"] .knob-label');
      o.shape = lab ? lab.textContent : '?';
      window.onWtFrames (0, 16);
      return o;
    });
    const shapes = ['Sine','Square','Saw','Triangle','Pulse','Hollow','Organ','Half','Vowel','Bright','Metal'];
    gate (r.wt16 === '16' && r.wt128a === '1' && r.wt128z === '128' && r.wt128m === '65'
          && r.live === r.liveWant
          && r.panL === '100' && r.panC === '0' && r.panR === '100' && r.semi === '12' && r.oct === '-3' && r.key === 'Chd'
          && r.warp === '48' && r.scan === '0' && r.ratio === '1/4' && r.ratio2 === '5/2'
          && ! r.bad && r.longest <= 4
          && /^\d+\|\d+\|\d+$/.test (r.noise) && shapes.indexOf (r.shape) >= 0,
      '[24] THE READOUTS ARE BARE NUMBERS — the frame from the live count, distance-from-centre on bipolar arcs, no units, no decimals, fractions only for the FM ratio',
      `WT Pos on 16 frames at full: ${r.wt16} → after a 128-frame push ${r.wt128a} … ${r.wt128m} … ${r.wt128z}, live ring "${r.live}" (must be ${r.liveWant})`
      + ` · pan ${r.panL}/${r.panC}/${r.panR} (must be 100/0/100) · semi ${r.semi} · oct ${r.oct} · key ${r.key} · warp ${r.warp} · scan-centre ${r.scan} · ratio ${r.ratio}, ${r.ratio2}`
      + ` · forbidden shapes across 13 knobs × 21 values: ${r.bad || 'none'} · longest ${r.longest} glyphs (≤4) · noise ${r.noise} (bare|bare|bare) · Shape's label "${r.shape}"`);
  }

  // ── fb630 — the native capture strip hears about the glass ─────────────────────────────────
  {
    const r = await p.evaluate (() => {
      const calls = window.__gateCalls.filter (c => c.fn === 'setBrowserGlass');
      document.documentElement.setAttribute ('data-theme', 'dark');   // the last open happened under dark ([21])
      const mb = (getComputedStyle (document.documentElement).getPropertyValue ('--menu-bg') || '').match (/[\d.]+/g).map (Number);
      const gb = (getComputedStyle (document.getElementById ('plugin')).backgroundColor || '').match (/[\d.]+/g).map (Number);
      const a = mb[3], mix = i => Math.round (mb[i] * a + gb[i] * (1 - a)), hx = v => v.toString (16).padStart (2, '0');
      const ons = calls.filter (c => c.args[0] === '1');
      const out = { n: calls.length, on: ons[ons.length - 1], off: calls.filter (c => c.args[0] === '0').length,
                    expect: 'ff' + hx (mix (0)) + hx (mix (1)) + hx (mix (2)) };
      document.documentElement.removeAttribute ('data-theme');
      return out;
    });
    gate (r.n >= 2 && !! r.on && /^ff[0-9a-f]{6}$/.test (r.on.args[1]) && r.on.args[1] === r.expect && r.off >= 1,
      '[25] THE NATIVE CAPTURE STRIP IS TOLD THE GLASS — composited from the live tokens on open, dropped on close',
      `setBrowserGlass called ${r.n}× · on open: ${r.on ? r.on.args.join (',') : 'NEVER'} (expected 1,${r.expect} = --menu-bg over #plugin's ground) · off calls ${r.off}`);
  }

  await b.close();
  console.log (`  ${pass} pass, ${fail} fail` + (errors.length ? ` · page errors: ${errors.join (' | ')}` : ''));
  process.exit (fail ? 1 : 0);
})().catch (e => {
  // a bar went red and the furniture under it could not be driven any further (a covered glass cannot be clicked):
  // the tally is the verdict, the crash is the detail
  ++fail; console.log ('  FAIL  THE RUN COULD NOT CONTINUE PAST A RED BAR\n        ' + String (e && e.message || e).slice (0, 200));
  console.log (`  ${pass} pass, ${fail} fail`); process.exit (1); });
