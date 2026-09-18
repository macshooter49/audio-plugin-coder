/* tp39b — do the bank-B oscillators (E-H) take a library one-shot, and does the payload record it? Load the same drum into
   A and E, read getOscSamplePayload for both, then save the patch so the AU harness can solo E. */
(function(){
  var R = { phase: 'start', errs: [] }; window.__tp39b = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39b:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function set(id, v){ var s = ss(id); if (s) s.setNormalisedValue(v); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  wait(2500).then(function(){ R.phase = 'load'; return Promise.resolve(NF('scanSampleFactory')()); }).then(function(j){ var d = JSON.parse(j); var f = d.path + '/Drums/' + d.cats.Drums[0]; R.file = f;
    ['a', 'e'].forEach(function(o){ set('SYN_OSC_' + o.toUpperCase() + '_ENABLE', 1); set('SYN_OSC_' + o.toUpperCase() + '_ENGINE', 1 / 6); window.__sampLoadPath(o, f); });
    return wait(2500); }).then(function(){ var g = NF('getOscSamplePayload'); return Promise.all(['a', 'e'].map(function(o){ return Promise.resolve(g(o)).then(function(p){ return { o: o, len: String(p || '').length }; }); })); })
  .then(function(rows){ R.payload = rows; var f = NF('savePatchFile'); return Promise.resolve(f('/tmp/tp39rolls/bankb.terrain', JSON.stringify({ name: 'bankb', bank: 'tp39', author: 'probe' }))); })
  .then(function(res){ R.saved = String(res).slice(0, 30); R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp39b started';
})();
