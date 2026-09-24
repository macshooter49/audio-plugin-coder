// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_view_gate.js — tp104 THE ORGANICS OSC VIEW, headless, against the page's built-in natives mock.
//
//    node Tests/_org_view_gate.js [page.html]
//
//  THE BARS
//   1  ALL EIGHT OSCILLATORS SELECT ORGANICS — A–D on the synth page, E–H adopted onto the Patcher canvas
//      (they are clones of B, tp40): engine-organic, the knob wrap shows, the picture is drawn, the name reads
//   2  EVERY KNOB WRITES ITS OWN PARAMETER — a real drag on each of the 10 rings × 8 oscillators lands a
//      setSynParam on SYN_OSC_x_ORG_<KNOB>, and each is the contract's mod destination 5272 + osc·10 + knob
//   3  THE ARROW CYCLES page 1 → page 2 → unison → page 1, and the picture is there on every page (fb590)
//   4  THE PILL: ‹ › step through the INSTALLED instruments of the category, the wheel steps, a click opens the
//      two-pane browser (purple categories, size · credit, ▶ audition, "Not installed" dimmed and unpickable)
//   5  THE ARTICULATION DROPDOWN is there only for an instrument with more than one, and it writes ORG_ARTIC
//   6  NOISE GREYS (with its tooltip) when the instrument has no mechanical noise
//   7  STATE READ-BACK — a fresh page paints the saved instrument, articulation and picture; a missing one says so
//   8  ENGINE AWAY AND BACK keeps the instrument (no re-set, no default)
// ══════════════════════════════════════════════════════════════════════════════════════════════
const H = require('./_org_harness.js');
const PAGE = process.argv[2] || H.PAGE_DEFAULT;
let pass = 0, fail = 0;
const ok = (c, name, detail) => { c ? ++pass : ++fail; console.log(`  ${c ? 'PASS' : 'FAIL'}  ${name}${detail ? '\n        ' + detail : ''}`); };
const KN = ['DYNAMICS', 'TONE', 'BODY', 'ATTACK', 'HUMAN', 'RELEASE', 'NOISE', 'SUSTAIN', 'VELOCITY', 'IMAGE'];
const realErrs = errs => errs.filter(e => !/formatOutput/.test(e));   // formatOutput: a pre-existing stub artefact (also on HEAD, any engine)

