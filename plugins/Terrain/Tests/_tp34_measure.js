/* tp34 — RUNS INSIDE THE REAL WEBVIEW (via the fb504 experiment hook), with the real C++ push
   lane driving the painters and real audio sounding. Builds a busy Patcher, then measures what
   the frame actually costs — per zoom level — and what a dice roll costs. Results land in
   window.__tp34; the harness reads them back on the next open (the page is kept alive). */
(function(){
  var R = { phase: 'start', t0: performance.now(), runs: [], gens: [], errs: [] };
  window.__tp34 = R;
  /* the read-back channel: WKWebView exposes document.title to the host via KVC, so the harness
     can read results without a second open (which a windowed view cannot do — fb521) */
  /* WebKit clamps document.title to 1000 chars, so the result rotates through it in
     generation-tagged chunks: 'tp34:<gen>:<i>/<n>:<chunk>'; the harness reassembles a generation */
  var chunks = [], ci = 0, gen = 0, pubT = null;
  function publish(){ try { var str = JSON.stringify({ phase: R.phase, built: R.built, adds: R.adds, runs: R.runs, gens: R.gens,
      errs: R.errs.slice(0, 4).map(function(x){ return String(x).replace(/["\\]/g, "'").slice(0, 120); }), errCounts: R.errCounts, stacks: R.stacks, wtDispMoves: R.wtDispMoves, wtDispSample: R.wtDispSample });
    var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++;
    if (R.phase === 'done' || R.phase === 'failed') { clearInterval(pubT); pubT = null; } } catch (e) {} }
  setInterval(function(){ if (!chunks.length) return; document.title = 'tp34:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 250);
  pubT = setInterval(publish, 4000);
  var errSeen = {};
  window.addEventListener('error', function(e){ var k = String(e.message).slice(0, 60); errSeen[k] = (errSeen[k] || 0) + 1;
    if (errSeen[k] === 1 && R.errs.length < 6) R.errs.push(String(e.message).slice(0, 90) + ' @' + (e.filename || '?').split('/').pop() + ':' + e.lineno + ':' + e.colno); });
  R.errCounts = errSeen;
  var oRAF = window.requestAnimationFrame.bind(window), cur = null;
  /* the native-call census: every getNativeFunction() call leaves the page through
     window.__JUCE__.backend.emitEvent('__juce__invoke', {name,...}) — count them by name */
  var nat = {}; try { var be = window.__JUCE__ && window.__JUCE__.backend, oEmit = be && be.emitEvent;
    if (oEmit) be.emitEvent = function(ev, payload){ if (ev === '__juce__invoke' && payload && payload.name) { nat[payload.name] = (nat[payload.name] || 0) + 1;
        if ((payload.name === 'getOscSamplePayload' || payload.name === 'getOscWavetable') && !R.stacks[payload.name]) R.stacks[payload.name] = String(new Error().stack || '').split('\n').slice(1, 7).map(function(l){ return l.replace(/.*index\.html[^:]*:/, '').split(':')[0]; }).join('>'); }
      return oEmit.apply(this, arguments); }; } catch (e) {}
  R.stacks = {};
  /* every rAF callback is timed — the frame dispatcher's painter pass is one of them, and by far
     the biggest, so per-frame callback time IS the painter pass */
  window.requestAnimationFrame = function(cb){ return oRAF(function(ts){ var a = performance.now();
    try { cb(ts); } finally { var d = performance.now() - a; if (cur) { cur.cb++; cur.cbMs += d; if (d > cur.cbMax) cur.cbMax = d; } } }); };
  function pct(a, p){ if (!a.length) return 0; var s = a.slice().sort(function(x, y){ return x - y; }); return s[Math.min(s.length - 1, Math.floor(s.length * p))]; }
  function canvasStats(){ var px = 0, k = 0, on = 0;
    document.querySelectorAll('#tp-page canvas').forEach(function(c){ if (c.width && c.height) { px += c.width * c.height; k++; } });
    return { canvases: k, mpx: +(px / 1e6).toFixed(2), nodes: (window.__tpNodes ? window.__tpNodes().length : 0) }; }
  function measure(label, ms){ return new Promise(function(res){
    var m = { label: label, gaps: [], cb: 0, cbMs: 0, cbMax: 0, late: [] }, last = 0, done = false, t0 = performance.now();
    nat = {};
    cur = m;
    (function tick(ts){ if (last) m.gaps.push(ts - last); last = ts; if (performance.now() - t0 < ms) oRAF(tick); else done = true; })(0);
    var mc = new MessageChannel(), sent = 0;
    mc.port1.onmessage = function(){ m.late.push(performance.now() - sent);
      /* wall-clock bound: if rAF never comes (a throttled view), finish anyway and say so */
      if (!done && performance.now() - t0 > ms + 2500) { done = true; m.starved = true; }
      if (!done) { sent = performance.now(); mc.port2.postMessage(0); } else finish(); };
    sent = performance.now(); mc.port2.postMessage(0);
    function finish(){ cur = null; var frames = m.gaps.length; var st = canvasStats();
      R.runs.push({ label: label, frames: frames, fps: +(1000 / (pct(m.gaps, 0.5) || 16.7)).toFixed(1),
        gapMed: +pct(m.gaps, 0.5).toFixed(1), gapP90: +pct(m.gaps, 0.9).toFixed(1), gapP99: +pct(m.gaps, 0.99).toFixed(1), gapMax: +pct(m.gaps, 1).toFixed(1),
        cbPerFrame: +(m.cbMs / Math.max(1, m.cb)).toFixed(2), cbMax: +m.cbMax.toFixed(1),
        busyP90: +pct(m.late, 0.9).toFixed(1), busyMax: +pct(m.late, 1).toFixed(1), zoom: window.__tpView ? +window.__tpView().z.toFixed(2) : null,
        nodes: st.nodes, canvases: st.canvases, mpx: st.mpx, starved: !!m.starved,
        natives: Object.keys(nat).sort(function(a, b){ return nat[b] - nat[a]; }).slice(0, 6).map(function(k){ return k + ':' + Math.round(nat[k] * 1000 / ms) + '/s'; }) });
      res(); } }); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function S(id, v){ try { window.__setSynParam(id, v); } catch (e) {} }
  (async function(){
    try {
      R.phase = 'open';
      window.setActivePanel('tp'); await wait(1500);
      R.phase = 'build';
      ['a','b','c','d'].forEach(function(o){ var d = document.getElementById('osc-' + o + '-device'); if (d) d.classList.remove('osc-off');
        S('SYN_OSC_' + o.toUpperCase() + '_ENABLE', 1); });
      R.adds = [];
      ['reverb','delay','saturate','granular','cho','fla','pha','eqz','bod'].forEach(function(k){ var a = performance.now(); try { window.__fxrAdd(k); } catch (e) {} R.adds.push(k + ':' + (performance.now() - a).toFixed(0)); });
      try { var a2 = performance.now(); window.__flowSetChain(['glitch','chop']); R.adds.push('flow:' + (performance.now() - a2).toFixed(0)); } catch (e) {}
      try { var t = document.getElementById('tape-toggle'); if (t && t.classList.contains('off')) t.click(); } catch (e) {}
      await wait(3000);
      try { window.__tpSync(); } catch (e) {} await wait(800);
      R.built = canvasStats();
      R.phase = 'measure';
      /* which __wtDisp fields move at rest? (each move re-bakes a whole wavetable through a native) */
      try { var snaps = []; for (var q = 0; q < 20; q++) { snaps.push(JSON.stringify(window.__wtDisp || null)); await wait(100); }
        var moved = {}; for (var q = 1; q < snaps.length; q++) { var A = JSON.parse(snaps[q - 1]) || [], B = JSON.parse(snaps[q]) || [];
          for (var o = 0; o < A.length; o++) for (var f = 0; f < (A[o] || []).length; f++) if (A[o][f] !== (B[o] || [])[f]) moved['osc' + o + '[' + f + ']'] = (moved['osc' + o + '[' + f + ']'] || 0) + 1; }
        R.wtDispMoves = moved; R.wtDispSample = snaps[0].slice(0, 200); } catch (e) { R.wtDispMoves = 'err ' + e.message; }
      window.__tpFit(); await wait(900);
      await measure('fit: everything on screen', 5000);
      var v = window.__tpView(); window.__tpSetView(v.x, v.y, 1.0); await wait(900);
      await measure('zoom 1.0: a few on screen', 4000);
      window.__tpSetView(v.x, v.y, 0.35); await wait(900);
      await measure('zoom 0.35: all, tiny', 4000);
      /* A/B — zoomed out, every canvas still paints at >=1x backing (__tpZoomUp = max(1, zoom)):
         at 0.35 that is ~8x the pixels actually displayed. Let the bitmap match the display
         exactly (not stretching - the crispest possible), rebake via a settle, re-measure. */
      var oZU = window.__tpZoomUp;
      window.__tpZoomUp = function(el){ return Math.max(0.3, window.__tpZoomOf ? window.__tpZoomOf(el) : 1); };
      window.__tpSetView(v.x, v.y, 0.36); await wait(400); window.__tpSetView(v.x, v.y, 0.35); await wait(1200);
      await measure('zoom 0.35: EXACT-size bitmaps', 4000);
      window.__tpZoomUp = oZU;
      window.__tpSetView(v.x, v.y, 0.36); await wait(400); window.__tpSetView(v.x, v.y, 0.35); await wait(900);
      window.__tpFit(); await wait(900);
      R.phase = 'generate';
      for (var g = 0; g < 3; g++) {
        var a = performance.now(); try { window.__tpGenerate(); } catch (e) { R.errs.push('gen: ' + e.message); }
        var syncMs = performance.now() - a;
        await measure('right after a dice roll #' + (g + 1), 1500);
        R.gens.push({ syncMs: +syncMs.toFixed(1) });
        await wait(1500);
      }
      R.phase = 'done'; publish();
    } catch (e) { R.errs.push('run: ' + (e && e.message)); R.phase = 'failed'; publish(); }
  })();
  return 'started';
})();
