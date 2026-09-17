/* tp35 — WHERE THE SECONDS GO. Runs inside the real WebView (fb504 hook, via Tests/mac_ui_exp.mm)
   and times the five things Max waits on: the list natives behind every menu, a bank preset load,
   a flow card's open / preset step / preset menu, the wavetable arrows on an import folder, and a
   dice roll (per-step sync cost, wall time to the restore). Results in window.__tp35, published
   through document.title in 'tp35:<gen>:<i>/<n>:<chunk>' pieces (the tp34 channel). */
(function(){
  var R = { phase: 'start', nat: {}, natMs: {}, errs: [], A: [], B: [], C: {}, D: {}, E: [] };
  window.__tp35 = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R);
    var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp35:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 3000);
  window.addEventListener('error', function(e){ if (R.errs.length < 8) R.errs.push(String(e.message).slice(0, 90) + ' @' + (e.filename || '?').split('/').pop() + ':' + e.lineno); });
  /* the native census: every call leaves through emitEvent('__juce__invoke'); the reply comes back
     through the promise the page created, so time it by wrapping the returned promise resolution */
  var natOn = false;
  try { var be = window.__JUCE__.backend, oEmit = be.emitEvent;
    be.emitEvent = function(ev, payload){ if (natOn && ev === '__juce__invoke' && payload && payload.name) R.nat[payload.name] = (R.nat[payload.name] || 0) + 1;
      return oEmit.apply(this, arguments); }; } catch (e) {}
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function raf2(){ return new Promise(function(r){ requestAnimationFrame(function(){ requestAnimationFrame(function(){ r(); }); }); }); }
  function timeNative(name, args){ return new Promise(function(res){ var f = NF(name); if (!f) { res({ name: name, ms: -1 }); return; }
    var a = performance.now();
    Promise.resolve(f.apply(null, args || [])).then(function(v){ var s = (typeof v === 'string') ? v : JSON.stringify(v);
      res({ name: name, ms: +(performance.now() - a).toFixed(1), bytes: s ? s.length : 0, head: s ? s.slice(0, 40).replace(/["\\]/g, "'") : '' }); },
      function(e){ res({ name: name, ms: +(performance.now() - a).toFixed(1), err: String(e).slice(0, 60) }); }); }); }
  /* long-task watch: the biggest gap between consecutive rAFs during a section */
  var gapMax = 0, gapLast = 0, gapOn = false;
  (function tick(){ requestAnimationFrame(function(t){ if (gapOn && gapLast && t - gapLast > gapMax) gapMax = t - gapLast; gapLast = t; tick(); }); })();
  function gapStart(){ gapMax = 0; gapOn = true; } function gapStop(){ gapOn = false; return +gapMax.toFixed(0); }
  var $ = function(s){ return document.querySelector(s); };

  function sectionA(){   /* the list natives, cold then warm */
    R.phase = 'A';
    var names = ['listWtImports', 'listImports', 'getPresets', 'listPresets', 'scanSampleFactory', 'listSampleImports', 'listNoiseImports'];
    var seq = Promise.resolve();
    for (var round = 0; round < 3; round++) names.forEach(function(n){ seq = seq.then(function(){
      gapStart(); return timeNative(n, n === 'getPresets' ? ['gli'] : []).then(function(r){ r.gap = gapStop(); R.A.push(r); }); }); });
    return seq;
  }
  function sectionB(){   /* a bank preset load, as the browser does it: loadPatchFile(path) */
    R.phase = 'B';
    return timeNative('listPresets').then(function(){ var f = NF('listPresets'); return Promise.resolve(f()); }).then(function(js){
      var cat = null; try { cat = JSON.parse(js); } catch (e) {}
      var all = []; ((cat && cat.banks) || []).forEach(function(b){ (b.presets || []).forEach(function(p){ if (!p.factory) all.push(p); }); });
      all.sort(function(a, b){ return (b.bytes || 0) - (a.bytes || 0); });
      R.B.push({ userPresets: all.length, biggestKB: all.length ? Math.round(all[0].bytes / 1024) : 0 });
      var picks = all.slice(0, 2).concat(all.slice(-1));
      var seq = Promise.resolve();
      picks.forEach(function(p){ seq = seq.then(function(){ natOn = true; R.nat = {}; gapStart(); var a = performance.now();
        return Promise.resolve(NF('loadPatchFile')(p.path)).then(function(){ var t1 = performance.now() - a; return raf2().then(function(){
          R.B.push({ name: p.name, kb: Math.round(p.bytes / 1024), nativeMs: +t1.toFixed(0), settledMs: +(performance.now() - a).toFixed(0), gap: gapStop(), nat: R.nat }); natOn = false; }); }).then(function(){ return wait(600); }); }); });
      return seq; });
  }
  function sectionC(){   /* a flow card: open, step a preset, open its menu */
    R.phase = 'C';
    var C = R.C;
    gapStart(); natOn = true; R.nat = {}; var a = performance.now();
    try { window.__openFlowCard('gli'); } catch (e) { C.err = String(e); }
    C.openSyncMs = +(performance.now() - a).toFixed(1);
    return raf2().then(function(){ C.openSettledMs = +(performance.now() - a).toFixed(0); C.openGap = gapStop(); C.openNat = R.nat;
      var card = null; document.querySelectorAll('.card.open, .flowcard.open, [class*="card"].open').forEach(function(c){ if (!card && c.querySelector('.pset')) card = c; });
      if (!card) { document.querySelectorAll('.pset').forEach(function(p){ var c = p.closest('.open'); if (c && !card) card = c; }); }
      C.cardFound = !!card; if (!card) return;
      var pas = card.querySelectorAll('.pset .pa'), pn = card.querySelector('.pset .pn');
      C.nameBefore = pn ? pn.textContent : '';
      R.nat = {}; gapStart(); var b = performance.now(); pas[1].click(); C.stepSyncMs = +(performance.now() - b).toFixed(1);
      return raf2().then(function(){ C.stepSettledMs = +(performance.now() - b).toFixed(0); C.stepGap = gapStop(); C.stepNat = R.nat; C.nameAfter = pn ? pn.textContent : '';
        return wait(300); }).then(function(){
        R.nat = {}; gapStart(); var c = performance.now(); pn.click();
        return new Promise(function(res){ var n = 0; (function poll(){ var m = document.querySelector('.pmenu'); if (m || n++ > 100) { C.menuMs = +(performance.now() - c).toFixed(0); C.menuRows = m ? m.querySelectorAll('.pi').length : -1; C.menuGap = gapStop(); C.menuNat = R.nat; res(); } else setTimeout(poll, 20); })(); });
      }).then(function(){ natOn = false; try { document.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); } catch (e) {} }); });
  }
  function sectionD(){   /* the wavetable arrows on an import folder: do they walk the folder? */
    R.phase = 'D';
    var D = R.D; D.cats = []; D.steps = []; D.rapid = {};
    var t0 = performance.now();
    return new Promise(function(res){ window.__wtAllCats('a', res); }).then(function(cats){
      D.allCatsMs = +(performance.now() - t0).toFixed(0);
      cats.forEach(function(c){ D.cats.push(c.label + ':' + (c.items || []).length + ':' + ((c.items || [])[0] || {}).kind); });
      var imp = null; cats.forEach(function(c){ if (!imp && c.items && c.items.length >= 4 && c.items[0].kind === 'import') imp = c; });
      D.folder = imp ? imp.label : null; if (!imp) return;
      window.__wtFolder.a = imp.label;
      var names = imp.items.map(function(it){ return it.name; }); D.first6 = names.slice(0, 6);
      var WT = window.wtWaterfall;
      function snap(){ var d = document.getElementById('osc-a-preset-display'); return { disp: d ? d.textContent : '', nm: WT && WT.importName ? WT.importName.a : '', path: (window.__wtPath.a || '').split('/').pop() }; }
      /* wtApplyItem is module-local: its import branch, inline */
      function applyImport(it){ var f = NF('loadWavetableByPath'); return Promise.resolve(f('a', it.path)).then(function(){
        var d = document.getElementById('osc-a-preset-display'); if (d) d.textContent = it.name; var snf = NF('setWavetableName'); if (snf) snf('a', it.name);
        if (WT) { WT.imported.a = true; WT.importName = WT.importName || {}; WT.importName.a = it.name; try { WT.fetch('a'); } catch (e) {} } window.__wtPath.a = it.path; }); }
      return applyImport(imp.items[0]).then(function(){ return wait(900); }).then(function(){ D.start = snap();
        var seq = Promise.resolve();
        for (var i = 0; i < 5; i++) seq = seq.then(function(){ var a = performance.now(); natOn = true; R.nat = {}; window.wtStepPreset('a', 1);
          return wait(700).then(function(){ var s = snap(); s.nat = R.nat; s.busyAfter = 0; D.steps.push(s); natOn = false; }); });
        return seq; }).then(function(){
        /* rapid: five presses 60 ms apart — how many moved? */
        var before = snap().nm; var moved = 0, last = before;
        var seq2 = Promise.resolve();
        for (var j = 0; j < 5; j++) seq2 = seq2.then(function(){ window.wtStepPreset('a', 1); return wait(60).then(function(){ var n = snap().nm; if (n !== last) { moved++; last = n; } }); });
        return seq2.then(function(){ return wait(1600); }).then(function(){ var n = snap().nm; if (n !== last) moved++; D.rapid = { pressed: 5, moved: moved, end: snap() }; });
      });
    });
  }
  function sectionE(){   /* the dice: each step's sync cost, and the wall time to the restore */
    R.phase = 'E';
    var oST = window.setTimeout, oRestore = window.__fxrRestoreAll;
    function roll(label){ return new Promise(function(res){
      var e = { label: label, steps: [], nat: {} }; R.E.push(e); var a = performance.now(); natOn = true; R.nat = {};
      gapStart();
      window.setTimeout = function(cb, ms){ var args = Array.prototype.slice.call(arguments, 2);
        if (ms === 260 || ms === 350) return oST.apply(window, [function(){ var s = performance.now(); try { cb.apply(null, args); } finally { e.steps.push({ at: +(s - a).toFixed(0), ms: +(performance.now() - s).toFixed(1), d: ms }); } }, ms]);
        return oST.apply(window, arguments); };
      window.__fxrRestoreAll = function(){ var s = performance.now(); try { return oRestore.apply(this, arguments); } finally { e.restoreAt = +(s - a).toFixed(0); e.restoreMs = +(performance.now() - s).toFixed(1); } };
      var s0 = performance.now(); try { window.__tpDice(); } catch (x) { e.err = String(x); } e.callSyncMs = +(performance.now() - s0).toFixed(1);
      var n = 0; (function poll(){ if (e.restoreAt != null || n++ > 300) { e.wallMs = e.restoreAt != null ? e.restoreAt : -1; e.gap = gapStop(); e.nat = R.nat; natOn = false;
          window.setTimeout = oST; window.__fxrRestoreAll = oRestore; res(); } else oST(poll, 20); })(); }); }
    return roll('roll1').then(function(){ return wait(2200); }).then(function(){ return roll('roll2'); }).then(function(){ return wait(2200); }).then(function(){ return roll('roll3'); });
  }
  wait(1500).then(sectionA).then(sectionB).then(sectionC).then(sectionD).then(sectionE)
    .then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push('FAIL ' + String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp35 started';
})();
