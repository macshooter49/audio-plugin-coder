/* tp43 — DAW capture on/off through the real plugin's natives (tpexp): off → getCaptureEnabled false; on → true. Leaves it ON. */
(function(){ var R = { phase: 'start', errs: [] }; function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); } function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++; if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp43c:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250); pubT = setInterval(publish, 1500);
  var g = NF('getCaptureEnabled'), s = NF('setCaptureEnabled');
  wait(2000).then(function(){ return Promise.resolve(g()); }).then(function(v){ R.initial = v; return Promise.resolve(s(0)); }).then(function(v){ R.afterOff = v; return wait(300); }).then(function(){ return Promise.resolve(g()); }).then(function(v){ R.readOff = v; var btn = document.getElementById('capture-off-btn'); R.settingsRow = !!btn; return Promise.resolve(s(1)); }).then(function(v){ R.afterOn = v; return Promise.resolve(g()); }).then(function(v){ R.readOn = v; R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e)); R.phase = 'failed'; publish(); });
  return 'tp43c started'; })();
