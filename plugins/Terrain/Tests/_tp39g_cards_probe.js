/* tp39g — Max: "my flow cards are broken now, it just crashed my FL Studio". In the real plugin: three crazy rolls with a
   chord held, then open every flow card of the chain (arp / arp2 / chop / glitch / robin) through window.__openFlowCard,
   count window errors, check each card's shell is in the DOM and visible, keep playing 15 s. A crash kills the harness. */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [], steps: [] }; window.__tp39g = R;
  window.addEventListener('error', function(e){ R.winErrs.push(String(e.message || e).slice(0, 140) + ' @' + (e.lineno || 0)); });
  window.addEventListener('unhandledrejection', function(e){ R.winErrs.push('rej: ' + String(e.reason && e.reason.message || e.reason).slice(0, 140)); });
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39g:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function roll(){ return new Promise(function(res){ window.__tpDiceAims(['plucks']); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); document.getElementById('dice-btn').click(); setTimeout(res, 3500); }); }
  function openAll(){ var chain = window.__flowChain ? window.__flowChain() : []; var out = { chain: chain, cards: [] };
    chain.forEach(function(m){ var before = R.winErrs.length; try { window.__openFlowCard(m); } catch (e) { R.winErrs.push('open ' + m + ': ' + String(e).slice(0, 100)); }
      var sh = document.querySelector('.ti-shell.on, .ext-card.on, [class*="-ext"].on') || null; var vis = sh ? sh.getBoundingClientRect() : null;
      out.cards.push({ card: m, errs: R.winErrs.length - before, shell: sh ? sh.className.slice(0, 40) : null, w: vis ? Math.round(vis.width) : 0, h: vis ? Math.round(vis.height) : 0 }); });
    return out; }
  var p = wait(3000).then(roll).then(function(){ R.steps.push(openAll()); publish(); return wait(2000); })
    .then(roll).then(function(){ R.steps.push(openAll()); publish(); return wait(2000); })
    .then(roll).then(function(){ R.steps.push(openAll()); publish(); return wait(15000); })
    .then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39g started';
})();
