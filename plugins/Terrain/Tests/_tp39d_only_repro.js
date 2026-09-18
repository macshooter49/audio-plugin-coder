/* tp39d — Max's model in the real plugin: Keys+Pads with nothing in Only = the WHOLE preset; Only: Effects = just the
   effects; Only: Effects + Modulation = effects + routes. Each step counts what changed in the live parameters. */
(function(){
  var R = { phase: 'start', errs: [], steps: [] }; window.__tp39d = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39d:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function v(id){ var s = ss(id); return s ? Math.round(s.getNormalisedValue() * 1000) : null; }
  function snap(){ var s = { en: 'ABCDEFGH'.split('').map(function(o){ return v('SYN_OSC_' + o + '_ENABLE') + '/' + v('SYN_OSC_' + o + '_ENGINE'); }).join(','), env: [v('SYN_ENV_FLT_D'), v('SYN_ENV_AMP_R'), v('SYN_ENV_FLT_A')].join(','), flt: [v('SYN_FILTER1_CUTOFF'), v('SYN_FILTER1_RES')].join(',') };
    try { s.fx = (window.__fxrDevs ? window.__fxrDevs() : []).map(function(d){ return d.core + (d.inst || ''); }).join(','); } catch (e) { s.fx = '?'; }
    try { s.routes = (window.__tiRoutes ? window.__tiRoutes() : []).map(function(r){ return r.s + '>' + r.d; }).sort().join(','); } catch (e) { s.routes = '?'; }
    try { s.chain = (window.__flowChain ? window.__flowChain() : []).join(','); } catch (e) { s.chain = '?'; } return s; }
  function step(name, aims, only){ return function(){ window.__tpDiceAims(aims); window.__tpDiceBlocks(only); var before = snap(); document.getElementById('dice-btn').click();
    return wait(3000).then(function(){ var after = snap(), ch = {}; Object.keys(after).forEach(function(k){ ch[k] = after[k] !== before[k]; }); R.steps.push({ step: name, changed: ch, aim: window.__tpDiceLastAim, blocks: window.__tpDiceBlocks() }); publish(); }); }; }
  wait(3000).then(step('whole:keys+pads', ['keys', 'pads'], {}))
    .then(step('whole:keys+pads (2)', ['keys', 'pads'], {}))
    .then(step('only:effects', ['keys', 'pads'], { fx: 1 }))
    .then(step('only:effects+modulation', ['keys', 'pads'], { fx: 1, mod: 1 }))
    .then(step('only:flow', ['keys', 'pads'], { flow: 1 }))
    .then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39d started';
})();
