/* tp39f — builds Tests/fixtures/modal_striker.terrain: osc A = Modal Pluck, osc B = Modal Brass, each with a factory one-shot
   striker (Keys / Bell), nothing else. The pitch gate renders it: the striker must follow the key.
   /tmp/tpexp Tests/_tp39f_fixture.js tp39fx 25 */
(function(){
  var R = { phase: 'start', errs: [] }; window.__tp39fx = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp39fx:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function set(id, v){ var s = ss(id); if (s) s.setNormalisedValue(v); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  var OUT = (window.__tp39fxOut || '/tmp/modal_striker.terrain');
  wait(2500).then(function(){ return Promise.resolve(NF('scanSampleFactory')()); }).then(function(j){ var d = JSON.parse(j); R.lib = d.path;
    var keys = d.path + '/Keys/' + d.cats.Keys[0], bell = d.path + '/Bell/' + d.cats.Bell[0]; R.files = [keys, bell];
    'abcdefgh'.split('').forEach(function(o){ set('SYN_OSC_' + o.toUpperCase() + '_ENABLE', 0); });
    set('SYN_OSC_A_ENABLE', 1); set('SYN_OSC_A_ENGINE', 1); set('SYN_OSC_A_MODAL_FAMILY', 1 / 8); set('SYN_OSC_A_MODAL_SOURCE', 0); set('SYN_OSC_A_MODAL_DECAY', .5);
    set('SYN_OSC_B_ENABLE', 1); set('SYN_OSC_B_ENGINE', 1); set('SYN_OSC_B_MODAL_FAMILY', 5 / 8); set('SYN_OSC_B_MODAL_SOURCE', 0); set('SYN_OSC_B_MODAL_DECAY', .5);
    window.__sampLoadPath('a', keys); window.__sampLoadPath('b', bell); return wait(3000); })
  .then(function(){ var g = NF('getOscSamplePayload'); return Promise.all(['a', 'b'].map(function(o){ return Promise.resolve(g(o)).then(function(p){ return String(p || '').length; }); })); })
  .then(function(lens){ R.payload = lens; if (!(lens[0] > 2 && lens[1] > 2)) throw new Error('striker did not land ' + JSON.stringify(lens));
    return Promise.resolve(NF('savePatchFile')(OUT, JSON.stringify({ name: 'modal_striker', bank: 'tp39f', author: 'fixture' }))); })
  .then(function(res){ R.saved = String(res).slice(0, 20); R.phase = 'done'; publish(); }, function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp39fx started';
})();
