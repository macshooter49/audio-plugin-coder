/* tp40 — Max: "test every single thing on the synth page and anything in the patcher that isn't alike needs to be fixed".
   In the real plugin: on the SYNTH page, then on the PATCHER: wheel over the wavetable name / the wavetable picture / the WT Pos
   knob (which params move?), click every oscillator's + (back panel) and every effect's + — does the card flip? */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [], syn: {}, tp: {} }; window.__tp40 = R;
  window.addEventListener('error', function(e){ R.winErrs.push(String(e.message || e).slice(0, 120) + ' @' + (e.lineno || 0)); });
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp40:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function v(id){ var s = ss(id); return s ? Math.round(s.getNormalisedValue() * 1000) : null; }
  function wheelAt(el, dy){ if (!el) return false; var r = el.getBoundingClientRect(); if (!r.width || !r.height) return false;
    var ev = new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: dy, deltaX: 0, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 }); el.dispatchEvent(ev); return true; }
  function clickAt(el){ if (!el) return false; var r = el.getBoundingClientRect(); if (!r.width) return false; ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click'].forEach(function(t){ try { el.dispatchEvent(new (t.indexOf('pointer') === 0 ? PointerEvent : MouseEvent)(t, { bubbles: true, cancelable: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2, button: 0 })); } catch (e) {} }); return true; }
  function wheelTests(o){ var O = o.toUpperCase(), dev = document.getElementById('osc-' + o + '-device'), out = {}; if (!dev) return { noDevice: true };
    var targets = { name: document.getElementById('osc-' + o + '-preset-display'), picture: dev.querySelector('canvas'), wtpos: dev.querySelector('.knob[data-syn="SYN_OSC_' + O + '_WT_FRAME"]') };
    Object.keys(targets).forEach(function(k){ var before = { p: v('SYN_OSC_' + O + '_WT_PRESET'), f: v('SYN_OSC_' + O + '_WT_FRAME') }; var ok = wheelAt(targets[k], 120);
      var after = { p: v('SYN_OSC_' + O + '_WT_PRESET'), f: v('SYN_OSC_' + O + '_WT_FRAME') }; out[k] = ok ? ((after.p !== before.p ? 'preset' : '') + (after.f !== before.f ? 'frame' : '') || 'nothing') : 'no target'; });
    return out; }
  function swapTests(){ var out = {}; 'abcdefgh'.split('').forEach(function(o){ var dev = document.getElementById('osc-' + o + '-device'), btn = document.getElementById('osc-' + o + '-swap-btn'); if (!dev || !dev.getBoundingClientRect().width) { out[o] = 'hidden'; return; }
      var was = dev.classList.contains('swapped'); if (!clickAt(btn)) { out[o] = 'no button'; return; } var now = dev.classList.contains('swapped'); out[o] = (now !== was) ? 'flips' : 'DEAD'; if (now !== was) clickAt(btn); });
    var fxs = document.querySelectorAll('.fxr-dev'); out.fx = []; fxs.forEach(function(d){ if (!d.getBoundingClientRect().width) return; var b = d.querySelector('[data-act="swap"]'); var was = d.classList.contains('swapped'); if (!clickAt(b)) { out.fx.push((d.dataset.dev || '?') + ':no+'); return; } var now = d.classList.contains('swapped'); out.fx.push((d.dataset.dev || '?') + ':' + (now !== was ? 'flips' : 'DEAD')); if (now !== was) clickAt(b); });
    var f = document.getElementById('filter-device'), fb = document.getElementById('filter-swap-btn'); if (f && f.getBoundingClientRect().width) { var w0 = f.classList.contains('swapped'); clickAt(fb); out.filter = f.classList.contains('swapped') !== w0 ? 'flips' : 'DEAD'; if (f.classList.contains('swapped') !== w0) clickAt(fb); }
    return out; }
  function worldT(){ var w = document.querySelector('#tp-page .tp-world'); return w ? (w.style.transform || getComputedStyle(w).transform) : null; }
  function wheelTests2(){ var out = {}; var knob = document.querySelector('#tp-page .tp-node .tp-body .knob'); var t0 = worldT(); if (knob) { wheelAt(knob, 120); out.knobPans = worldT() !== t0; }
    var page = document.getElementById('tp-page'); var r = page.getBoundingClientRect(); var t1 = worldT(); page.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: 120, clientX: r.left + 8, clientY: r.bottom - 8 })); out.blankPans = worldT() !== t1; return out; }
  function fxTests(){ var out = []; document.querySelectorAll('#tp-page .fxr-dev').forEach(function(d){ var i = d.dataset.dev; var b = d.querySelector('[data-act="swap"]'); var was = d.classList.contains('swapped'); clickAt(b); var flips = d.classList.contains('swapped') !== was; if (flips) clickAt(b);
      var sel = d.querySelector('select.fxr-type-native'); var typed = null; if (sel && sel.options.length > 1) { var before = (window.__fxrDevs()[+i] || {}).type; sel.selectedIndex = (sel.selectedIndex + 1) % sel.options.length; sel.dispatchEvent(new Event('change', { bubbles: true })); typed = (window.__fxrDevs()[+i] || {}).type !== before; }
      var pw = d.querySelector('.fxr-pwr'); var pwr = null; if (pw) { var on0 = (window.__fxrDevs()[+i] || {}).on; clickAt(pw); pwr = (window.__fxrDevs()[+i] || {}).on !== on0; if (pwr) clickAt(pw); }
      out.push({ dev: i, core: (window.__fxrDevs()[+i] || {}).core, swap: flips ? 'flips' : 'DEAD', type: typed, power: pwr }); }); return out; }
  wait(3000).then(function(){ try { setActivePanel('syn'); } catch (e) { R.errs.push('syn: ' + e); } try { if (!(window.__fxrDevs() || []).length) window.__fxrAdd('reverb'); } catch (e) { R.errs.push('add: ' + e); } return wait(800); })
    .then(function(){ R.syn.wheelA = wheelTests('a'); R.syn.swap = swapTests(); publish(); try { setActivePanel('tp'); } catch (e) { R.errs.push('tp: ' + e); } return wait(1500); })
    .then(function(){ 'efgh'.split('').forEach(function(o){ try { ss('SYN_OSC_' + o.toUpperCase() + '_ENABLE').setNormalisedValue(1); } catch (e) {} }); return wait(2500); })
    .then(function(){ R.tp.nodes = (window.__tpLayout && window.__tpLayout().nodes) || null; R.tp.wheelA = wheelTests('a'); R.tp.wheelE = wheelTests('e'); R.tp.swap = swapTests(); R.tp.fx = fxTests(); R.tp.wheel2 = wheelTests2(); R.phase = 'done'; publish(); })
    .catch(function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp40 started';
})();
