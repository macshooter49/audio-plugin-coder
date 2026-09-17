/* tp36c — WHICH KNOB MAKES A VOICE SIX TIMES DEARER? Crazy rolls, cap off, chord held; every setSynParam the roll writes is
   captured and summarised per roll (engines, unison, blends, sub, feedback, filters, LFO count); the harness prints the
   beacon's voice cost next to it. */
(function(){
  var R = { phase: 'start', rolls: [], errs: [], t0: performance.now() }; window.__tp36c = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp36c:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  var writes = {}, capOn = false;
  try { var be = window.__JUCE__.backend, oEmit = be.emitEvent;
    be.emitEvent = function(ev, payload){ if (capOn && ev === '__juce__invoke' && payload && payload.name === 'setSynParam') writes[String(payload.params[0])] = +payload.params[1]; return oEmit.apply(this, arguments); }; } catch (e) {}
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function summary(w){ var o = { osc: [], f: [], lfo: 0, on: 0 };
    'ABCDEFGH'.split('').forEach(function(x){ var p = 'SYN_OSC_' + x + '_'; if (!(w[p + 'ENABLE'] > 0.5)) return; o.on++;
      var bl = 0; for (var k = 1; k <= 4; k++) if ((w[p + 'BLEND' + k + '_DEPTH'] || 0) > 0.01) bl++;
      o.osc.push(x + ':e' + Math.round((w[p + 'ENGINE'] || 0) * 6) + ' u' + (1 + Math.round((w[p + 'UNISON'] || 0) * 15)) + ' bl' + bl + ' sub' + ((w[p + 'SUB_MIX'] || 0) > 0.05 ? 1 : 0) + ' fb' + Math.round((w[p + 'FEEDBACK'] || w[p + 'WT_FEEDBACK'] || 0) * 10) + ' fold' + Math.round((w[p + 'FOLD_AMT'] || 0) * 10) + ' warp' + Math.round((w[p + 'WARP_AMOUNT'] || 0) * 10)); });
    ['1', '2'].forEach(function(f){ var p = 'SYN_FILTER' + f + '_'; o.f.push('t' + Math.round((w[p + 'TYPE'] || 0) * 117) + ' d' + (w[p + 'DRV'] || 0).toFixed(2) + ' r' + (w[p + 'RES'] || 0).toFixed(2) + ' pd' + (w[p + 'PDRV'] || 0).toFixed(2)); });
    for (var i = 1; i <= 10; i++) if (w['LFO' + i + '_SHAPE'] != null || w['LFO' + i + '_RATE'] != null) o.lfo++;
    o.nWrites = Object.keys(w).length; o.other = Object.keys(w).filter(function(k){ return /STACK|INTERP|SPEC|GRAIN|HARM_|MODAL|OSAMP|QUAL/.test(k); }).length; return o; }
  function series(n){ return function(){ window.__tpDiceCap('off'); window.__tpDiceLevelSet('crazy'); var seq = Promise.resolve();
    for (var i = 0; i < n; i++) seq = seq.then(function(){ writes = {}; capOn = true; var t = (performance.now() - R.t0) / 1000; window.__tpDice(); return wait(900).then(function(){ capOn = false; var e = window.__tpDiceEstimate();
      var s = summary(writes); s.at = +t.toFixed(1); s.est = e ? e.est : null; s.aim = window.__tpDiceAim(); R.rolls.push(s); publish(); return wait(8100); }); });
    return seq; }; }
  wait(2500).then(function(){ R.phase = 'held'; }).then(series(7)).then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push(String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp36c started';
})();
