/* tp43 — NOISE 2 OWNS THE SAME LIBRARY (tpexp). Scan the factory, load one sound into Noise 2 by the instance-aware native,
   read its peaks + selection; Noise 1's stay untouched; then clear Noise 2. */
(function(){ var R = { phase: 'start', errs: [] }; function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); } function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++; if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp43n:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250); pubT = setInterval(publish, 1500);
  var scan = NF('scanNoiseFactory'), loadF = NF('loadNoiseFactory'), peaks = NF('getNoiseWavePeaks'), sel = NF('getNoiseSampleSel'), setSel = NF('setNoiseSampleSel'), clear = NF('clearNoiseSample');
  var cat = null, file = null;
  wait(2000).then(function(){ return Promise.resolve(scan()); }).then(function(js){ var o = JSON.parse(js || '{}'); var cats = Object.keys(o.cats || {}); R.cats = cats.length; cat = cats[0]; file = cat ? (o.cats[cat] || [])[0] : null; R.pick = [cat, file]; if (!file) throw new Error('no factory sound');
      return Promise.resolve(loadF(cat, file, 2)); }).then(function(r){ R.load2 = String(r); return wait(600); })
    .then(function(){ return Promise.resolve(peaks(2)); }).then(function(p){ R.peaks2 = String(p || '').length; return Promise.resolve(peaks(1)); }).then(function(p){ R.peaks1 = String(p || '').length;
      return Promise.resolve(setSel(JSON.stringify({ kind: 'factory', cat: cat, file: file, name: file }), 2)); }).then(function(){ return Promise.resolve(sel(2)); }).then(function(s2){ R.sel2 = String(s2 || '').slice(0, 60); return Promise.resolve(sel(1)); }).then(function(s1){ R.sel1 = String(s1 || '').slice(0, 60);
      return Promise.resolve(clear(2)); }).then(function(){ return wait(300); }).then(function(){ return Promise.resolve(peaks(2)); }).then(function(p){ R.peaks2AfterClear = String(p || '').length; return Promise.resolve(setSel('', 2)); }).then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e)); R.phase = 'failed'; publish(); });
  return 'tp43n started'; })();
