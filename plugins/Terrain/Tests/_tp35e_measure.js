/* tp35e — the arrow's 250 ms race: an import folder is being stepped while the host thread is busy
   (a 1.6 s getPresets, as the Glitch card's first open causes). Does the arrow still step the folder? */
(function(){
  var R = { phase: 'start', errs: [], E: [] }; window.__tp35e = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp35e:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  var WT = window.wtWaterfall;
  function snap(){ var d = document.getElementById('osc-a-preset-display'); return { disp: d ? d.textContent : '', nm: WT && WT.importName ? WT.importName.a : '', path: (window.__wtPath.a || '').split('/').pop(), folder: window.__wtFolder.a || null }; }
  function applyImport(it){ var f = NF('loadWavetableByPath'); return Promise.resolve(f('a', it.path)).then(function(){
    var d = document.getElementById('osc-a-preset-display'); if (d) d.textContent = it.name; var snf = NF('setWavetableName'); if (snf) snf('a', it.name);
    if (WT) { WT.imported.a = true; WT.importName = WT.importName || {}; WT.importName.a = it.name; try { WT.fetch('a'); } catch (e) {} } window.__wtPath.a = it.path; }); }
  wait(1500).then(function(){ R.phase = 'E'; return new Promise(function(res){ window.__wtAllCats('a', res); }); }).then(function(cats){
    var imp = null; cats.forEach(function(c){ if (!imp && c.items && c.items.length >= 6 && c.items[0].kind === 'import') imp = c; });
    if (!imp) { R.errs.push('no import folder'); return; }
    window.__wtFolder.a = imp.label; R.folder = imp.label; R.first = imp.items.slice(0, 5).map(function(i){ return i.name; });
    return applyImport(imp.items[1]).then(function(){ return wait(800); }).then(function(){
      var e = { label: 'quiet host', before: snap() }; window.wtStepPreset('a', 1);
      return wait(900).then(function(){ e.after = snap(); R.E.push(e); });
    }).then(function(){
      /* now the same press while the host thread is inside a 1.6 s native */
      var e = { label: 'busy host (getPresets gli in flight)', before: snap() };
      var t0 = performance.now(); Promise.resolve(NF('getPresets')('gli')).then(function(){ e.getPresetsMs = +(performance.now() - t0).toFixed(0); });
      window.wtStepPreset('a', 1);
      return wait(2500).then(function(){ e.after = snap(); R.E.push(e); });
    }).then(function(){
      var e = { label: 'quiet again', before: snap() }; window.wtStepPreset('a', 1);
      return wait(900).then(function(){ e.after = snap(); R.E.push(e); });
    });
  }).then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push('FAIL ' + String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp35e started';
})();
