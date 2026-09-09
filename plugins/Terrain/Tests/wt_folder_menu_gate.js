// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_folder_menu_gate.js — fb606: THE FOLDER MENU COMES OUT FROM BEHIND THE BROWSER.
//
//    node Tests/wt_folder_menu_gate.js [page.html]
//
//  Max: "delete + locate folder is behind the menu AKA impossible to get to lol."
//
//  THE DEFECT, measured before the fix: `.tpb-panel` (the two-pane glass browser) and
//  `#syn-panel .syn-ctx-menu.act` (the menu it opened) BOTH carried `z-index: 2147483646`, and at
//  EQUAL z-index the later DOM node wins. The panel is document.body.appendChild'ed when it opens;
//  #syn-panel is markup near the top of <body>. The panel is therefore always the later sibling,
//  and the menu always lost.
//
//  🚨 WHY THIS GATE HIT-TESTS INSTEAD OF COMPARING z-index. Raising the number could not have
//  fixed the bug: the menu is scoped INSIDE #syn-panel and trapped in that element's stacking
//  context — "a huge z inside an auto-z branch loses to any positive sibling", which the page
//  itself documents. So a gate that reads the two z-index values learns NOTHING: they were equal
//  before the fix and the menu was still buried, and a gate could have been written that passed on
//  a value while the owner could not click the thing. Bar [3] therefore asks the only question
//  that means anything — CAN A CLICK LAND ON IT? — via document.elementFromPoint at the menu's own
//  coordinates. Bar [4] prints what a z-index-only gate would have concluded, side by side, so
//  the difference is on the record rather than in a comment.
//
//  THE BARS
//   0  THE PANEL ACTUALLY LAID OUT — nothing below may be asserted on a page that never rendered
//   1  🚨 THE SEAM: the REAL C++ payloads (emitted by Tests/wt_folder_scan_cert --emit) are the
//      ones the shipping JS reads. A C++ shape the JS reader does not accept is green on both
//      sides and broken in the plugin, and no single-language gate can see it.
//      PENDING (not FAIL) when the payload files are absent; the line says where it looked.
//   2  RIGHT-CLICKING A FOLDER OPENS A MENU — with Locate Folder, and Remove Folder on a folder
//      the user registered
//   3  🚨 THE MENU IS HIT-TESTABLE AT ITS OWN COORDINATES — elementFromPoint at the menu's centre
//      AND at every row's centre returns the menu (or its own child), never .tpb-panel
//   4  IT ESCAPED THE PANEL'S STACKING CONTEXT — the menu is not a descendant of #syn-panel; the
//      z-index numbers and the DOM order are printed beside what a z-only gate would have said
//   5  THE MENU NEVER CUTS OFF — right-click in the far bottom-right corner; the whole box stays
//      inside the viewport (the .pmenu clamp, the house law)
//   6  A ROW ACTUALLY REACHES ITS HANDLER — click at the row's own screen coordinates through
//      elementFromPoint; the native the row is wired to must fire
//   7  REMOVE IS NEVER OFFERED ON FACTORY CONTENT OR ON A SUBFOLDER — both may still be LOCATED
//
//  MUTATION CONTROLS
//    TPBMENU_MUTATE=synmenu   route the folder menu back through window.__synShowMenu — the EXACT
//                             pre-fb606 code path — so the menu is built inside #syn-panel again.
//                             [3] must go RED while [4]'s z-index numbers stay identical: that
//                             pair IS the argument for hit-testing.
//    TPBMENU_MUTATE=noclamp   open the menu at the raw pointer position with no clamp → [5] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs   = require ('fs');
const path = require ('path');
const puppeteer = require ('puppeteer-core');

const PAGE = process.argv[2] || path.resolve (__dirname, '../Source/ui/public/index.html');
const MUT  = process.env.TPBMENU_MUTATE || '';

// ── the REAL C++ payloads, if wt_folder_scan_cert --emit has produced them ────────────────────
const PAYDIR = process.env.TPB_PAYLOAD
  || '/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/wtfolders/payload';
const readJson = (f) => { try { return fs.readFileSync (path.join (PAYDIR, f), 'utf8'); } catch (e) { return null; } };
const IMPORTS_REAL = readJson ('imports.json');
const MANAGED_REAL = readJson ('managed.json');

