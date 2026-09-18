/* tp39 — in the real plugin: crazy rolls and drum rolls; after each, every enabled oscillator's engine and whether the
   sample-family ones carry a landed one-shot (getOscSamplePayload), the paths the dice picked, the blends it stacked. */
(function(){
  var R = { phase: 'start', rolls: [], errs: [] }; window.__tp39 = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 2000);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function census(label){ var g = NF('getOscSamplePayload'); var oscs = 'abcdefgh'.split('').filter(function(o){ var e = ss('SYN_OSC_' + o.toUpperCase() + '_ENABLE'); return e && e.getNormalisedValue() > 0.5; });
    return Promise.all(oscs.map(function(o){ var eng = Math.round(ss('SYN_OSC_' + o.toUpperCase() + '_ENGINE').getNormalisedValue() * 6); return Promise.resolve(g ? g(o) : '').then(function(pl){ return { o: o, eng: eng, has: !!(pl && String(pl).length > 2), pick: ((window.__diceLastOneShot || {})[o] || '').split('/').slice(-2).join('/'), blend: ((window.__diceLastBlend || {})[o] || '').split('/').slice(-2).join('/') }; }); }))
      .then(function(rows){ R.rolls.push({ label: label, aim: window.__tpDiceLastAim, oscs: rows, est: (window.__tpDiceEstimate() || {}).est }); publish(); }); }
  function series(label, aims, lvl, n){ return function(){ window.__tpDiceCap('off'); window.__tpDiceAims(aims); window.__tpDiceLevelSet(lvl); var seq = Promise.resolve();
    for (var i = 0; i < n; i++) seq = seq.then(function(){ window.__tpDice(); return wait(6500).then(function(){ return census(label); }); });
    return seq; }; }
  wait(3000).then(function(){ R.phase = 'crazy'; }).then(series('crazy', [], 'crazy', 5)).then(function(){ R.phase = 'drums'; }).then(series('drums', ['drums'], 'heavy', 4))
    .then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp39 started';
})();
