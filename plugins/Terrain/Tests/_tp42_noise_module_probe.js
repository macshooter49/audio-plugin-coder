/* tp42 — NOISE AS A MODULE in the real plugin (the WebView inside the installed AU, /tmp/tpexp Tests/_tp42_noise_module_probe.js).
   On the Patcher: spawn Noise 2 from the canvas, read SYN_NOISE2_ON and the clone strip's home; cable it into a reverb and
   read SYN_RVB_SRC_N2 + the tap bit; then its own power pill (a real click) switches it off and the node leaves. */
(function(){
  var R = { phase: 'start', errs: [], winErrs: [] }; window.__tp42p = R;
  window.addEventListener('error', function(e){ R.winErrs.push(String(e.message || e).slice(0, 120) + ' @' + (e.lineno || 0)); });
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify(R); var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp42p:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 1500);
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function P(ids){ var g = NF('getSynParam'); return Promise.all(ids.map(function(id){ return Promise.resolve(g(id)).then(function(v){ return [id, Math.round((+v) * 1e6) / 1e6]; }); })); }
  var oi = function(n){ var outs = n.ports.filter(function(q){ return q.kind === 'out'; }); var i = outs.findIndex(function(p){ return p.el && p.el.dataset.t === 'a'; }); return i < 0 ? 0 : i; };
  wait(3000).then(function(){ setActivePanel('syn'); try { if (!(window.__fxrDevs() || []).some(function(d){ return d.core === 'reverb'; })) window.__fxrAdd('reverb'); } catch (e) { R.errs.push('add: ' + e); } return wait(600); })
    .then(function(){ setActivePanel('tp'); return wait(2500); })
    .then(function(){ R.catalog = (window.__tpCatalog ? window.__tpCatalog() : []).filter(function(x){ return x.cat === 'Oscillators'; }).map(function(x){ return x.n; });
      window.__tpAdd('noise:2', 420, 520); return wait(1500); })
    .then(function(){ return P(['SYN_NOISE2_ON', 'SYN_NOISE2_OUT', 'SYN_NOISE2_LEVEL']); })
    .then(function(rows){ R.spawn = rows; var n = window.__tpNodeByKey('noise2'); var el = document.getElementById('n2-noise-mod'); R.node = { have: !!n, inNode: !!(n && el && n.body.contains(el)), dests: el ? [].map.call(el.querySelectorAll('.noise-knobs .knob'), function(k){ return k.getAttribute('data-mod-dest'); }) : null, viz: !!(el && el.querySelector('.noise-viz canvas')) };
      R.cables0 = window.__tpDerive().map(function(c){ return c.id; }).filter(function(id){ return /noise2/.test(id); });
      var fx = window.__tpNodeByKey('fx-reverb-1'); if (!n || !fx) throw new Error('nodes missing'); R.okC = window.__tpConnect(n, 'out', oi(n), fx, 'in', 0); return wait(800); })
    .then(function(){ return P(['SYN_RVB_SRC_N2', 'SYN_RVB_TAPS']); })
    .then(function(rows){ R.afterCable = rows; R.route = (window.__fxrDevs()[0] || {}).route; R.cables1 = window.__tpDerive().map(function(c){ return c.id; }).filter(function(id){ return /noise2/.test(id); });
      return wait(2500); })
    .then(function(){ var pill = document.querySelector('#tp-page .fxr-dev[data-dev="0"] .fxr-r[data-r="10"]'); R.pill = pill ? { lit: pill.classList.contains('fxr-on'), text: pill.textContent.trim(), shown: !!pill.offsetParent, disp: getComputedStyle(pill).display, awake: document.body.classList.contains('tp-pool-awake'), pillE: (function(){ var e = document.querySelector('#tp-page .fxr-dev[data-dev="0"] .fxr-r[data-r="6"]'); return e ? getComputedStyle(e).display : null; })() } : null;
      var pw = document.getElementById('n2-noise-pow'); if (!pw) throw new Error('no power pill'); var r = pw.getBoundingClientRect(); R.pwRect = [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)];
      pw.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, button: 0, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 })); return wait(1800); })
    .then(function(){ return P(['SYN_NOISE2_ON']); })
    .then(function(rows){ R.afterPower = rows; R.nodeAfter = !!window.__tpNodeByKey('noise2'); R.home = !!document.querySelector('#noise2-home #n2-noise-mod'); R.phase = 'done'; publish(); },
          function(e){ R.errs.push(String(e).slice(0, 160)); R.phase = 'failed'; publish(); });
  return 'tp42p started';
})();
