/* tp36 — THE DICE, SIMULATED. Max: "randomize the craziest ones and see ... run simulation". Runs in the real WebView
   (Tests/mac_ui_exp.mm, TPEXP_CHORD8=1 for an 8-note chord, the plugin's CPU beacon armed by terrain-cpu-on.txt): crazy
   rolls with the cap OFF, then crazy rolls with a 40% cap, 9 s each with the chord held. The roll times and the cap's own
   estimate are published; the harness prints the beacon (the DSP% the plugin measured) with elapsed time, so the two
   line up offline. */
(function(){
  var R = { phase: 'start', rolls: [], errs: [], restrike: 0, t0: performance.now() }; window.__tp36 = R;   /* restrike: the harness re-strikes the chord when it changes (a roll can silence held voices) */
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp36:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function series(cap, n){ return function(){ window.__tpDiceCap(cap); window.__tpDiceLevelSet('crazy'); var seq = Promise.resolve();
    for (var i = 0; i < n; i++) seq = seq.then(function(){ var t = (performance.now() - R.t0) / 1000; window.__tpDice(); return wait(700).then(function(){ var e = window.__tpDiceEstimate();
      R.rolls.push({ cap: cap, at: +t.toFixed(1), est: e ? e.est : null, trims: e ? e.trims.length : null, aim: window.__tpDiceAim() }); R.restrike++; publish(); return wait(8300); }); });
    return seq; }; }
  wait(2500).then(function(){ R.phase = 'off'; }).then(series('off', 10)).then(function(){ R.phase = 'cap40'; }).then(series(40, 10))
    .then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push(String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp36 started';
})();
