/* tp41 — THE PATCHER'S RULES in the real plugin (the WebView inside the installed AU, /tmp/tpexp Tests/_tp41_patcher_rules_probe.js).
   On the Patcher: a reverb on the canvas, osc A on; cable A → reverb through the canvas's own connect() and read SYN_RVB_TAPS
   (bit 0 = a direct tap) + SYN_RVB_SRC_A; spawn a Chop from the canvas (its pills must be 0); cable A → Chop, Chop → reverb and
   read FLOW_CHOP_INLINE / FLOW_CHOP_RANK / the reverb's rank. */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [] }; window.__tp41p = R;
  window.addEventListener('error', function(e){ R.winErrs.push(String(e.message || e).slice(0, 120) + ' @' + (e.lineno || 0)); });
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp41p:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function P(ids){ var g = NF('getSynParam'); return Promise.all(ids.map(function(id){ return Promise.resolve(g(id)).then(function(v){ return [id, Math.round((+v) * 1e6) / 1e6]; }); })); }
  var oi = function(n){ var outs = n.ports.filter(function(q){ return q.kind === 'out'; }); var i = outs.findIndex(function(p){ return p.el && p.el.dataset.t === 'a'; }); return i < 0 ? 0 : i; };
  wait(3000).then(function(){ setActivePanel('syn'); try { if (!(window.__fxrDevs() || []).some(function(d){ return d.core === 'reverb'; })) window.__fxrAdd('reverb'); } catch (e) { R.errs.push('add: ' + e); } return wait(600); })
    .then(function(){ setActivePanel('tp'); ss('SYN_OSC_A_ENABLE').setNormalisedValue(1); return wait(2500); })
    .then(function(){ var nb = window.__tpNodeByKey, a = nb('osc-a'), fx = nb('fx-reverb-1'); R.have = { a: !!a, fx: !!fx, nodes: window.__tpLayout().nodes };
      if (!(a && fx)) throw new Error('nodes missing ' + JSON.stringify(R.have));
      R.okDirect = window.__tpConnect(a, 'out', oi(a), fx, 'in', 0); return wait(700); })
    .then(function(){ return P(['SYN_RVB_SRC_A', 'SYN_RVB_TAPS']); })
    .then(function(rows){ R.afterDirect = rows; R.cablesDirect = window.__tpDerive().map(function(c){ return c.id; }).filter(function(id){ return /fx-reverb-1|osc-a/.test(id); });
      window.__tpAdd('flow:chop', 420, 420); return wait(1200); })
    .then(function(){ return P(['FLOW_CHOP_SRC_A', 'FLOW_CHOP_SRC_B', 'FLOW_CHOP_SRC_E', 'FLOW_CHOP_INLINE']); })
    .then(function(rows){ R.chopSpawn = rows; var nb = window.__tpNodeByKey, a = nb('osc-a'), ch = nb('flow-chop'), fx = nb('fx-reverb-1'); R.haveChop = !!ch;
      if (!ch) throw new Error('no chop node'); R.okAChop = window.__tpConnect(a, 'out', oi(a), ch, 'in', 0); return wait(500).then(function(){ R.okChopFx = window.__tpConnect(ch, 'out', oi(ch), fx, 'in', 0); return wait(900); }); })
    .then(function(){ return P(['FLOW_CHOP_SRC_A', 'FLOW_CHOP_TAPS', 'FLOW_CHOP_INLINE', 'FLOW_CHOP_RANK', 'SYN_RVB_RANK', 'SYN_RVB_SRC_A']); })
    .then(function(rows){ R.afterInline = rows; R.cablesInline = window.__tpDerive().map(function(c){ return c.id; }).filter(function(id){ return /fx-reverb-1|osc-a|flow-chop/.test(id); }); R.phase = 'done'; publish(); },
          function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp41p started';
})();
