// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_import_gate.js — tp108 THE USER SOUNDFONT IMPORT, the page side (headless, a mock native).
//      node Tests/_org_import_gate.js [page.html]
//
//  The natives are mocked on window.__orgMock (organicsChooseSoundFont · organicsListSf2Presets · organicsImport ·
//  organicsDeleteUser; the organicImport event is fed through window.__orgImportIn, the page's own event entry).
//   1  the Organics browser lists a "User" category AFTER the nine factory ones, in the house grey/white (no purple)
//   2  its rows are name-only (no ▶, no size/credit) and the last row is "Import SoundFont…"
//   3  "Import SoundFont…" → the native chooser → organicsImport(path, -1, false); progress rides the house toast;
//      the done event selects the new instrument on the osc and says "Imported …"
//   4  an SF2 with 3 presets → the house .pmenu lists them + "Import all presets" → organicsImport(path, -2, false)
//   5  over 500 MB → "Import N MB of audio?" → "Import anyway" → organicsImport(path, preset, true)
//   6  a failed import toasts its error
//   7  right-click a User row → Remove → organicsDeleteUser(id), the row is gone from the reopened browser
//   8  a .sf2 dropped on the Organics display is NOT loaded as a sample (the engine stays Organics); with the OS-level path
//      (__orgFileDropped) it imports; the same path with no drop on an Organics display does nothing
// ══════════════════════════════════════════════════════════════════════════════════════════════
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };

