/* tp39e — in the real plugin: over crazy pad rolls the reverb/delay Mod Rate + Mod Depth sit at their defaults and no route
   lands on them (Max: "stop randomizing the delay and reverb mod rate and mod depth — wobbly and bad"). */
(function(){
  var R = { phase: 'start', errs: [], rolls: [] }; window.__tp39e = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39e:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  var IDS = ['SYN_RVB_MODDEPTH', 'SYN_RVB_MODRATE', 'SYN_DLY_MODRATE', 'SYN_DLY_MODDEPTH', 'SYN_RVB_MODMODE'];
  function readAll(){ var g = NF('getSynParam'); return Promise.all(IDS.map(function(id){ return Promise.resolve(g ? g(id) : null).then(function(v){ return [id, v == null ? null : Math.round(+v * 1000) / 1000]; }); })).then(function(rows){ var o = {}; rows.forEach(function(r){ o[r[0]] = r[1]; }); return o; }); }
  function roll(){ return new Promise(function(res){ document.getElementById('dice-btn').click(); setTimeout(res, 3200); }).then(readAll).then(function(vals){
    var devs = window.__fxrDevs ? window.__fxrDevs() : [], routes = window.__tiRoutes ? window.__tiRoutes() : [], hits = 0, mods = [];
    devs.forEach(function(d){ if (d.core !== 'reverb' && d.core !== 'delay') return; mods.push(d.core + ':' + d.type);
      (d.back.knobs || []).forEach(function(kn, i){ if (!/^(Mod Rate|Mod Depth)$/i.test(kn[0]) && !/_(MODRATE|MODDEPTH)$/.test(kn[2])) return; var dest = window.__fxModDest(d.core, d.inst, 4 + i); routes.forEach(function(r){ if (r.d === dest) hits++; }); }); });
    R.rolls.push({ aim: window.__tpDiceLastAim, devs: mods.join(','), vals: vals, routeHits: hits, routes: routes.length }); publish(); }); }
  wait(3000).then(function(){ window.__tpDiceAims(['pads']); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); window.__tpDiceCap('off'); return readAll(); }).then(function(v0){ R.before = v0; })
    .then(roll).then(roll).then(roll).then(roll).then(roll).then(roll)
    .then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39e started';
})();
