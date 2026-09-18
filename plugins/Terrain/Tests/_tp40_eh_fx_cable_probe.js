/* tp40 — Max: "make sure I can put effects on E, F, G and H" (the canvas said 'That does not fit'). In the real plugin, on the
   Patcher: a reverb on the canvas, oscillators E and B switched on, cable E → reverb and B → reverb through the canvas's own
   connect(); read the reverb's E and B pills (SYN_RVB_SRC_E / SRC_B) and the device model's route row. */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [] }; window.__tp40c = R;
  window.addEventListener('error', function(e){ R.winErrs.push(String(e.message || e).slice(0, 120) + ' @' + (e.lineno || 0)); });
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp40c:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  wait(3000).then(function(){ setActivePanel('syn'); try { if (!(window.__fxrDevs() || []).length) window.__fxrAdd('reverb'); } catch (e) { R.errs.push('add: ' + e); } return wait(600); })
    .then(function(){ setActivePanel('tp'); ss('SYN_OSC_E_ENABLE').setNormalisedValue(1); ss('SYN_OSC_B_ENABLE').setNormalisedValue(1); return wait(2500); })
    .then(function(){ R.nodes = window.__tpLayout().nodes; var nb = window.__tpNodeByKey, e = nb('osc-e'), b = nb('osc-b'), fx = nb('fx-reverb-1'); R.have = { e: !!e, b: !!b, fx: !!fx };
      if (!(e && b && fx)) throw new Error('nodes missing ' + JSON.stringify(R.have));
      var oi = function(n){ var i = -1; n.ports.forEach(function(p, k){ if (p.kind === 'out' && p.el && p.el.dataset.t === 'a' && i < 0) i = n.ports.filter(function(q){ return q.kind === 'out'; }).indexOf(p); }); return i; };
      var ii = function(n){ var i = -1; (n.ports || []).forEach(function(p, k){ if (p.kind === 'in' && i < 0) i = (n.ports.filter(function(q){ return q.kind === 'in'; })).indexOf(p); }); return i < 0 ? 0 : i; };
      R.okE = window.__tpConnect(e, 'out', oi(e), fx, 'in', ii(fx)); R.okB = window.__tpConnect(b, 'out', oi(b), fx, 'in', ii(fx)); return wait(800); })
    .then(function(){ var g = NF('getSynParam'); return Promise.all(['SYN_RVB_SRC_E', 'SYN_RVB_SRC_B', 'SYN_RVB_SRC_A'].map(function(id){ return Promise.resolve(g(id)).then(function(v){ return [id, +v]; }); })); })
    .then(function(rows){ R.params = rows; R.route = (window.__fxrDevs()[0] || {}).route; R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp40c started';
})();
