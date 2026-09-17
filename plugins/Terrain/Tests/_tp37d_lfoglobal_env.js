/* tp37d — the envelope route, calibrated against a known dest: Env 2 → LFO 1 rate (673, global off) must speed LFO 1
   up as the filter envelope climbs; then the same route onto the GLOBAL rate (5199, global on). Speeds from the DSP
   phase feed at 0.25-0.85 s and 2.5-3.1 s after a re-struck chord. */
(function(){
  var R = { phase: 'start', errs: [], restrike: 0 }; window.__tp37d = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp37d:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function set(id, v){ var s = ss(id); if (s) s.setNormalisedValue(v); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function rateNorm(hz){ return Math.pow((hz - 0.01) / 39.99, 0.3); }
  function speedWindow(ms){ return new Promise(function(res){ var t0 = performance.now(), last = null, acc = 0;
    var iv = setInterval(function(){ var ph = window.__mvLfoPh; if (!ph || ph.length < 1) return; if (last !== null) { var d = ph[0] - last; if (d < -0.5) d += 1; if (d > 0.5) d -= 1; acc += d; } last = ph[0];
      if (performance.now() - t0 > ms) { clearInterval(iv); res(+(acc / ((performance.now() - t0) / 1000)).toFixed(2)); } }, 16); }); }
  function measure(label){ R.restrike++; publish(); return wait(250).then(function(){ return speedWindow(600); }).then(function(early){ return wait(1650).then(function(){ return speedWindow(600); }).then(function(late){ R[label] = { early: early, late: late }; }); }); }
  function clearRoutes(){ try { var f = window.Juce.getNativeFunction('setSynthMod'); if (f) f('[]'); } catch (e) {} }
  wait(1500).then(function(){ R.phase = 'setup';
    for (var i = 1; i <= 10; i++) { set('LFO' + i + '_SYNC', 0); set('LFO' + i + '_RATE', rateNorm(1.0)); set('LFO' + i + '_DEPTH', 1); }
    set('SYN_ENV_FLT_A', 0.9); set('SYN_ENV_FLT_S', 1.0); set('SYN_ENV_FLT_D', 0.5);
    set('LFO_GLOBAL', 0); try { window.__openLfoCard(); } catch (e) {} return wait(900); })
  .then(function(){ R.phase = 'own-routed'; try { window.__tiAddRoute(2, 0, 673); window.__tiSetDepth('env2', 673, 0.8); } catch (e) { R.errs.push('r673 ' + e); } return wait(700).then(function(){ return measure('lfo1_env_on_673'); }); })
  .then(function(){ R.phase = 'global-routed'; set('LFO_GLOBAL_SYNC', 0); set('LFO_GLOBAL_RATE', rateNorm(1.0)); set('LFO_GLOBAL', 1);
    try { window.__tiAddRoute(2, 0, 5199); window.__tiSetDepth('env2', 5199, 0.8); } catch (e) { R.errs.push('r5199 ' + e); }
    var gm = null; try { gm = window.Juce.getNativeFunction('getSynthMod'); } catch (e) {} return (gm ? Promise.resolve(gm()) : Promise.resolve('')).then(function(j){ R.modJson = String(j).slice(0, 200); return wait(700); }).then(function(){ return measure('global_env_on_5199'); }); })
  .then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp37d started';
})();
