/* tp39f — in the real plugin: crazy plucks rolls until the chain holds an arp; then Arp Latch is OFF, the arp's dots are drawn,
   every flow card in the chain carries at least one LFO route, and the chop's TIME is never a bar-long grid. */
(function(){
  var R = { phase: 'start', errs: [], rolls: [] }; window.__tp39fa = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39fa:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function kd(n){ try { return window.__tpKnobDest(n); } catch (e) { return null; } }
  var SETS = { arp: ['BLEND','GATE','GLIDE','MORPH'].map(function(k){ return 'FLOW_ARP_' + k; }), glitch: ['BLEND','DECAY','DEJAVU','BURST'].map(function(k){ return 'FLOW_GLI_' + k; }), chop: ['BLEND','GRIT','FILTER','DETUNE'].map(function(k){ return 'FLOW_CHOP_' + k; }), drift: ['VARY','DRIFT','WOBBLE','GLIDE'].map(function(k){ return 'FLOW_RBN_' + k; }) };
  function inst(m){ var mm = /^([a-z]+)(\d*)$/.exec(m); return { kind: mm[1], n: mm[2] ? +mm[2] : 1 }; }
  function roll(){ return new Promise(function(res){ window.__tpDiceAims(['plucks']); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); document.getElementById('dice-btn').click(); setTimeout(res, 3500); }).then(function(){
    var g = NF('getSynParam'); var chain = window.__flowChain ? window.__flowChain() : [], routes = window.__tiRoutes ? window.__tiRoutes() : [];
    var per = chain.map(function(m){ var i = inst(m), base = SETS[i.kind] || []; var names = base.map(function(n){ return i.n > 1 ? n.replace(/^(FLOW_[A-Z]+)_/, '$1' + i.n + '_') : n; }); var dests = names.map(kd).filter(function(d){ return d != null; });
      return { card: m, dests: dests.length, hits: routes.filter(function(r){ return dests.indexOf(r.d) >= 0; }).length }; });
    var latchIds = ['FLOW_ARP_LATCH', 'FLOW_ARP2_LATCH', 'FLOW_ARP3_LATCH'];
    return Promise.all(latchIds.map(function(id){ return Promise.resolve(g(id)).then(function(v){ return v == null ? null : +v; }); })).then(function(latches){
      var lanes = null; try { lanes = window.__tiArpLanes && window.__tiArpLanes[0] ? window.__tiArpLanes[0].get() : null; } catch (e) {}
      var chopT = null; try { chopT = window.__tiDice && window.__tiDice.chop ? window.__tiDice.chop.S.v.time : null; } catch (e) {}
      R.rolls.push({ chain: chain, per: per, latches: latches, pitchFlat: lanes ? lanes.pitch.every(function(v){ return v === 3; }) : null, pitch: lanes ? lanes.pitch.join('') : null, chopTime: chopT, routes: routes.length }); publish(); }); }); }
  var p = wait(3000); for (var i = 0; i < 5; i++) p = p.then(roll);
  p.then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39fa started';
})();
