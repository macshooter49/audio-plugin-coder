/* tp37 — DOES THE DSP OBEY THE GLOBAL CLOCK? In the real WebView (Tests/mac_ui_exp.mm, chord held): set ten different
   LFO rates, read the DSP's own LFO phases (window.__mvLfoPh, the truth feed) over a second → ten different speeds;
   turn the global clock on at ~3 Hz → ten equal speeds at the global rate; off → the ten speeds return. */
(function(){
  var R = { phase: 'start', errs: [] }; window.__tp37 = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp37:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function set(id, v){ var s = ss(id); if (s) s.setNormalisedValue(v); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function rateNorm(hz){ return Math.pow((hz - 0.01) / 39.99, 0.3); }
  /* speed of each LFO from the phase feed: unwrap over ~1.2 s */
  function speeds(){ return new Promise(function(res){ var t0 = performance.now(), last = null, acc = [0,0,0,0,0,0,0,0,0,0], n = 0;
    var iv = setInterval(function(){ var ph = window.__mvLfoPh; if (!ph || ph.length < 10) return; if (last) { for (var i = 0; i < 10; i++) { var d = ph[i] - last[i]; if (d < -0.5) d += 1; if (d > 0.5) d -= 1; acc[i] += d; } } last = ph.slice(); n++;
      if (performance.now() - t0 > 1200) { clearInterval(iv); var dt = (performance.now() - t0) / 1000; res(acc.map(function(a){ return +(a / dt).toFixed(2); })); } }, 16); }); }
  wait(1500).then(function(){ R.phase = 'own';
    for (var i = 1; i <= 10; i++) { set('LFO' + i + '_SYNC', 0); set('LFO' + i + '_RATE', rateNorm(0.5 + 0.5 * i)); set('LFO' + i + '_DEPTH', 1); }
    set('LFO_GLOBAL', 0); return wait(600); }).then(speeds).then(function(v){ R.own = v; R.phase = 'global';
    set('LFO_GLOBAL_SYNC', 0); set('LFO_GLOBAL_RATE', rateNorm(3.0)); set('LFO_GLOBAL', 1); return wait(600); }).then(speeds).then(function(v){ R.global = v; R.phase = 'off';
    set('LFO_GLOBAL', 0); return wait(600); }).then(speeds).then(function(v){ R.off = v; R.phase = 'done'; publish(); },
    function(e){ R.errs.push(String(e).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp37 started';
})();
