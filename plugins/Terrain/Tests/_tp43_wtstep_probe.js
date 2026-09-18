/* tp43 — THE WAVETABLE ARROWS ON THE PATCHER, real plugin (tpexp). Max: "click on the arrow of my wavetable and it directs me
   to the exact wavetable in that library". On the canvas, press › three times on osc A and on osc E; read the preset name each
   time. Before: the canvas's drag capture ate the press (the arrow was not a CONTROL). */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [] }; window.__tp43w = R;
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++; if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp43w:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function ss(id){ try { return window.Juce.getSliderState(id); } catch (e) { return null; } }
  function navOf(o){ var pd = document.getElementById('osc-' + o + '-preset-display'); var pw = pd ? pd.parentElement : null; return pw ? [].slice.call(pw.parentNode.querySelectorAll('.wt-nav')) : []; }
  function nameOf(o){ var pd = document.getElementById('osc-' + o + '-preset-display'); return pd ? pd.textContent.trim() : null; }
  function press(el){ var r = el.getBoundingClientRect(); el.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 })); el.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, button: 0, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 })); }
  function stepThrice(o){ var names = [nameOf(o)]; var nav = navOf(o); if (nav.length < 2) return Promise.resolve({ arrows: nav.length, names: names });
    return wait(0).then(function(){ press(nav[1]); return wait(1200); }).then(function(){ names.push(nameOf(o)); press(nav[1]); return wait(1200); }).then(function(){ names.push(nameOf(o)); press(nav[1]); return wait(1200); }).then(function(){ names.push(nameOf(o)); var dragged = !!(window.__tpDrag && window.__tpDrag()); return { arrows: nav.length, names: names, dragging: dragged }; }); }
  wait(2500).then(function(){ setActivePanel('tp'); var e = ss('SYN_OSC_E_ENABLE'); if (e) e.setNormalisedValue(1); return wait(2500); })
    .then(function(){ return stepThrice('a'); }).then(function(a){ R.a = a; return stepThrice('e'); }).then(function(e){ R.e = e; R.phase = 'done'; publish(); },
          function(err){ R.errs.push(String(err).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp43w started';
})();