(async () => {
  const { b, p, errs } = await H.launch({ page: PAGE, seed: { 0: { id: 'salamander.grand', artic: 0 } } });
  await H.enable(p, 'a'); await H.setEngine(p, 'a', 7); await H.sleep(700);
  // ── the mock natives (the C++ contract, OrganicsImport.h / PluginEditor.cpp withOrganics)
  await p.evaluate(() => {
    const M = window.__orgMock, L = window.__impLog = { calls: [], toasts: [] };
    M.index.push({ id: 'user.my-old-piano', name: 'My Old Piano', family: 'grand', category: 'User', sizeMB: 12, credit: 'old.sf2', licence: 'User import', installed: true, artics: ['Normal'] });
    let job = 0; window.__impNext = { chooser: '/Users/max/Sounds/My Piano.sfz', presets: [], done: { ok: true } };
    M.organicsChooseSoundFont = () => { L.calls.push(['choose']); return window.__impNext.chooser; };
    M.organicsListSf2Presets = (path) => { L.calls.push(['list', path]); return JSON.stringify({ ok: true, presets: window.__impNext.presets, error: '' }); };
    M.organicsImport = (path, preset, allow) => { L.calls.push(['import', path, preset, allow]); const j = ++job;
      setTimeout(() => window.__orgImportIn(JSON.stringify({ job: j, pct: 42, stage: 'Samples', done: false })), 30);
      setTimeout(() => { const d = Object.assign({ job: j, pct: 100, done: true, ok: true, id: '', name: '', ids: [], names: [], error: '', warning: '', needConfirm: false, mb: 0 }, window.__impNext.done);
        if (d.ok && d.id) M.index.push({ id: d.id, name: d.name, family: 'grand', category: 'User', installed: true, artics: ['Normal'] });
        window.__orgImportIn(JSON.stringify(d)); }, 120);
      return JSON.stringify({ job: j }); };
    M.organicsDeleteUser = (id) => { L.calls.push(['delete', id]); const i = M.index.findIndex(e => e.id === id); if (i >= 0) M.index.splice(i, 1); return JSON.stringify({ ok: i >= 0, error: '' }); };
    const setI = M.organicsSetInstrument; M.organicsSetInstrument = (osc, id) => { L.calls.push(['set', osc, id]);
      if (String(id).indexOf('user.') !== 0) return setI(osc, id); const e = M.index.find(x => x.id === id) || {};
      return JSON.stringify({ ok: true, id, name: e.name, family: 'grand', category: 'User', artics: ['Normal'], hasNoise: false, hasRelease: false, status: 'ok', artic: 0 }); };
    const t0 = window.__tiToast; window.__tiToast = (m) => { L.toasts.push(String(m)); if (t0) t0(m); };
    window.loadSampleForOscCalls = 0;
  });
  const openUser = () => p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms));
    document.querySelectorAll('.tpb-panel').forEach(x => x.remove()); window.__orgOpenBrowser('a'); await sleep(400);
    const panel = document.querySelector('.tpb-panel'); if (!panel) return { open: false };
    const cats = [...panel.querySelectorAll('.tpb-pane')[0].children];
    const R = { open: true, cats: cats.map(c => c.textContent.replace(/\d+$/, '').replace('•', '').trim()), catColors: cats.map(c => getComputedStyle(c).color) };
    { const t = document.createElement('i'); t.style.color = 'var(--purple-400)'; document.body.appendChild(t); R.purple = getComputedStyle(t).color; t.remove(); }
    const u = cats.find(c => c.textContent.replace('•', '').trim().indexOf('User') === 0); if (u) u.click(); await sleep(120);
    R.rows = [...panel.querySelectorAll('.tpb-pane')[1].children].map(r => ({ t: r.textContent.replace('•', '').trim(), play: !!r.querySelector('.tpb-play'),
      spans: [...r.children].filter(c => !c.classList.contains('tpb-play') && c.textContent.trim() && c.textContent.trim() !== '•').length }));
    return R; });

  // [1][2] the User category
  const u1 = await openUser();
  const fac = ['Keys', 'Organs', 'Strings', 'Plucked', 'Winds', 'Brass', 'Mallets & Bells', 'Choir & Voice', 'Percussion'];
  const ui = (u1.cats || []).findIndex(c => c.indexOf('User') === 0), lastFac = Math.max(...fac.map(f => (u1.cats || []).findIndex(c => c.indexOf(f) === 0)));
  const WHITE = 'rgb(255, 255, 255)', GREY = 'rgba(255, 255, 255, 0.5)';
  ok(u1.open && ui > lastFac && lastFac > 0 && (u1.catColors || []).every(c => c !== u1.purple && (c === WHITE || c === GREY)),
     '[1] the browser has a "User" category after the nine factory ones, in the house grey/white', `${(u1.cats || []).join(' / ')}`);
  const rows = u1.rows || [];
  ok(rows.length === 2 && rows[0].t === 'My Old Piano' && !rows[0].play && rows[0].spans === 1 && rows[1].t === 'Import SoundFont…',
     '[2] User rows are the name only (no ▶), the last row is "Import SoundFont…"', JSON.stringify(rows));

  // [3] Import SoundFont… → chooser → import → progress toast → done selects it
  const r3 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const L = window.__impLog; L.calls.length = 0; L.toasts.length = 0;
    window.__impNext = { chooser: '/Users/max/Sounds/My Piano.sfz', presets: [], done: { ok: true, id: 'user.my-piano', name: 'My Piano', ids: ['user.my-piano'], names: ['My Piano'], warning: '' } };
    const panel = document.querySelector('.tpb-panel'); const row = [...panel.querySelectorAll('.tpb-pane')[1].children].find(r => /Import SoundFont/.test(r.textContent));
    row.click(); await sleep(700);
    return { calls: L.calls.slice(), toasts: L.toasts.slice(), panel: !!document.querySelector('.tpb-panel'), name: document.getElementById('osc-a-orginst-display').textContent }; });
  const imp3 = r3.calls.find(c => c[0] === 'import');
  ok(r3.calls[0] && r3.calls[0][0] === 'choose' && imp3 && imp3[1] === '/Users/max/Sounds/My Piano.sfz' && imp3[2] === -1 && imp3[3] === false && !r3.panel,
     '[3a] "Import SoundFont…" closes the browser, opens the chooser and queues organicsImport(path, -1, false)', JSON.stringify(r3.calls));
  ok(r3.toasts.some(t => /Importing My Piano\.sfz.*42%/.test(t)) && r3.toasts.some(t => /^Imported My Piano/.test(t)),
     '[3b] the progress and the result ride the house toast', r3.toasts.join(' | '));
  ok(r3.calls.some(c => c[0] === 'set' && c[1] === 0 && c[2] === 'user.my-piano') && r3.name === 'My Piano',
     '[3c] the done event selects the new instrument on the oscillator', `header "${r3.name}"`);

  // [4] SF2 with presets → the .pmenu → Import all
  const r4 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const L = window.__impLog; L.calls.length = 0;
    window.__impNext = { chooser: '/Users/max/Sounds/GM Bank.sf2', presets: ['Soft Piano', 'Bright Strings', 'Choir Pad'], done: { ok: true, id: 'user.soft-piano', name: 'Soft Piano', ids: ['user.soft-piano', 'user.bright-strings', 'user.choir-pad'], names: ['Soft Piano', 'Bright Strings', 'Choir Pad'] } };
    window.__orgChooseImport('a'); await sleep(250);
    const m = [...document.querySelectorAll('.pmenu')].pop(); const items = m ? [...m.querySelectorAll('.pi')].map(x => x.textContent.trim()) : [];
    const title = m && m.querySelector('.ps') ? m.querySelector('.ps').textContent : '';
    const all = m ? [...m.querySelectorAll('.pi')].find(x => /Import all presets/.test(x.textContent)) : null; if (all) all.click(); await sleep(400);
    return { items, title, calls: L.calls.slice(), toasts: L.toasts.slice(-2) }; });
  const imp4 = r4.calls.find(c => c[0] === 'import');
  ok(r4.title === 'Preset' && r4.items.join('|') === 'Soft Piano|Bright Strings|Choir Pad|Import all presets' && imp4 && imp4[2] === -2,
     '[4] an SF2 with 3 presets: the house .pmenu lists them + "Import all presets" → organicsImport(path, -2)', `${r4.title}: ${r4.items.join(' | ')} · ${JSON.stringify(imp4)} · ${r4.toasts.join(' | ')}`);

  // [5] the size guard
  const r5 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const L = window.__impLog; L.calls.length = 0;
    window.__impNext = { chooser: '/Users/max/Sounds/Huge.sfz', presets: [], done: { ok: false, needConfirm: true, mb: 812, error: 'Huge.sfz holds 812 MB of audio (over 500 MB). Import anyway?' } };
    window.__orgChooseImport('a'); await sleep(350);
    const m = [...document.querySelectorAll('.pmenu')].pop(); const title = m && m.querySelector('.ps') ? m.querySelector('.ps').textContent : '';
    const items = m ? [...m.querySelectorAll('.pi')].map(x => x.textContent.trim()) : [];
    window.__impNext.done = { ok: true, id: 'user.huge', name: 'Huge', ids: ['user.huge'], names: ['Huge'] };
    const go = m ? [...m.querySelectorAll('.pi')].find(x => /Import anyway/.test(x.textContent)) : null; if (go) go.click(); await sleep(400);
    return { title, items, calls: L.calls.filter(c => c[0] === 'import') }; });
  ok(/Import 812 MB of audio\?/.test(r5.title) && r5.items.join('|') === 'Import anyway|Cancel' && r5.calls.length === 2 && r5.calls[1][3] === true && r5.calls[1][1] === '/Users/max/Sounds/Huge.sfz',
     '[5] over 500 MB asks "Import N MB of audio?" and "Import anyway" re-queues with allowLarge', `${r5.title} · ${JSON.stringify(r5.calls)}`);

  // [6] a failure
  const r6 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const L = window.__impLog; L.toasts.length = 0;
    window.__impNext = { chooser: '/Users/max/Sounds/Broken.sf2', presets: ['Only'], done: { ok: false, error: 'Broken.sf2 is truncated (a chunk runs past the end of the file)' } };
    window.__orgChooseImport('a'); await sleep(400); return L.toasts.slice(); });
  ok(r6.some(t => /Broken\.sf2 is truncated/.test(t)), '[6] a failed import toasts its reason', r6.join(' | '));

  // [7] right-click → Remove
  const r7 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const L = window.__impLog; L.calls.length = 0;
    document.querySelectorAll('.tpb-panel').forEach(x => x.remove()); window.__orgOpenBrowser('a'); await sleep(400);
    let panel = document.querySelector('.tpb-panel'); const cats = [...panel.querySelectorAll('.tpb-pane')[0].children];
    cats.find(c => c.textContent.replace('•', '').trim().indexOf('User') === 0).click(); await sleep(120);
    const row = [...panel.querySelectorAll('.tpb-pane')[1].children].find(r => /My Old Piano/.test(r.textContent)); const rr = row.getBoundingClientRect();
    row.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2, clientX: rr.left + 10, clientY: rr.top + 5 })); await sleep(150);
    const m = [...document.querySelectorAll('.pmenu')].pop(); const items = m ? [...m.querySelectorAll('.pi')].map(x => x.textContent.trim()) : [];
    const title = m && m.querySelector('.ps') ? m.querySelector('.ps').textContent : '';
    const rem = m ? [...m.querySelectorAll('.pi')].find(x => x.textContent.trim() === 'Remove') : null; if (rem) rem.click(); await sleep(500);
    panel = document.querySelector('.tpb-panel'); let after = [];
    if (panel) { const c2 = [...panel.querySelectorAll('.tpb-pane')[0].children].find(c => c.textContent.replace('•', '').trim().indexOf('User') === 0); if (c2) c2.click(); await sleep(120);
      after = [...panel.querySelectorAll('.tpb-pane')[1].children].map(r => r.textContent.replace('•', '').trim()); }
    return { title, items, calls: L.calls.slice(), after, toasts: L.toasts.slice(-1) }; });
  ok(r7.title === 'My Old Piano' && r7.items.join('|') === 'Remove' && r7.calls.some(c => c[0] === 'delete' && c[1] === 'user.my-old-piano') && r7.after.length > 0 && !r7.after.includes('My Old Piano'),
     '[7] right-click a User row → Remove → organicsDeleteUser(id); the reopened list no longer has it', `${r7.title}: ${r7.items} · after ${JSON.stringify(r7.after)} · ${r7.toasts}`);

  // [8] the drop
  const r8 = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const L = window.__impLog; L.calls.length = 0;
    document.querySelectorAll('.tpb-panel, .pmenu').forEach(x => x.remove());
    window.__impNext = { chooser: '', presets: [], done: { ok: true, id: 'user.dropped', name: 'Dropped', ids: ['user.dropped'], names: ['Dropped'] } };
    const fnCalls = []; const gn = window.Juce.getNativeFunction; window.Juce.getNativeFunction = (n) => { fnCalls.push(n); return gn(n); };
    const d = document.getElementById('osc-a-device'), sd = d.querySelector('.sample-view .samp-disp'), r = sd.getBoundingClientRect();
    const dt = new DataTransfer(); dt.items.add(new File([new Uint8Array(16)], 'Dropped.sfz', { type: '' }));
    sd.dispatchEvent(new DragEvent('dragover', { bubbles: true, cancelable: true, dataTransfer: dt, clientX: r.left + 20, clientY: r.top + 20 }));
    const ev = new DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer: dt, clientX: r.left + 20, clientY: r.top + 20 });
    sd.dispatchEvent(ev); await sleep(50);
    const stillOrg = d.classList.contains('engine-organic');
    window.__orgFileDropped('/Users/max/Desktop/Dropped.sfz'); await sleep(400);
    const imp = L.calls.filter(c => c[0] === 'import');
    // the OS path alone (no drop on an Organics display for 3 s) → nothing
    await sleep(2200); L.calls.length = 0; window.__orgFileDropped('/Users/max/Desktop/Elsewhere.sf2'); await sleep(500);
    window.Juce.getNativeFunction = gn;
    return { stillOrg, prevented: ev.defaultPrevented, sampleLoads: fnCalls.filter(n => n === 'loadSampleForOsc' || n === 'resetBlendState').length, imp, stray: L.calls.slice() }; });
  ok(r8.stillOrg && r8.prevented && r8.sampleLoads === 0, '[8a] a SoundFont dropped on the Organics display is not loaded as a sample (engine stays Organics)', JSON.stringify(r8));
  ok(r8.imp.length === 1 && r8.imp[0][1] === '/Users/max/Desktop/Dropped.sfz', '[8b] with the OS-level path it imports that file', JSON.stringify(r8.imp));
  ok(r8.stray.length === 0, '[8c] a SoundFont path with no drop on an Organics display does nothing', JSON.stringify(r8.stray));

  const real = errs.filter(e => !/formatOutput/.test(e));   // formatOutput: a pre-existing stub artefact (HEAD too — _org_view_gate filters it the same way)
  ok(real.length === 0, 'no page errors', real.join(' | '));
  await b.close();
  console.log(`\n_org_import_gate: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
