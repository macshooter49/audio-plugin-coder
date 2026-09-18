/* tp39c — Max: "I'm ticking boxes and pressing the dice but it's not working". Reproduce in the real plugin: read the
   SAVED dice state, open the sheet, tick chips, press Roll, press the header dice — count what actually changes. */
(function(){
  var R = { phase: 'start', errs: [], steps: [] }; window.__tp39c = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39c:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  var IDS = []; 'ABCDEFGH'.split('').forEach(function(o){ IDS.push('SYN_OSC_' + o + '_ENABLE', 'SYN_OSC_' + o + '_ENGINE', 'SYN_OSC_' + o + '_UNISON'); });
  ['SYN_ENV_AMP_A','SYN_ENV_AMP_D','SYN_ENV_AMP_S','SYN_ENV_AMP_R','SYN_FILTER1_CUTOFF','SYN_FILTER1_RES','SYN_FILTER1_TYPE','SYN_FILTER2_CUTOFF'].forEach(function(i){ IDS.push(i); });
  function snap(){ var s = {}; IDS.forEach(function(i){ var st = ss(i); if (st) s[i] = Math.round(st.getNormalisedValue() * 1000); });
    try { s.fx = (window.__fxrDevs ? window.__fxrDevs() : []).length; } catch (e) {} try { s.routes = (window.__tiRoutes ? window.__tiRoutes() : []).length; } catch (e) {} return s; }
  function diff(a, b){ var n = 0, ks = []; Object.keys(b).forEach(function(k){ if (a[k] !== b[k]) { n++; if (ks.length < 6) ks.push(k); } }); return { n: n, ks: ks }; }
  function step(name, fn){ return function(){ var before = snap(); return Promise.resolve().then(fn).then(function(){ return wait(2500); }).then(function(){ var d = diff(before, snap());
    var db = document.getElementById('dice-btn'); R.steps.push({ step: name, changed: d.n, keys: d.ks, rolling: !!(db && db.classList.contains('rolling')), sheetOn: document.getElementById('tp-sheet').classList.contains('on'), lastAim: window.__tpDiceLastAim || null }); publish(); }); }; }
  function chipsOf(id){ return Array.prototype.slice.call(document.querySelectorAll('#' + id + ' .tp-chip')); }
  function clickChip(id, val, attr){ var c = chipsOf(id).filter(function(b){ return b.dataset[attr] === val; })[0]; if (!c) { R.errs.push('no chip ' + id + ' ' + val); return; } c.click(); }
  wait(3000).then(function(){
    var L = window.__tpLayout ? window.__tpLayout() : null; R.saved = L ? { aim: L.aim, aims: L.aims, blocks: L.blocks, dlevel: L.dlevel, cpuCap: L.cpuCap } : 'no layout';
    R.hasSheetOpen = typeof window.__tpDiceSheetOpen; R.hasDice = typeof window.__tpDice;
  })
  .then(step('dice-click-as-saved', function(){ document.getElementById('dice-btn').click(); }))
  .then(step('open-sheet+tick+Roll', function(){ var ok = window.__tpDiceSheetOpen(); R.sheetOpened = ok; R.chips = { aim: chipsOf('dc-aim').length, blk: chipsOf('dc-blk').length, lvl: chipsOf('dc-lvl').length, cap: chipsOf('dc-cap').length };
    clickChip('dc-aim', 'keys', 'a'); clickChip('dc-aim', 'leads', 'a'); clickChip('dc-lvl', 'crazy', 'l'); clickChip('dc-cap', 'off', 'c');
    R.chipsActiveAfterTicks = chipsOf('dc-aim').filter(function(b){ return b.classList.contains('active'); }).map(function(b){ return b.dataset.a; });
    var okb = document.getElementById('tp-sh-ok'); R.okText = okb ? okb.textContent : null; if (okb) okb.click(); }))
  .then(step('dice-click-after-sheet', function(){ document.getElementById('dice-btn').click(); }))
  .then(step('open-sheet+tick+press-DICE-through-backdrop', function(){ window.__tpDiceSheetOpen(); chipsOf('dc-aim').filter(function(b){ return b.classList.contains('active'); }).forEach(function(b){ b.click(); }); clickChip('dc-aim', 'drums', 'a'); clickChip('dc-blk', 'fx', 'b');
    var sh = document.getElementById('tp-sheet'), db = document.getElementById('dice-btn'), r = db.getBoundingClientRect(); R.diceRect = [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)];
    R.underDice = (document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2) || {}).id || null;   /* what a real click hits while the sheet is open */
    sh.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 })); }))
  .then(function(){ var L = window.__tpLayout(); R.after = { aim: L.aim, aims: L.aims, blocks: L.blocks, dlevel: L.dlevel, cpuCap: L.cpuCap }; R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39c started';
})();
