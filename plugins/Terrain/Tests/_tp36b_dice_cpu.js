/* tp36b — WHY DOES IT CLIMB? Crazy rolls, cap off: four with the chord held straight through, then four where the harness
   re-strikes the chord after each roll (old voices released). Publishes the roll times, the flow cards the roll added and
   the Patcher's node count; the harness prints the beacon (DSP%, voice count) with elapsed time. */
(function(){
  var R = { phase: 'start', rolls: [], errs: [], restrike: 0, t0: performance.now() }; window.__tp36b = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp36b:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function flowNow(){ try { var f = window.__flowFeedPush || {}; var on = []; ['arp', 'chop', 'gli'].forEach(function(k){ (f[k] || []).forEach(function(x, i){ if (x && x.on) on.push(k + (i + 1)); }); }); return on.join(' '); } catch (e) { return '?'; } }
  function series(restrike, n){ return function(){ window.__tpDiceCap('off'); window.__tpDiceLevelSet('crazy'); var seq = Promise.resolve();
    for (var i = 0; i < n; i++) seq = seq.then(function(){ var t = (performance.now() - R.t0) / 1000; window.__tpDice(); return wait(700).then(function(){ var e = window.__tpDiceEstimate();
      R.rolls.push({ at: +t.toFixed(1), est: e ? e.est : null, flow: flowNow(), nodes: window.__tpNodes ? window.__tpNodes().length : -1, restrike: restrike });
      if (restrike) R.restrike++; publish(); return wait(8300); }); });
    return seq; }; }
  wait(2500).then(function(){ R.phase = 'held'; }).then(series(false, 4)).then(function(){ R.phase = 'restruck'; }).then(series(true, 4))
    .then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push(String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp36b started';
})();