// the synthetic fallback: enough of a registry to open a browser with one user folder that has
// subfolders, so bars [2]–[7] still run when the cert has not been built.
const IMPORTS_FAKE = JSON.stringify ({ kind: 1, files: [], folders: [
  { name: 'MASTER', path: '/tmp/MASTER', kind: 'user', count: 3,
    subs: ['Digital/Deep', 'Foundation'],
    items: [{ name: 'loose', path: '/tmp/MASTER/loose.wav', rel: '' },
            { name: 'Terra Bell', path: '/tmp/MASTER/Foundation/Terra Bell.wav', rel: 'Foundation' },
            { name: 'Bitcrush 12', path: '/tmp/MASTER/Digital/Deep/Bitcrush 12.wav', rel: 'Digital/Deep' }] }] });
const MANAGED_FAKE = JSON.stringify ({ root: '/tmp/Wavetables', exists: true, total: 0, items: [] });

const IMPORTS = IMPORTS_REAL || IMPORTS_FAKE;
const MANAGED = MANAGED_REAL || MANAGED_FAKE;

let pass = 0, fail = 0, pend = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail;
  console.log (`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };
const pending = (name, detail) => { ++pend; console.log (`  PEND  ${name}\n        ${detail}`); };

// ── the JUCE bridge stub (the fm_wtpage_gate / harm_wtmenu_gate idiom) ────────────────────────
// Without it the panel never lays out and every measurement reads 0, which a naive gate calls a pass.
const STUB = (IMPORTS, MANAGED) => {
  window.__gateCalls = [];
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  const nf = (n) => (...a) => new Promise (r => {
    window.__gateCalls.push ({ fn: n, args: a.map (String) });
    if (n === 'listWtImports') return r (IMPORTS);
    if (n === 'listImports')   return r (MANAGED);
    if (/getPresets/i.test (n)) return r ('[]');
    if (/Json|JSON/.test (n))   return r ('{}');
    r (0); });
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk, getNativeFunction: nf,
    backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
};

(async () => {
  console.log ('══ wt_folder_menu_gate (fb606) — THE FOLDER MENU MUST NOT RENDER BEHIND THE BROWSER ══');
  console.log ('   payload: ' + (IMPORTS_REAL ? 'REAL C++ (' + PAYDIR + ')' : 'SYNTHETIC fallback — run '
             + 'wt_folder_scan_cert --emit ' + PAYDIR + ' for the seam bar'));
  console.log ('   mutation: ' + (MUT || '(none)'));

  const b = await puppeteer.launch ({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport ({ width: 1120, height: 760, deviceScaleFactor: 1 });
  await p.evaluateOnNewDocument (STUB, IMPORTS, MANAGED);
  await p.goto ('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise (r => setTimeout (r, 1500));
  await p.evaluate (() => { const sp = document.getElementById ('syn-panel');
                            if (sp) sp.style.display = 'block'; window.dispatchEvent (new Event ('resize')); });
  await new Promise (r => setTimeout (r, 2400));

  // ── install the mutation INSIDE the page, over the shipping implementation ──────────────────
  await p.evaluate ((MUT) => {
    window.__mutFired = 'none';
    if (MUT === 'synmenu') {
      // the EXACT pre-fb606 path: build the folder actions on the #syn-panel-scoped menu
      window.__tpbFolderMenu = function (title, rows, x, y) {
        if (! window.__synShowMenu) return null;
        window.__synShowMenu (title, rows.map (function (r) {
          return { label: r.label, onPick: r.onPick }; }), x, y);
        return document.getElementById ('syn-ctx-menu'); };
      window.__mutFired = 'synmenu — the folder menu is __synShowMenu again (#syn-panel-scoped)';
    } else if (MUT === 'noclamp') {
      const real = window.__tpbFolderMenu;
      window.__tpbFolderMenu = function (title, rows, x, y) {
        const m = real (title, rows, x, y);
        if (m) { m.style.left = x + 'px'; m.style.top = y + 'px'; }   // the clamp, undone
        return m; };
      window.__mutFired = 'noclamp — the menu is placed at the raw pointer position';
    }
  }, MUT);

  // ── open the wavetable browser through the SHIPPING opener ──────────────────────────────────
  const opened = await p.evaluate (() => new Promise ((res) => {
    try { window.openWtSelectMenu ('a', { clientX: 300, clientY: 220 }); } catch (e) { return res ('threw: ' + e.message); }
    setTimeout (() => res (document.querySelector ('.tpb-panel') ? 'ok' : 'no .tpb-panel'), 700); }));

  // ── [0] ────────────────────────────────────────────────────────────────────────────────────
  const box = await p.evaluate (() => { const el = document.querySelector ('.tpb-panel');
    if (! el) return null; const r = el.getBoundingClientRect();
    const cats = el.querySelector ('.tpb-pane');
    return { w: r.width, h: r.height, rows: cats ? cats.children.length : 0,
             labels: cats ? [...cats.children].map (c => c.textContent.trim()) : [] }; });
  gate (!! box && box.w > 100 && box.h > 100 && box.rows > 0, '[0] THE PANEL ACTUALLY LAID OUT',
    box ? `.tpb-panel ${Math.round (box.w)}x${Math.round (box.h)} with ${box.rows} categories: `
          + box.labels.map (s => s.replace (/\s+/g, ' ')).join (' · ').slice (0, 220)
        : `openWtSelectMenu produced no .tpb-panel (${opened}) — every bar below would measure nothing`);

  // ── [1] THE SEAM ───────────────────────────────────────────────────────────────────────────
  if (! IMPORTS_REAL) {
    pending ('[1] THE C++ PAYLOAD AND THE JS READER AGREE',
      'no imports.json / managed.json under ' + PAYDIR + ' — build and run\n        '
      + '`wt_folder_scan_cert --emit ' + PAYDIR + '` and this bar arms itself. The bars below ran '
      + 'against the synthetic registry instead.');
  } else {
    const real = JSON.parse (IMPORTS_REAL);
    const wantFolders = (real.folders || []).map (f => f.name);
    const wantFactory = ((real.factory && real.factory.cats) || []).map (c => c.name);
    const seen = (box ? box.labels : []).map (s => s.replace (/\s*\d+\s*$/, '').trim());   // rows carry a count
    const missF = wantFolders.filter (n => ! seen.some (s => s === n || s.indexOf (n) >= 0));
    const missX = wantFactory.filter (n => ! seen.some (s => s === n || s.indexOf (n) >= 0));
    // an empty or unnamed category must not exist (Max: "delete anything that doesn't have a table")
    const junk = await p.evaluate (() => { const c = document.querySelector ('.tpb-panel .tpb-pane');
      if (! c) return []; return [...c.children].map (r => r.textContent.trim())
        .filter (t => ! t || /^Folder\b/.test (t) || /\b0$/.test (t)); });
    const ok = missF.length === 0 && missX.length === 0 && junk.length === 0;
    let d = 'C++ emitted ' + wantFolders.length + ' registered folder(s) [' + wantFolders.join (', ') + '] and '
          + wantFactory.length + ' factory categor(y|ies) [' + wantFactory.join (', ') + ']';
    if (missF.length) d += '\n        REGISTERED FOLDER MISSING FROM THE BROWSER: ' + missF.join (' · ');
    if (missX.length) d += '\n        FACTORY CATEGORY MISSING FROM THE BROWSER: ' + missX.join (' · ')
      + '\n        (the payload puts them under factory.cats — an OBJECT with a cats array. The JS does '
      + '`(reg.folders||[]).concat(reg.factory||[])`, which appends that object as one nameless folder.)';
    if (junk.length) d += '\n        EMPTY / UNNAMED CATEGORY RENDERED: ' + junk.join (' · ');
    if (ok) d += ' — every one of them is a category in the browser, and no empty one is';
    gate (ok, '[1] THE C++ PAYLOAD AND THE JS READER AGREE', d);
  }

  // ── right-click the first category that carries a delKind (a real folder on disk) ───────────
  // ⚠️ .pmenu is the project's RECYCLED menu class — a reverb/delay preset menu wears it too.
  // Always take the LAST one in the DOM: the one this gate just opened.
  const rc = await p.evaluate (() => {
    const catsEl = document.querySelector ('.tpb-panel .tpb-pane');
    if (! catsEl) return { err: 'no category pane' };
    // The shipping handler reads `cat.delKind`, which this gate cannot see from outside — so find
    // a row that has one by right-clicking rows until a menu appears. PREFER the richest menu
    // (Locate + Remove, i.e. a folder the user registered) so bar [3] hit-tests every row type.
    const rows = [...catsEl.children];
    let best = null;
    for (let i = rows.length - 1; i >= 0; --i) {
      // a row scrolled out of the pane is not a point a mouse could reach — bring it into view,
      // then click its real centre. (Without this the gate right-clicks at a y past the viewport
      // and measures a gesture the owner cannot make.)
      try { rows[i].scrollIntoView ({ block: 'nearest' }); } catch (e) {}
      const r = rows[i].getBoundingClientRect();
      const x = Math.round (Math.min (Math.max (r.left + r.width / 2, 2), window.innerWidth  - 2));
      const y = Math.round (Math.min (Math.max (r.top  + r.height / 2, 2), window.innerHeight - 2));
      rows[i].dispatchEvent (new MouseEvent ('contextmenu', { bubbles: true, cancelable: true, clientX: x, clientY: y }));
      const m = [...document.querySelectorAll ('.pmenu')].pop () || document.querySelector ('#syn-ctx-menu.act');
      if (m && m.offsetWidth > 0) {
        const n = m.children.length;
        if (! best || n > best.n) best = { label: rows[i].textContent.trim(), x, y, idx: i, total: rows.length, n };
        if (n >= 3) break;                                   // title + Locate + Remove: the richest there is
      }
      if (window.__tpbMenuClose) try { window.__tpbMenuClose (); } catch (e) {}
      if (window.__synHideMenu)  try { window.__synHideMenu (); } catch (e) {}
    }
    if (! best) return { err: 'no category answered a right-click with a menu', total: rows.length };
    // reopen the winner so the measurements below read IT
    const row = rows[best.idx];
    row.dispatchEvent (new MouseEvent ('contextmenu', { bubbles: true, cancelable: true, clientX: best.x, clientY: best.y }));
    return best;
  });

  const menu = await p.evaluate (() => {
    const m = [...document.querySelectorAll ('.pmenu')].pop () || document.querySelector ('#syn-ctx-menu.act');
    if (! m) return null;
    const r = m.getBoundingClientRect();
    const rows = [...m.children].map (c => { const b = c.getBoundingClientRect();
      return { text: c.textContent.trim(), x: Math.round (b.left + b.width / 2), y: Math.round (b.top + b.height / 2),
               w: b.width, h: b.height, cls: c.className }; });
    const panel = document.querySelector ('.tpb-panel');
    const syn = document.getElementById ('syn-panel');
    return { cls: m.className, id: m.id || '',
             rect: { l: r.left, t: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height },
             rows,
             inSyn: !! (syn && syn.contains (m)),
             zMenu: getComputedStyle (m).zIndex, zPanel: panel ? getComputedStyle (panel).zIndex : '(no panel)',
             menuAfterPanel: !! (panel && (panel.compareDocumentPosition (m) & Node.DOCUMENT_POSITION_FOLLOWING)),
             vw: window.innerWidth, vh: window.innerHeight };
  });

  // ── [2] ────────────────────────────────────────────────────────────────────────────────────
  {
    const labels = menu ? menu.rows.map (r => r.text).filter (Boolean) : [];
    const hasLocate = labels.some (t => /Locate/i.test (t));
    const hasRemove = labels.some (t => /Remove/i.test (t));
    gate (!! menu && hasLocate,
      '[2] RIGHT-CLICKING A FOLDER OPENS A MENU',
      menu ? `right-clicked "${rc.label}" (row ${rc.idx + 1}/${rc.total}) at (${rc.x},${rc.y}) -> `
             + `${menu.cls || menu.id} with [${labels.join (' | ')}]`
             + (hasRemove ? '' : '   (no Remove — correct on factory content and on a subfolder; see [7])')
           : `no menu appeared on any of ${rc.total || 0} categories: ${rc.err || ''}`);
  }

  // ── [3] 🚨 HIT-TESTABLE AT ITS OWN COORDINATES ─────────────────────────────────────────────
  const hit = menu ? await p.evaluate ((pts) => {
    const m = [...document.querySelectorAll ('.pmenu')].pop () || document.querySelector ('#syn-ctx-menu.act');
    const describe = (el) => { if (! el) return '(nothing)';
      return el.tagName.toLowerCase() + (el.id ? '#' + el.id : '')
           + (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split (/\s+/).join ('.') : ''); };
    return pts.map (pt => { const el = document.elementFromPoint (pt.x, pt.y);
      return { what: pt.what, x: pt.x, y: pt.y, mine: !! (m && el && (el === m || m.contains (el))),
               got: describe (el) }; });
  }, [{ what: 'centre', x: Math.round (menu.rect.l + menu.rect.w / 2), y: Math.round (menu.rect.t + menu.rect.h / 2) }]
     .concat (menu.rows.filter (r => r.h > 2 && r.text).map (r => ({ what: '"' + r.text + '"', x: r.x, y: r.y })))) : [];
  {
    const bad = hit.filter (h => ! h.mine);
    gate (hit.length > 0 && bad.length === 0,
      '[3] THE MENU IS HIT-TESTABLE AT ITS OWN COORDINATES',
      hit.length === 0 ? 'there is no menu to hit-test — see [2]'
        : `elementFromPoint at ${hit.length} of the menu's own points: ${hit.length - bad.length} land on the menu`
          + (bad.length ? '\n        BURIED — a click at these points reaches something else:\n        '
              + bad.map (h => `${h.what} (${h.x},${h.y}) -> ${h.got}`).join ('\n        ')
              + '\n        This is the owner\'s "impossible to get to lol", measured.'
            : ` — every one of them, so a click on any row reaches the row`));
  }

  // ── [4] IT ESCAPED THE PANEL'S STACKING CONTEXT ────────────────────────────────────────────
  {
    const zSame = menu && menu.zMenu === menu.zPanel;
    const zOnlyWouldSay = menu
      ? (parseInt (menu.zMenu, 10) >= parseInt (menu.zPanel, 10) ? 'PASS ("the menu z is >= the panel z")'
                                                                 : 'FAIL')
      : 'n/a';
    const reallyClickable = hit.length > 0 && hit.every (h => h.mine);
    gate (!! menu && ! menu.inSyn, '[4] IT ESCAPED THE PANEL\'S STACKING CONTEXT',
      menu ? `menu z-index ${menu.zMenu} · .tpb-panel z-index ${menu.zPanel}${zSame ? ' (IDENTICAL)' : ''}`
             + ` · menu comes ${menu.menuAfterPanel ? 'AFTER' : 'BEFORE'} the panel in the DOM`
             + ` · inside #syn-panel: ${menu.inSyn ? 'YES — trapped in its stacking context' : 'no'}`
             + `\n        a z-index-only gate would say ${zOnlyWouldSay}; the hit test says `
             + `${reallyClickable ? 'CLICKABLE' : 'BURIED'} — that pair is why bar [3] hit-tests`
           : 'no menu');
  }

  // ── [5] THE MENU NEVER CUTS OFF ────────────────────────────────────────────────────────────
  {
    const corner = await p.evaluate (() => {
      const close = window.__tpbMenuClose; if (close) try { close (); } catch (e) {}
      const catsEl = document.querySelector ('.tpb-panel .tpb-pane');
      if (! catsEl) return null;
      const rows = [...catsEl.children];
      for (let i = rows.length - 1; i >= 0; --i) {
        rows[i].dispatchEvent (new MouseEvent ('contextmenu', { bubbles: true, cancelable: true,
          clientX: window.innerWidth - 2, clientY: window.innerHeight - 2 }));
        const m = [...document.querySelectorAll ('.pmenu')].pop () || document.querySelector ('#syn-ctx-menu.act');
        if (m && m.offsetWidth > 0) { const r = m.getBoundingClientRect();
          return { l: r.left, t: r.top, r: r.right, b: r.bottom, vw: window.innerWidth, vh: window.innerHeight }; }
      }
      return null; });
    const ok = !! corner && corner.l >= 0 && corner.t >= 0
                         && corner.r <= corner.vw + 0.5 && corner.b <= corner.vh + 0.5;
    gate (ok, '[5] THE MENU NEVER CUTS OFF',
      corner ? `right-clicked at the far corner (${corner.vw - 2},${corner.vh - 2}) in a ${corner.vw}x${corner.vh} viewport`
               + ` -> box [${Math.round (corner.l)},${Math.round (corner.t)} .. ${Math.round (corner.r)},${Math.round (corner.b)}]`
               + (ok ? ' — wholly on screen' : '   OFF SCREEN: the clamp did not run')
             : 'no menu opened at the corner');
  }

  // ── [6] A ROW ACTUALLY REACHES ITS HANDLER ─────────────────────────────────────────────────
  {
    const fired = await p.evaluate (() => {
      const close = window.__tpbMenuClose; if (close) try { close (); } catch (e) {}
      window.__gateCalls.length = 0;
      const catsEl = document.querySelector ('.tpb-panel .tpb-pane');
      if (! catsEl) return { err: 'no pane' };
      const rows = [...catsEl.children];
      for (let i = rows.length - 1; i >= 0; --i) {
        const rr = rows[i].getBoundingClientRect();
        rows[i].dispatchEvent (new MouseEvent ('contextmenu', { bubbles: true, cancelable: true,
          clientX: Math.round (rr.left + rr.width / 2), clientY: Math.round (rr.top + rr.height / 2) }));
        const m = [...document.querySelectorAll ('.pmenu')].pop () || document.querySelector ('#syn-ctx-menu.act');
        if (! m || ! m.offsetWidth) continue;
        const row = [...m.children].find (c => /Locate/i.test (c.textContent));
        if (! row) continue;
        const b = row.getBoundingClientRect();
        const x = Math.round (b.left + b.width / 2), y = Math.round (b.top + b.height / 2);
        const under = document.elementFromPoint (x, y);          // ← the click goes where a MOUSE would
        if (under) under.click ();
        return { clicked: row.textContent.trim(), via: under ? under.className || under.tagName : '(nothing)',
                 calls: window.__gateCalls.map (c => c.fn + '(' + c.args.join (',') + ')') };
      }
      return { err: 'no Locate row found on any category' }; });
    const ok = !! fired && ! fired.err && (fired.calls || []).some (c => /revealPath|openImportsFolder/.test (c));
    gate (ok, '[6] A ROW ACTUALLY REACHES ITS HANDLER',
      fired && ! fired.err
        ? `clicked "${fired.clicked}" through elementFromPoint (landed on ${fired.via}) -> natives fired: `
          + ((fired.calls || []).join (' · ') || 'NONE — the click was swallowed by whatever is on top')
        : `could not run: ${fired ? fired.err : 'no result'}`);
  }

  // ── [7] REMOVE IS NEVER OFFERED ON FACTORY OR A SUBFOLDER ──────────────────────────────────
  {
    const scan = await p.evaluate (() => {
      const close = window.__tpbMenuClose; if (close) try { close (); } catch (e) {}
      const catsEl = document.querySelector ('.tpb-panel .tpb-pane');
      if (! catsEl) return [];
      const out = [];
      [...catsEl.children].forEach ((row) => {
        const rr = row.getBoundingClientRect();
        row.dispatchEvent (new MouseEvent ('contextmenu', { bubbles: true, cancelable: true,
          clientX: Math.round (rr.left + rr.width / 2), clientY: Math.round (rr.top + rr.height / 2) }));
        const m = [...document.querySelectorAll ('.pmenu')].pop () || document.querySelector ('#syn-ctx-menu.act');
        const labels = m && m.offsetWidth ? [...m.children].map (c => c.textContent.trim()) : [];
        out.push ({ label: row.textContent.trim().replace (/\s+/g, ' '),
                    remove: labels.some (t => /Remove/i.test (t)),
                    locate: labels.some (t => /Locate/i.test (t)) });
        if (window.__tpbMenuClose) try { window.__tpbMenuClose (); } catch (e) {}
        if (window.__synHideMenu)  try { window.__synHideMenu (); } catch (e) {}
      });
      return out; });
    // a factory drawer is one of the ten names; the registered user root is the one from the payload
    const realNames = IMPORTS_REAL ? (JSON.parse (IMPORTS_REAL).folders || []).map (f => f.name) : ['MASTER'];
    const offenders = scan.filter (s => s.remove && ! realNames.some (n => s.label.indexOf (n) >= 0));
    const userRow  = scan.filter (s => realNames.some (n => s.label.indexOf (n) >= 0));
    const ok = offenders.length === 0 && (userRow.length === 0 || userRow.some (s => s.remove));
    gate (ok, '[7] REMOVE IS NEVER OFFERED ON FACTORY OR A SUBFOLDER',
      `${scan.length} categories right-clicked · Remove offered on: `
      + (scan.filter (s => s.remove).map (s => s.label).join (' · ') || '(none)')
      + ' · Locate offered on: ' + (scan.filter (s => s.locate).length) + ' of them'
      + (offenders.length ? '\n        REMOVE OFFERED ON CONTENT THE USER DID NOT REGISTER: '
          + offenders.map (o => o.label).join (' · ') : ''));
  }

  const mutSaw = await p.evaluate (() => window.__mutFired);
  console.log (`\n  mutation: ${mutSaw}`);
  console.log (`  ${pass} pass · ${fail} fail · ${pend} pending`);
  await b.close();
  process.exit (fail === 0 ? 0 : 1);
})().catch (e => { console.error ('GATE CRASHED: ' + (e && e.stack || e)); process.exit (2); });
