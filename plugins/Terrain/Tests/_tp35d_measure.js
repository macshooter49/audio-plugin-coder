/* tp35d — the second pass: the first bank-preset load's 15 s, a flow card's FIRST open (build),
   and the wavetable arrows after a dice roll (select vs param vs display). Same channel as tp35. */
(function(){
  var R = { phase: 'start', errs: [], B: [], C: {}, D: {} };
  window.__tp35d = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R);
    var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp35d:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 3000);
  window.addEventListener('error', function(e){ if (R.errs.length < 8) R.errs.push(String(e.message).slice(0, 90) + ' @' + (e.filename || '?').split('/').pop() + ':' + e.lineno); });
  var natOn = false, nat = {}, natT0 = 0, natTl = {};
  try { var be = window.__JUCE__.backend, oEmit = be.emitEvent;
    be.emitEvent = function(ev, payload){ if (natOn && ev === '__juce__invoke' && payload && payload.name) { var n = payload.name; nat[n] = (nat[n] || 0) + 1;
        var t = +(performance.now() - natT0).toFixed(0); if (!natTl[n]) natTl[n] = [t, t]; else natTl[n][1] = t; }
      return oEmit.apply(this, arguments); }; } catch (e) {}
  function natStart(){ natOn = true; nat = {}; natTl = {}; natT0 = performance.now(); } function natStop(){ natOn = false; return { n: nat, tl: natTl }; }
  var mk = 0; function mark(t){ try { document.title = 'tp35d:' + (9000 + (mk++)) + ':0/1:{"phase":"MARK ' + t + '"}'; } catch (e) {}
    try { NF('savePreset')('tp35mark', ('00' + mk).slice(-3) + ' ' + Math.round(performance.now()) + ' ' + String(t).replace(/[^A-Za-z0-9 ._-]/g, '_').slice(0, 40), '{"t":' + Math.round(performance.now()) + '}'); } catch (e) {} }
  document.addEventListener('visibilitychange', function(){ mark('vis ' + document.visibilityState); });
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function raf2(){ return new Promise(function(r){ requestAnimationFrame(function(){ requestAnimationFrame(function(){ r(); }); }); }); }
  /* two stall monitors: rAF gap (paint starvation) and a 10 ms timer's drift (JS-thread busy) */
  var gapMax = 0, gapLast = 0, gapOn = false, rafN = 0, drift = 0, dLast = 0;
  (function tick(){ requestAnimationFrame(function(t){ if (gapOn) { rafN++; if (gapLast && t - gapLast > gapMax) gapMax = t - gapLast; } gapLast = t; tick(); }); })();
  setInterval(function(){ var t = performance.now(); if (gapOn && dLast && t - dLast - 10 > drift) drift = t - dLast - 10; dLast = t; }, 10);
  function monStart(){ gapMax = 0; drift = 0; rafN = 0; gapOn = true; } function monStop(){ gapOn = false; return { gap: +gapMax.toFixed(0), drift: +drift.toFixed(0), rafN: rafN }; }

  function sectionB(){
    R.phase = 'B';
    return Promise.resolve(NF('listPresets')()).then(function(js){
      var cat = JSON.parse(js), all = [];
      cat.banks.forEach(function(b){ (b.presets || []).forEach(function(p){ if (!p.factory) all.push(p); }); });
      var byName = {}; all.forEach(function(p){ byName[p.name] = p; });
      var order = ['Magnolia', 'Frog Face', 'Leviathan', 'Frog Face', 'Magnolia'].map(function(n){ return byName[n]; }).filter(Boolean);
      var seq = Promise.resolve();
      order.forEach(function(p){ seq = seq.then(function(){ mark('load ' + p.name); natStart(); monStart(); var a = performance.now();
        return Promise.resolve(NF('loadPatchFile')(p.path)).then(function(){ var t1 = performance.now() - a;
          return raf2().then(function(){ var t2 = performance.now() - a; var m = monStop(); var nn = natStop();
            R.B.push({ name: p.name, kb: Math.round(p.bytes / 1024), nativeMs: +t1.toFixed(0), settledMs: +t2.toFixed(0), mon: m,
              tpOpen: !!(document.querySelector('#tp-page.open, #tp-page.on')), page: (document.body.className || '').slice(0, 60),
              natN: nn.n, natLate: Object.keys(nn.tl).filter(function(k){ return nn.tl[k][0] > 400; }).map(function(k){ return k + '@' + nn.tl[k][0] + '-' + nn.tl[k][1]; }).slice(0, 12) }); }); })
          .then(function(){ return wait(800); }); }); });
      return seq; });
  }
  function sectionC(){
    R.phase = 'C'; var C = R.C;
    function openMode(m){ mark('open ' + m); natStart(); monStart(); var a = performance.now(); var err = null;
      try { window.__openFlowCard(m); } catch (e) { err = String(e); }
      var sync = performance.now() - a; mark('opened ' + m + ' sync ' + sync.toFixed(0));
      return raf2().then(function(){ var cards = [].map.call(document.querySelectorAll('.ti-card.open'), function(c){ return c.className.slice(0, 40); });
        var r = { syncMs: +sync.toFixed(1), settledMs: +(performance.now() - a).toFixed(0), mon: monStop(), nat: natStop().n, open: cards, err: err, vis: document.visibilityState, parked: !!window.__tiParked }; mark('settled ' + m + ' ' + r.settledMs); return r; }); }
    return openMode('chop').then(function(r){ C.chopFirst = r; return wait(400); })
      .then(function(){ return openMode('glitch'); }).then(function(r){ C.glitchFirst = r; return wait(400); })
      .then(function(){ return openMode('glitch'); }).then(function(r){ C.glitchAgain = r;
        var card = null; document.querySelectorAll('.ti-card.open').forEach(function(c){ if (!card && /gli/.test(c.className) && c.querySelector('.pset')) card = c; });
        if (!card) document.querySelectorAll('.ti-card.open').forEach(function(c){ if (!card && c.querySelector('.pset')) card = c; });
        C.cardFound = card ? card.className.slice(0, 40) : null; if (!card) return;
        var pas = card.querySelectorAll('.pset .pa'), pn = card.querySelector('.pset .pn'); C.nameBefore = pn.textContent;
        mark('step'); natStart(); monStart(); var b = performance.now(); pas[1].click(); mark('stepped'); C.stepSyncMs = +(performance.now() - b).toFixed(1);
        return raf2().then(function(){ C.stepSettledMs = +(performance.now() - b).toFixed(0); C.stepMon = monStop(); C.stepNat = natStop().n; C.nameAfter = pn.textContent; return wait(300); })
          .then(function(){ mark('menu'); natStart(); monStart(); var c = performance.now(); pn.click(); mark('menu clicked');
            return new Promise(function(res){ var n = 0; (function poll(){ var m = document.querySelector('.pmenu'); if (m || n++ > 150) { C.menuMs = +(performance.now() - c).toFixed(0); C.menuRows = m ? m.querySelectorAll('.pi').length : -1; C.menuMon = monStop(); C.menuNat = natStop().n; res(); } else setTimeout(poll, 20); })(); }); })
          .then(function(){ try { document.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); } catch (e) {} });
      })
      .then(function(){ mark('popout gli'); var pf = NF('popOutCard'); var a = performance.now(); if (pf) { try { pf('gli', 60, 60, 330, 430); } catch (e) { C.popErr = String(e); } }
        C.popSyncMs = +(performance.now() - a).toFixed(1); return wait(7000).then(function(){ C.popped = !!(window.__poppedCards && window.__poppedCards.gli); mark('popout done'); }); })
      .then(function(){ if (!window.__openLfoCard) return; natStart(); monStart(); var a = performance.now(); try { window.__openLfoCard(); } catch (e) {}
        var s = performance.now() - a; return raf2().then(function(){ C.lfoExt = { syncMs: +s.toFixed(1), settledMs: +(performance.now() - a).toFixed(0), mon: monStop(), nat: natStart && natStop().n }; }); });
  }
  function sectionD(){
    R.phase = 'D'; var D = R.D; var WT = window.wtWaterfall;
    function snap(){ var sel = document.getElementById('osc-a-preset-select'), d = document.getElementById('osc-a-preset-display'); var pv = -1;
      try { pv = window.Juce.getSliderState('SYN_OSC_A_WT_PRESET').getNormalisedValue(); } catch (e) {}
      var pn = sel ? sel.options.length : 0; var idx = Math.round(pv * Math.max(1, pn - 1));
      return { selVal: sel ? sel.value : null, selTxt: sel && sel.selectedIndex >= 0 ? sel.options[sel.selectedIndex].textContent : null, disp: d ? d.textContent : null,
        paramIdx: idx, paramTxt: (sel && sel.options[idx]) ? sel.options[idx].textContent : null, imported: !!(WT && WT.imported && WT.imported.a), impName: WT && WT.importName ? WT.importName.a : '', folder: window.__wtFolder.a || null }; }
    D.rolls = [];
    var seq = Promise.resolve();
    for (var i = 0; i < 3; i++) seq = seq.then(function(){ mark('dice'); var e = {}; e.before = snap(); try { window.__tpDice(); } catch (x) { e.err = String(x); }
      return wait(2600).then(function(){ e.afterDice = snap(); window.wtStepPreset('a', 1); return wait(800); }).then(function(){ e.afterArrow = snap(); window.wtStepPreset('a', 1); return wait(800); }).then(function(){ e.afterArrow2 = snap(); D.rolls.push(e); }); });
    return seq;
  }
  wait(1500).then(sectionC).then(sectionD)
    .then(function(){ R.phase = 'done'; publish(); }, function(err){ R.errs.push('FAIL ' + String(err).slice(0, 120)); R.phase = 'failed'; publish(); });
  return 'tp35d started';
})();