(async () => {
  // ── run 1: the synth page + the Patcher ─────────────────────────────────────────────────────
  let { b, p, errs } = await H.launch({ page: PAGE });
  for (const o of 'abcd') { await H.enable(p, o); await H.setEngine(p, o, 7); }
  await H.sleep(700);
  const v1 = await p.evaluate(() => 'abcd'.split('').map(o => { const d = document.getElementById('osc-' + o + '-device');
    const w = d.querySelector('.organic-knob-wrap'), sv = d.querySelector('.org-viz');
    return { o, cls: d.classList.contains('engine-organic'), wrap: w && getComputedStyle(w).display !== 'none',
      parts: sv ? sv.querySelectorAll('.pt').length : 0, name: (document.getElementById('osc-' + o + '-orginst-display') || {}).textContent }; }));
  // E–H: switch them on, open the Patcher (it adopts them), then pick Organics from their own header
  await p.evaluate(() => { 'efgh'.split('').forEach(o => { try { window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE').setNormalisedValue(1); } catch (e) {} });
    if (window.__tpOpen) window.__tpOpen(); });
  await H.sleep(1800);
  for (const o of 'efgh') await H.setEngine(p, o, 7);
  await H.sleep(900);
  const v2 = await p.evaluate(() => 'efgh'.split('').map(o => { const d = document.getElementById('osc-' + o + '-device');
    const w = d && d.querySelector('.organic-knob-wrap'), sv = d && d.querySelector('.org-viz');
    return { o, onCanvas: !!(d && d.closest('#tp-page')), cls: !!(d && d.classList.contains('engine-organic')), wrap: !!(w && getComputedStyle(w).display !== 'none'),
      parts: sv ? sv.querySelectorAll('.pt').length : 0, name: (document.getElementById('osc-' + o + '-orginst-display') || {}).textContent }; }));
  const all8 = v1.concat(v2);
  ok(all8.every(x => x.cls && x.wrap && x.parts > 20 && x.name === 'Grand Piano') && v2.every(x => x.onCanvas),
     '[1] all eight oscillators select Organics (A–D on the page, E–H on the Patcher canvas), picture + name drawn',
     all8.map(x => `${x.o}:${x.cls ? 'org' : '—'}/${x.wrap ? 'wrap' : 'nowrap'}/${x.parts}pt/${x.name}${x.onCanvas ? '/canvas' : ''}`).join(' '));

  // [2] drag every ring
  const drags = await p.evaluate(async (KN) => { const out = []; const sleep = ms => new Promise(r => setTimeout(r, ms));
    for (const o of 'abcdefgh') { const d = document.getElementById('osc-' + o + '-device'), w = d.querySelector('.organic-knob-wrap');
      for (let k = 0; k < KN.length; k++) { const id = 'SYN_OSC_' + o.toUpperCase() + '_ORG_' + KN[k];
        w.classList.toggle('pg2', k >= 5); await sleep(0);
        const ring = d.querySelector('.knob[data-syn="' + id + '"] .knob-ring'); if (!ring) { out.push({ id, ring: false }); continue; }
        window.__natLog.length = 0;
        const r = ring.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
        ring.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientX: x, clientY: y, pointerId: 1, button: 0 }));
        document.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, clientX: x, clientY: y - 37, pointerId: 1 }));
        document.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, clientX: x, clientY: y - 37, pointerId: 1 }));
        await sleep(0);
        const hits = window.__natLog.filter(e => e[0] === id), others = window.__natLog.filter(e => /_ORG_/.test(e[0]) && e[0] !== id);
        out.push({ id, ring: true, wrote: hits.length > 0, val: hits.length ? +hits[hits.length - 1][1].toFixed(3) : null, stray: others.map(e => e[0]),
                   dest: window.__knobDest ? window.__knobDest(id) : null, want: 5272 + 'abcdefgh'.indexOf(o) * 10 + k }); }
      w.classList.remove('pg2'); }
    return out; }, KN);
  const bad = drags.filter(x => !x.ring || !x.wrote || x.stray.length || x.dest !== x.want);
  ok(drags.length === 80 && bad.length === 0, `[2] every knob writes its own SYN_OSC_x_ORG_* param (${drags.filter(x => x.wrote).length}/80) and is mod dest 5272 + osc·10 + knob`,
     bad.slice(0, 6).map(x => JSON.stringify(x)).join(' | ') || `e.g. ${drags[0].id} → ${drags[0].val} dest ${drags[0].dest}; ${drags[79].id} dest ${drags[79].dest}`);

  // [3] the arrow cycles, the picture stays
  await p.evaluate(() => { if (window.__tpClose) window.__tpClose(); });
  const cyc = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const d = document.getElementById('osc-a-device');
    const w = d.querySelector('.organic-knob-wrap'), out = [];
    const snap = () => { const sv = d.querySelector('.org-viz'), r = sv.getBoundingClientRect(), labels = [...d.querySelectorAll('.knob-label, .uni-voices .knob-label')].filter(e => e.offsetParent && e.getBoundingClientRect().height > 0).map(e => e.textContent.trim());
      return { pg2: w.classList.contains('pg2'), uni: d.classList.contains('uni-page'), pic: getComputedStyle(sv).display !== 'none' && r.width > 100 && r.height > 20 && sv.querySelectorAll('.pt').length > 20,
               h: +r.height.toFixed(1), labels: labels.slice(0, 6).join('·') }; };
    out.push(snap());
    for (let i = 0; i < 3; i++) { d.querySelector('.organic-knob-wrap > .organic-arrow:not(.set-prev)') && (d.classList.contains('uni-page') ? d.querySelector('.uni-knob-wrap > .uni-arrow:not(.set-prev)') : d.querySelector('.organic-knob-wrap > .organic-arrow:not(.set-prev)')).click(); await sleep(250); out.push(snap()); }
    return out; });
  const cycOk = !cyc[0].pg2 && !cyc[0].uni && cyc[1].pg2 && !cyc[1].uni && cyc[2].uni && !cyc[3].pg2 && !cyc[3].uni && cyc.every(c => c.pic)
    && /Dynamics/.test(cyc[0].labels) && /Release/.test(cyc[1].labels) && /Players/.test(cyc[2].labels);
  ok(cycOk, '[3] the arrow cycles page 1 → page 2 → Ensemble (unison) → page 1 with the picture present on each',
     cyc.map((c, i) => `${i}: ${c.uni ? 'uni' : c.pg2 ? 'pg2' : 'pg1'} pic=${c.pic}(${c.h}) [${c.labels}]`).join(' | '));

  // [4] ‹ › / wheel / browser
  const pill = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const d = document.getElementById('osc-a-device'), R = {};
    const name = () => document.getElementById('osc-a-orginst-display').textContent;
    const md = el => { const r = el.getBoundingClientRect(); el.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0, clientX: r.left + 3, clientY: r.top + 3 })); };
    const [prev, next] = d.querySelectorAll('.org-nav');
    R.n0 = name(); md(next); await sleep(150); R.n1 = name(); md(next); await sleep(150); R.n2 = name(); md(prev); await sleep(150); R.n3 = name();
    const pw = d.querySelector('.org-inst-wrap'); const pr = pw.getBoundingClientRect();
    pw.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: 60, clientX: pr.left + 5, clientY: pr.top + 5 })); await sleep(200); R.w1 = name();
    await sleep(150); pw.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: -60, clientX: pr.left + 5, clientY: pr.top + 5 })); await sleep(200); R.w2 = name();
    md(pw); await sleep(400);
    const panel = document.querySelector('.tpb-panel'); R.open = !!panel; if (!panel) return R;
    const panes = panel.querySelectorAll('.tpb-pane'), cats = [...panes[0].children];
    R.cats = cats.map(c => c.querySelector('span:not([style*="flex:none"])') ? c.textContent.replace(/\d+$/, '').replace('•', '').trim() : c.textContent.trim());
    R.catColor = getComputedStyle(cats[2]).color; { const t = document.createElement('i'); t.style.color = 'var(--purple-400)'; document.body.appendChild(t); R.purple = getComputedStyle(t).color; t.remove(); }
    const pickCat = lab => { const c = cats.find(x => x.textContent.indexOf(lab) === 0 || x.textContent.replace('•', '').trim().indexOf(lab) === 0); if (c) c.click(); return !!c; };
    pickCat('Strings'); await sleep(100);
    const rows = () => [...panel.querySelectorAll('.tpb-pane')[1].children].map(r => ({ t: r.textContent.replace('•', '').trim(), color: r.style.color, play: !!r.querySelector('.tpb-play') }));
    R.strings = rows();
    pickCat('Choir'); await sleep(100); R.choir = rows();
    const choirRow = panel.querySelectorAll('.tpb-pane')[1].children[0]; if (choirRow) choirRow.click(); await sleep(200); R.afterDimClick = name();
    pickCat('Strings'); await sleep(100);
    const celloRow = [...panel.querySelectorAll('.tpb-pane')[1].children].find(r => /^Cello/.test(r.textContent.replace('•', '').trim()));
    if (celloRow) { celloRow.querySelector('.tpb-play').click(); await sleep(100); R.previews = window.__orgMock.previews.slice(); celloRow.click(); await sleep(300); }
    R.picked = name(); R.fam = window.__orgView('a') && window.__orgView('a').fam; R.panelOpen = !!document.querySelector('.tpb-panel');   /* a single click commits and keeps the browser open (the house two-pane rule); a double-click closes */
    return R; });
  ok(pill.n0 === 'Grand Piano' && pill.n1 === 'Wurlitzer EP200' && pill.n2 === 'Grand Piano' && pill.n3 === 'Wurlitzer EP200'
     && pill.w1 === 'Grand Piano' && pill.w2 === 'Wurlitzer EP200',
     '[4a] ‹ › and the wheel step through the installed Keys (wrapping)', `${pill.n0} → ${pill.n1} → ${pill.n2} ‹ ${pill.n3} · wheel ${pill.w1} / ${pill.w2}`);
  const catsWant = ['Keys', 'Organs', 'Strings', 'Plucked', 'Winds', 'Brass', 'Mallets & Bells', 'Choir & Voice', 'Percussion'];
  ok(pill.open && catsWant.every(c => (pill.cats || []).some(x => x.indexOf(c) === 0)) && pill.catColor === pill.purple,
     '[4b] the pill opens the two-pane browser with the nine categories in purple', `${(pill.cats || []).join(' / ')} · ${pill.catColor} (the house --purple-400 = ${pill.purple})`);
  const sv = pill.strings || [], ch = pill.choir || [];
  ok(sv.some(r => /^Violin Section.*MB · VSCO/.test(r.t) && r.play) && sv.some(r => /^Cello/.test(r.t))
     && ch.length === 1 && /Not installed/.test(ch[0].t) && !ch[0].play && /\.24\)/.test(ch[0].color) && pill.afterDimClick !== 'Choir Aahs',
     '[4c] rows carry size · credit and a ▶; "Not installed" is dimmed, has no ▶ and cannot be picked', `${sv.map(r => r.t).join(' | ')} || ${ch.map(r => r.t + ' ' + r.color).join('')} · after click: ${pill.afterDimClick}`);
  ok((pill.previews || []).indexOf('vsco.cello') >= 0 && pill.picked === 'Cello' && pill.fam === 'cello' && pill.panelOpen,
     '[4d] ▶ auditions through organicsPreview(id); a click on a row loads it (browser stays, house rule) and redraws the picture', `previews ${JSON.stringify(pill.previews)} · ${pill.picked} · ${pill.fam}`);

  // [5] articulation, [6] noise
  const art = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const d = document.getElementById('osc-a-device'), R = {};
    const vis = () => { const a = d.querySelector('.org-artic'); return getComputedStyle(a).visibility === 'visible'; };
    await window.__orgSetInstrument('a', 'salamander.grand'); await sleep(100); R.grand = vis();
    await window.__orgSetInstrument('a', 'vsco.violin.section'); await sleep(100); R.violin = vis();
    const a = d.querySelector('.org-artic'), r = a.getBoundingClientRect();
    a.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0, clientX: r.left + 2, clientY: r.top + 2 })); await sleep(150);
    const m = document.querySelector('.pmenu.org-artic-menu'); R.menu = !!m; R.rows = m ? [...m.querySelectorAll('.pi')].map(x => x.textContent + (x.classList.contains('cur') ? '•' : '')) : [];
    const pz = m && [...m.querySelectorAll('.pi')].find(x => x.textContent === 'Pizzicato'); if (pz) pz.click(); await sleep(100);
    R.param = window.__params['SYN_OSC_A_ORG_ARTIC']; R.shown = d.querySelector('.org-artic .samp-sel-disp').textContent; R.menuGone = !document.querySelector('.pmenu.org-artic-menu');
    // [6] noise
    const nk = () => d.querySelector('.knob[data-syn="SYN_OSC_A_ORG_NOISE"]');
    R.noiseViolin = nk().classList.contains('org-na');
    await window.__orgSetInstrument('a', 'vsco.flute'); await sleep(100);
    R.noiseFlute = nk().classList.contains('org-na'); R.noiseTip = nk().getAttribute('title'); R.noiseOp = getComputedStyle(nk()).opacity;
    return R; });
  ok(!art.grand && art.violin && art.menu && art.rows.join(',') === 'Sustain•,Staccato,Pizzicato,Tremolo' && Math.abs(art.param - 2 / 7) < 1e-6 && art.shown === 'Pizz' && art.menuGone,
     '[5] the articulation dropdown shows only for a multi-articulation instrument (.pmenu, • on the current) and writes ORG_ARTIC',
     `grand:${art.grand} violin:${art.violin} rows ${art.rows.join(',')} → ARTIC ${art.param} "${art.shown}"`);
  ok(!art.noiseViolin && art.noiseFlute && /no mechanical noise/.test(art.noiseTip || '') && +art.noiseOp < 0.5,
     '[6] Noise greys with its tooltip when the instrument has no mechanical noise', `violin ${art.noiseViolin} · flute ${art.noiseFlute} op ${art.noiseOp} "${art.noiseTip}"`);

  // [8] engine away and back
  const back = await p.evaluate(async () => { const sleep = ms => new Promise(r => setTimeout(r, ms)); const sel = document.getElementById('osc-a-engine-select');
    await window.__orgSetInstrument('a', 'vsco.violin.section'); await sleep(100); const n0 = window.__orgMock.sets.length;
    sel.value = '6'; sel.dispatchEvent(new Event('change')); await sleep(300); const away = document.getElementById('osc-a-device').className;
    sel.value = '7'; sel.dispatchEvent(new Event('change')); await sleep(500);
    return { away: /engine-modal/.test(away) && !/engine-organic/.test(away), name: document.getElementById('osc-a-orginst-display').textContent,
             fam: window.__orgView('a').fam, reSets: window.__orgMock.sets.length - n0, wrote: window.__params['SYN_OSC_A_ENGINE'] }; });
  ok(back.away && back.name === 'Violin Section' && back.fam === 'violin' && back.reSets === 0 && Math.abs(back.wrote - 7 / 11) < 1e-6,
     '[8] switching the engine away (Modal) and back keeps the instrument — no re-set, written as 7/11 (the backend cardinality)', JSON.stringify(back));
  ok(realErrs(errs).length === 0, '[—] run 1: no page errors', realErrs(errs).join(' | '));
  await b.close();

  // ── run 2: a fresh page with saved state (the engine is the truth) ─────────────────────────────
  ({ b, p, errs } = await H.launch({ page: PAGE, seed: { 2: { id: 'vsco.cello', artic: 1 }, 1: { id: 'community.choir.aahs' } },
    pre: () => { window.__stubState('SYN_OSC_C_ENGINE').norm = 7 / 11; window.__stubState('SYN_OSC_B_ENGINE').norm = 7 / 11; } }));
  await H.sleep(600);
  const rb = await p.evaluate(() => { const g = o => ({ name: document.getElementById('osc-' + o + '-orginst-display').textContent, fam: (window.__orgView(o) || {}).fam,
      artic: document.querySelector('#osc-' + o + '-device .org-artic .samp-sel-disp').textContent, eng: document.getElementById('osc-' + o + '-engine-display').textContent,
      status: document.querySelector('#osc-' + o + '-device .org-status').textContent, missing: document.querySelector('#osc-' + o + '-device .samp-disp').classList.contains('org-missing') });
    return { c: g('c'), b: g('b'), sets: window.__orgMock.sets.length }; });
  ok(rb.c.eng === 'Organics' && rb.c.name === 'Cello' && rb.c.fam === 'cello' && rb.c.artic === 'Pizz' && rb.sets === 0,
     '[7a] a fresh page READS BACK the saved instrument, articulation and picture (nothing re-set, nothing defaulted)', JSON.stringify(rb.c) + ' sets=' + rb.sets);
  ok(rb.b.name === 'Choir Aahs' && rb.b.missing && rb.b.status === 'Instrument not installed: Choir Aahs',
     '[7b] a saved instrument that is not installed says so: "Instrument not installed: <name>"', JSON.stringify(rb.b));
  ok(realErrs(errs).length === 0, '[—] run 2: no page errors', realErrs(errs).join(' | '));
  await b.close();

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.log('FAIL (crash)', e); process.exit(1); });
