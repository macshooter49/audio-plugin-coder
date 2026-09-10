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
//   1  THE HEADER NAMES THE PATCH — getPresetMeta → "Terra - Glacier"
//   2  THE QUICK MENU — click the name: banks · Favourites · Import bank…; the current bank's list
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
//
//  MUTATION CONTROLS
//    TP_MUT=zorder   #syn-panel is raised over the browser → [3] must go RED (the hit test)
//    TP_MUT=lanes    the pre-fb628 world: .sm-ul reaches over the preset surfaces again and the
//                    author's lane loses its clearance → [19] and [20] must both go RED
//    TP_MUT=nopush   the courier loads but never pushes onPatchLoaded (the pre-fb620 world) →
//                    [6] and [7] must go RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require ('path');
const puppeteer = require ('puppeteer-core');
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
  gate (await header() === 'Terra - Glacier', '[1] THE HEADER NAMES THE PATCH (getPresetMeta)', 'reads "' + await header() + '"');

  // [2] quick menu
  await p.click ('#preset-name'); await wait (150);
  const q = await p.evaluate (() => { const m = document.getElementById ('tp-q'); const banks = [...m.querySelectorAll ('#tp-q-banks .pi .nm')].map (e => e.textContent.trim());
    const secs = [...m.querySelectorAll ('#tp-q-list .ps')].map (e => e.textContent.trim()); const cur = m.querySelector ('#tp-q-list .pi.cur .nm'); const r = m.getBoundingClientRect();
    return { on: m.classList.contains ('on'), banks, secs, cur: cur ? cur.textContent.trim() : null, inside: r.left >= 0 && r.top >= 40 && r.right <= 820 && r.bottom <= 656, box: [r.left | 0, r.top | 0, r.width | 0, r.height | 0].join ('×') }; });
  const qh = await centreIn ('#tp-q');
  gate (q.on && q.banks.join ('|') === 'Terra|User|Favourites|Import bank…' && q.secs.join ('|') === 'Bass|Lead|Pad' && q.cur === 'Glacier' && q.inside && qh.ok,
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
  const hooks = await p.evaluate (() => { window.__hookCalls = {}; const names = ['__tiSeedMacroNames', '__tiPullLfoShapes', '__tiPullDynEnvs', '__tiModRestoreStrict', '__tiPullArpLanes', '__tiRestoreNoiseSel'];
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
  gate (ld.length >= 1 && ld[ld.length - 1].args[0] === '/tmp/Banks/Terra/Cirrus.terrain' && after.h === 'Terra - Cirrus' && ! after.open && ! after.frozen && hookOk,
    '[6] 🚨 A LOAD IS ONE CALL AND THE PAGE HEARS IT ONCE — loadPatchFile(path) → onPatchLoaded → header, glass closed, painters awake, every re-seed hook fired once',
    `loadPatchFile ×${ld.length} (${ld.length ? ld[ld.length - 1].args[0] : '—'}) · header "${after.h}" · browser open ${after.open} · frozen ${after.frozen} · hooks present ${hooks.join (',')} · fired ${JSON.stringify (after.hooks)}`);

  // [7] arrows
  await p.click ('#preset-next'); await wait (200); const hN = await header(); const ldN = await calls ('loadPatchFile');
  await p.click ('#preset-prev'); await wait (200); const hP = await header();
  gate (ldN.length >= 2 && ldN[ldN.length - 1].args[0] === '/tmp/Banks/Terra/Tectonic.terrain' && hN === 'Terra - Tectonic' && hP === 'Terra - Cirrus',
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
  gate (menuRows.join ('|') === 'Save(off)|Save as…|Export preset…|Init preset' && sheetOn && sv.length === 1 && sv[0].args[0] === 'User' && svm.name === 'Gate Pad' && svm.type === 'Keys' && /Wide/.test (svm.styles) && /Dark/.test (svm.styles) && svm.note === 'Written in the save sheet.' && h8 === 'User - Gate Pad'
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
      return { off: Math.abs ((r.left + r.width / 2) - (hr.left + hr.width / 2)), eq: vis ('eq-btn'), dly: vis ('delay-btn'),
               syn: vis ('syn-btn'), size: nm ? getComputedStyle (nm).fontSize : '?' };
    });
    gate (h.off != null && h.off <= 2 && ! h.eq && ! h.dly && h.syn,
      '[14] THE PRESET CLUSTER IS CENTRED, AND EQ / DLY HAVE NO BUTTON (their code stays)',
      `centre offset ${h.off == null ? '—' : h.off.toFixed (1) + 'px'} (must be ≤ 2) · EQ visible ${h.eq} · DLY visible ${h.dly} · SYN still visible ${h.syn} · name ${h.size}`);
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
    gate (afterLoad.open && afterLoad.h !== before && ! afterDbl.open && afterDbl.h === 'Terra - Anvil',
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
    gate (i.keys.join ('|') === 'Type|Style|Bank|Load|Size' && i.author === 'Max' && i.editable
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

  await b.close();
  console.log (`  ${pass} pass, ${fail} fail` + (errors.length ? ` · page errors: ${errors.join (' | ')}` : ''));
  process.exit (fail ? 1 : 0);
})().catch (e => {
  // a bar went red and the furniture under it could not be driven any further (a covered glass cannot be clicked):
  // the tally is the verdict, the crash is the detail
  ++fail; console.log ('  FAIL  THE RUN COULD NOT CONTINUE PAST A RED BAR\n        ' + String (e && e.message || e).slice (0, 200));
  console.log (`  ${pass} pass, ${fail} fail`); process.exit (1); });
