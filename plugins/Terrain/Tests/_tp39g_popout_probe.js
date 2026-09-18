/* tp39g — roll until an arp is in the chain, then POP the arp card out (the separate-window path FL users take), keep playing. */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [], popped: null }; window.__tp39gp = R;
  window.addEventListener('error', function(e){ R.winErrs.push(String(e.message || e).slice(0, 140) + ' @' + (e.lineno || 0)); });
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39gp:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function roll(){ return new Promise(function(res){ window.__tpDiceAims(['plucks']); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); document.getElementById('dice-btn').click(); setTimeout(res, 3500); }); }
  function untilArp(n){ return roll().then(function(){ var ch = window.__flowChain(); R.chain = ch; if (ch.some(function(m){ return /^arp/.test(m); }) || n <= 0) return; return untilArp(n - 1); }); }
  wait(3000).then(function(){ return untilArp(6); })
    .then(function(){ window.__openFlowCard('arp'); return wait(1500); })
    .then(function(){ var f = window.Juce.getNativeFunction('popOutCard'); return Promise.resolve(f('arp', 0, 0, 0, 0)); })
    .then(function(r){ R.popped = String(r).slice(0, 40); return wait(6000); })
    .then(function(){ R.poppedCards = window.__poppedCards ? Object.keys(window.__poppedCards) : null; return wait(12000); })
    .then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39gp started';
})();
