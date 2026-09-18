/* tp39f — save a dozen crazy rolls (keys / plucks / pads / bass) so the AU harness can pitch-track every oscillator:
   TP_BANK=/tmp/tp39f /tmp/aucensus4 pitch   (Max: "some one shots stay locked to one note") */
(function(){
  var R = { phase: 'start', errs: [], saved: [] }; window.__tp39f = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39f:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  var AIMS = ['keys', 'plucks', 'pads', 'bass', 'keys', 'plucks', 'leads', 'textures', 'keys', 'plucks', 'pads', 'plucks'];
  function one(i){ return function(){ window.__tpDiceAims([AIMS[i]]); window.__tpDiceBlocks({}); window.__tpDiceLevelSet('crazy'); window.__tpDiceCap('off'); document.getElementById('dice-btn').click();
    return wait(4500).then(function(){ var engs = 'abcdefgh'.split('').map(function(o){ var s = window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENGINE'), e = window.Juce.getSliderState('SYN_OSC_' + o.toUpperCase() + '_ENABLE'); return e.getNormalisedValue() > .5 ? o + Math.round(s.getNormalisedValue() * 6) : null; }).filter(Boolean).join(',');
      var f = NF('savePatchFile'); var nm = 'r' + (i < 10 ? '0' + i : i) + '-' + AIMS[i]; return Promise.resolve(f('/tmp/tp39f/' + nm + '.terrain', JSON.stringify({ name: nm, bank: 'tp39f', author: 'dice' }))).then(function(){ R.saved.push(nm + ' [' + engs + ']'); publish(); }); }); }; }
  var p = wait(3000); for (var i = 0; i < AIMS.length; i++) p = p.then(one(i));
  p.then(function(){ R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39f started';
})();
