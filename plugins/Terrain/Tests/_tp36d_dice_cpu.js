/* tp36d — SAVE THE ROLLS. Crazy rolls, cap off, chord held; each roll is saved as /tmp/tp36rolls/roll-N.terrain through
   the plugin's own savePatchFile, so the heaviest one can be profiled offline (Tests/au_preset_census.cpp hold/tail with
   TP_BANK=/tmp/tp36rolls, the sampler, TERRAIN_PROFILE). The beacon (DSP%, voices) is printed by the harness. */
(function(){
  var R = { phase: 'start', rolls: [], errs: [], t0: performance.now() }; window.__tp36d = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp36d:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function series(n){ return function(){ window.__tpDiceCap('off'); window.__tpDiceLevelSet('crazy'); var seq = Promise.resolve();
    for (var i = 0; i < n; i++) (function(i){ seq = seq.then(function(){ var t = (performance.now() - R.t0) / 1000; window.__tpDice(); return wait(900).then(function(){ var e = window.__tpDiceEstimate();
      var f = NF('savePatchFile'); var pr = f ? Promise.resolve(f('/tmp/tp36rolls/roll-' + i + '.terrain', JSON.stringify({ name: 'roll-' + i, bank: 'tp36', author: 'dice' }))) : Promise.resolve('no native');
      return pr.then(function(res){ R.rolls.push({ i: i, at: +t.toFixed(1), est: e ? e.est : null, aim: window.__tpDiceAim(), saved: String(res).slice(0, 40) }); publish(); return wait(8100); }); }); }); })(i);
    return seq; }; }
  wait(2500).then(function(){ R.phase = 'held'; }).then(series(6)).then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push(String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp36d started';
})();
