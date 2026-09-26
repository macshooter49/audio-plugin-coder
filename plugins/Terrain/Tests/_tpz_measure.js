/* tp110 — RUNS INSIDE THE REAL WEBVIEW (Tests/mac_patcher_glide.mm, via the fb504 hook).
   Loads a preset (or builds a stress patch), opens the Patcher, and drives the gestures a hand
   makes — a drag-pan, a trackpad (wheel) pan, a pinch zoom in and out — for several seconds each,
   with a chord sounding and the C++ push lane live. Per phase it reports the rAF cadence and
   WHO spends the frame:
     ev      the page's own gesture handlers (dispatch of the synthetic events)
     cb      every other rAF callback (the painters, the frame dispatcher)
     frame   window.__tiFrame (the push lane's per-frame entry)
     render  style + layout + paint on the main thread after the last rAF callback
     rd      layout reads (getBoundingClientRect / getComputedStyle / offset*) — count and time
     cvs     canvas bitmap re-buffers (a width/height that actually changed) — count
     d       SVG path 'd' writes (cable recompute) — count
   Phases are announced to the host through document.title = 'tpzP:<phase>' (it samples the
   WebContent / GPU / host CPU at each change); results go back as 'tp34:' chunks.
   Args (window.__tpzArgs): {preset:"<abs path .terrain>"} or {stress:<nodes>}, optional {z:1}. */
(function(){
  var A = window.__tpzArgs || {};
  var R = { phase: 'start', args: A, runs: [], errs: [], info: {} };
  window.__tpz = R;
  var chunks = [], ci = 0, gen = 0, announcing = true;
  function publish(){ try { var str = JSON.stringify({ phase: R.phase, info: R.info, runs: R.runs,
      errs: R.errs.slice(0, 6).map(function(x){ return String(x).replace(/["\\]/g, "'").slice(0, 140); }), errCounts: R.errCounts });
    var c = []; for (var k = 0; k < str.length; k += 850) c.push(str.slice(k, k + 850)); chunks = c; ci = 0; gen++; } catch (e) {} }
  setInterval(function(){ if (announcing || !chunks.length) return; document.title = 'tp34:' + gen + ':' + ci + '/' + chunks.length + ':' + chunks[ci]; ci = (ci + 1) % chunks.length; }, 120);
  function announce(p){ R.phase = p; document.title = 'tpzP:' + p; }
  var errSeen = {}; R.errCounts = errSeen;
  window.addEventListener('error', function(e){ var k = String(e.message).slice(0, 60); errSeen[k] = (errSeen[k] || 0) + 1;
    if (errSeen[k] === 1 && R.errs.length < 6) R.errs.push(String(e.message).slice(0, 90) + ' @' + (e.filename || '?').split('/').pop() + ':' + e.lineno); });

  /* ── instrumentation ─────────────────────────────────────────────────────────────────────── */
  var cur = null, oRAF = window.requestAnimationFrame.bind(window), now = performance.now.bind(performance);
  var lastEnd = 0, rpending = false, rch = new MessageChannel();
  rch.port1.onmessage = function(){ rpending = false; if (cur && lastEnd) { var d = now() - lastEnd; cur.render.push(d); } };
  /* per-callback attribution: the first 70 chars of the callback's source name it (most painters are anonymous) */
  var who = {}; function keyOf(cb){ var k = cb.__tpzK; if (k) return k; try { k = (cb.name ? cb.name + ':' : '') + String(cb).slice(0, 70).replace(/\s+/g, ' '); } catch (e) { k = '?'; } try { cb.__tpzK = k; } catch (e) {} return k; }
  window.requestAnimationFrame = function(cb){ return oRAF(function(ts){ var a = now();
    try { cb(ts); } finally { var d = now() - a; if (cur) { cur.cbMs += d; cur.cbN++; var k = keyOf(cb); cur.who[k] = (cur.who[k] || 0) + d; } lastEnd = now(); if (!rpending) { rpending = true; rch.port2.postMessage(0); } } }); };
  function wrapTime(obj, name, key){ try { var o = obj[name]; if (typeof o !== 'function' || o.__tpz) return;
      var f = function(){ var a = now(); try { return o.apply(this, arguments); } finally { if (cur) { cur[key + 'Ms'] += now() - a; cur[key + 'N']++; } } }; f.__tpz = 1; obj[name] = f; } catch (e) {} }
  if (!A.lite) {
  wrapTime(Element.prototype, 'getBoundingClientRect', 'rd');
  wrapTime(window, 'getComputedStyle', 'rd');
  wrapTime(Range.prototype, 'getBoundingClientRect', 'rd');
  ['offsetWidth', 'offsetHeight', 'clientWidth', 'clientHeight', 'scrollHeight'].forEach(function(p){ try {
    var proto = (p.indexOf('offset') === 0) ? HTMLElement.prototype : Element.prototype, d = Object.getOwnPropertyDescriptor(proto, p);
    if (!d || !d.get) return; var g = d.get;
    Object.defineProperty(proto, p, { configurable: true, enumerable: d.enumerable, get: function(){ var a = now(); try { return g.call(this); } finally { if (cur) { cur.rdMs += now() - a; cur.rdN++; } } } }); } catch (e) {} });
  ['width', 'height'].forEach(function(p){ try { var d = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, p), g = d.get, s = d.set;
    Object.defineProperty(HTMLCanvasElement.prototype, p, { configurable: true, enumerable: d.enumerable, get: g,
      set: function(v){ if (cur && (+v | 0) !== g.call(this)) cur.cvs++; return s.call(this, v); } }); } catch (e) {} });
  try { var oSA = Element.prototype.setAttribute; Element.prototype.setAttribute = function(n, v){ if (cur && n === 'd') cur.d++; return oSA.call(this, n, v); }; } catch (e) {}
  }
  /* the push lane's entry — rewrapped if the page replaces it */
  /* a shipped frame starts with the __tickT heartbeat and ends with __tiFrame(): the gap is the whole eval'd push task */
  var tickAt = 0; try { var tv = window.__tickT; Object.defineProperty(window, '__tickT', { configurable: true, get: function(){ return tv; }, set: function(v){ tv = v; tickAt = now(); } }); } catch (e) {}
  function wrapFrame(){ try { var f = window.__tiFrame; if (typeof f === 'function' && !f.__tpz) { var g = function(){ var a = now(); if (cur && tickAt) { cur.evalMs += a - tickAt; tickAt = 0; } try { return f.apply(this, arguments); } finally { if (cur) { cur.frameMs += now() - a; cur.frameN++; } } }; g.__tpz = 1; window.__tiFrame = g; } } catch (e) {} }
  setInterval(wrapFrame, 500); wrapFrame();
  /* the push statements (the C++ frame calls these globals by name, outside rAF): time them as 'push:<name>' */
  var PUSH = ['updateOscScope','__mvChaos','__modViz','__lfoLiveApply','__crvXApply','__crvLiveTick','updateVisualization','updateTapeLoopState',
    'updateSynthLFO','updateSampleFollower','updateLFOOutputs','updateHarmViz','updateFeedState','updateEnvFollower','updateCaptureState','onWtFrames',
    '__tiModRestore','__terrainReso','__terrainEqAnalyzer','__mvP2Tick','__geodeSpectrum','__geodeImage','__flowFeedPush'];
  function wrapPush(){ PUSH.forEach(function(nm){ try { var f = window[nm]; if (typeof f !== 'function' || f.__tpz) return;
    var g = function(){ var a = now(); try { return f.apply(this, arguments); } finally { if (cur) { var d = now() - a; cur.pushMs += d; cur.who['push:' + nm] = (cur.who['push:' + nm] || 0) + d; } } }; g.__tpz = 1; window[nm] = g; } catch (e) {} }); }
  setInterval(wrapPush, 1000); wrapPush();
  /* per-PAINTER attribution: the dispatcher runs reg.forEach(function(fn){ try { fn(ts); } ... }) over a Map
     keyed by painter name — recognise that call and time each entry under its registered name */
  var PF = {};   /* the registry's painters by name, seen through the dispatcher's forEach (the exp 'painters' re-registers them) */
  (function(){ var oFE = Map.prototype.forEach;
    Map.prototype.forEach = function(cb, th){ if (cur && typeof cb === 'function' && /try \{ fn\(ts\); \}/.test(cb.__tpzS || (cb.__tpzS = String(cb).slice(0, 60)))) {
        var self = this; return oFE.call(this, function(v, k){ PF[k] = v; var a = now(); try { cb.call(th, v, k, self); } finally { var d = now() - a; cur.who['p:' + k] = (cur.who['p:' + k] || 0) + d; } }); }
      return oFE.call(this, cb, th); }; })();

  function pct(a, p){ if (!a.length) return 0; var s = a.slice().sort(function(x, y){ return x - y; }); return s[Math.min(s.length - 1, Math.floor(s.length * p))]; }
  function sum(a){ var s = 0; for (var i = 0; i < a.length; i++) s += a[i]; return s; }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function S(id, v){ try { window.__setSynParam(id, v); } catch (e) {} }
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  var page = function(){ return document.getElementById('tp-page'); };

  /* ── a phase: `drive(i, dt)` is called once per frame and dispatches the gesture's events ── */
  function measure(label, ms, drive){ return new Promise(function(res){
    var m = { gaps: [], cbMs: 0, cbN: 0, evMs: 0, frameMs: 0, frameN: 0, rdMs: 0, rdN: 0, cvs: 0, d: 0, render: [], late: [], who: {}, pushMs: 0, evalMs: 0 }, last = 0, t0 = now(), i = 0, done = false;
    announce(label); cur = m;
    (function tick(ts){ if (last) m.gaps.push(ts - last); last = ts;
      if (drive) { var a = now(); try { drive(i++, now() - t0); } catch (e) { R.errs.push('drive ' + label + ': ' + e.message); } m.evMs += now() - a; }
      if (now() - t0 < ms) oRAF(tick); else done = true; })(0);
    /* main-thread lateness: a 4 ms timer chain (cheap — a MessageChannel spin would itself fill the thread
       and pollute the WebContent CPU reading); lateness = how long the thread was held past the due time */
    var due = 0;
    function probe(){ var t = now(); if (due) m.late.push(Math.max(0, t - due));
      if (!done && t - t0 > ms + 3000) { done = true; m.starved = true; }
      if (!done) { due = now() + 4; setTimeout(probe, 4); } else finish(); }
    due = now() + 4; setTimeout(probe, 4);
    function finish(){ cur = null; var F = Math.max(1, m.gaps.length), med = pct(m.gaps, 0.5) || 16.7, drop = 0;
      m.gaps.forEach(function(g){ if (g > med * 1.5) drop += Math.round(g / med) - 1; });
      var v = window.__tpView ? window.__tpView() : {};
      R.runs.push({ l: label, n: m.gaps.length, fps: +(1000 * m.gaps.length / sum(m.gaps)).toFixed(1),
        med: +med.toFixed(1), p95: +pct(m.gaps, 0.95).toFixed(1), p99: +pct(m.gaps, 0.99).toFixed(1), max: +pct(m.gaps, 1).toFixed(1), drop: drop,
        ev: +(m.evMs / F).toFixed(2), eval: +(m.evalMs / F).toFixed(2), push: +(m.pushMs / F).toFixed(2), cb: +(m.cbMs / F).toFixed(2), frame: +(m.frameMs / F).toFixed(2), lane: +(m.frameN * 1000 / ms).toFixed(0),
        render: +(sum(m.render) / F).toFixed(2), renderP95: +pct(m.render, 0.95).toFixed(1),
        rd: +(m.rdMs / F).toFixed(2), rdN: +(m.rdN / F).toFixed(0), cvs: +(m.cvs / F).toFixed(2), d: +(m.d / F).toFixed(1),
        busyP95: +pct(m.late, 0.95).toFixed(1), busyMax: +pct(m.late, 1).toFixed(1), z: v.z != null ? +v.z.toFixed(2) : null, starved: !!m.starved,
        top: Object.keys(m.who).sort(function(a, b){ return m.who[b] - m.who[a]; }).slice(0, 12).map(function(k){ return (m.who[k] / F).toFixed(2) + 'ms ' + k.slice(0, 60).replace(/["\\]/g, "'"); }) });
      res(); } }); }

  /* ── gestures ───────────────────────────────────────────────────────────────────────────── */
  function pageRect(){ return page().getBoundingClientRect(); }
  function emptyPoint(){ var r = pageRect();
    for (var gy = 0.15; gy < 0.9; gy += 0.05) for (var gx = 0.1; gx < 0.9; gx += 0.05) {
      var x = r.left + r.width * gx, y = r.top + r.height * gy, el = document.elementFromPoint(x, y);
      if (el && page().contains(el) && !el.closest('.tp-node, .tp-tools, .tp-mini, .tp-menu, .tp-cables .hit')) return { x: x, y: y }; }
    return null; }
  /* a real drag is a pointer stream AND a mouse stream: pointerdown/move/up first, then the mouse event */
  function mouse(type, x, y, target){ var o = { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 0, buttons: type === 'mouseup' ? 0 : 1, view: window };
    var t = target || document.elementFromPoint(x, y) || window;
    try { t.dispatchEvent(new PointerEvent(type.replace('mouse', 'pointer'), Object.assign({ pointerId: 1, pointerType: 'mouse', isPrimary: true }, o))); } catch (e) {}
    t.dispatchEvent(new MouseEvent(type, o)); }
  function wheel(x, y, dx, dy, ctrl){ var t = document.elementFromPoint(x, y) || page();
    t.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, clientX: x, clientY: y, deltaX: dx, deltaY: dy, deltaMode: 0, ctrlKey: !!ctrl, view: window })); }
  function dragPan(label, ms){ var p = emptyPoint(); if (!p) { R.errs.push('no empty point to drag'); return measure(label, ms, null); }
    var down = false, t = document.elementFromPoint(p.x, p.y);
    return measure(label, ms, function(i, el){
      if (!down) { down = true; mouse('mousedown', p.x, p.y, t); }
      /* a slow lissajous around the press point, ±180 px — a hand exploring the patch */
      var ph = el / 1000 * 1.3, x = p.x + 180 * Math.sin(ph), y = p.y + 110 * Math.sin(ph * 1.7);
      mouse('mousemove', x, y); if (el >= ms - 20) mouse('mouseup', x, y); }); }
  function wheelPan(ms){ var r = pageRect(), c = { x: r.left + r.width * 0.5, y: r.top + r.height * 0.5 };
    return measure('pan-wheel', ms, function(i, el){ var ph = el / 1000 * 1.1;
      /* two trackpad events per frame, like a real two-finger scroll */
      wheel(c.x, c.y, 4 * Math.cos(ph), 4 * Math.sin(ph * 1.3)); wheel(c.x, c.y, 4 * Math.cos(ph), 4 * Math.sin(ph * 1.3)); }); }
  /* the zoom follows a time curve (a hand's pinch does not slow down because frames drop): each frame
     sends two ctrl-wheel events that together land on z(t) = z0·(zTo/z0)^(t/ms) */
  function pinch(label, ms, zTo){ var r = pageRect(), c = { x: r.left + r.width * 0.5, y: r.top + r.height * 0.5 };
    var z0 = window.__tpView().z;
    return measure(label, ms, function(i, el){ var zt = z0 * Math.pow(zTo / z0, Math.min(1, el / ms)), zc = window.__tpView().z,
        dy = -Math.log(zt / zc) / 2 / 0.006; wheel(c.x, c.y, 0, dy, true); wheel(c.x, c.y, 0, dy, true); }); }

  (async function(){
    try {
      announce('load'); await wait(1200);
      if (A.preset) { var lp = NF('loadPatchFile'); if (!lp) throw new Error('no loadPatchFile');
        var r = await lp(A.preset); R.info.load = String(r).slice(0, 60); await wait(2500); }
      window.setActivePanel('tp'); await wait(2500);
      if (A.stress) {
        ['a','b','c','d'].forEach(function(o){ S('SYN_OSC_' + o.toUpperCase() + '_ENABLE', 1); });
        var cores = ['reverb','delay','saturate','granular','tape','flt','cho','fla','pha','eqz','wid','cmp','ott','bod','utl','spl'], k = 0, guard = 0;
        while (window.__tpNodes().length < A.stress && guard++ < 200) { window.__fxrAdd(cores[k++ % cores.length]); if (k % 8 === 0) await wait(250); }
        await wait(2500);
      }
      try { window.__tpSync(); } catch (e) {} await wait(1000);
      window.__tpFit(); await wait(1500);
      var nodes = window.__tpNodes();
      R.info.nodes = nodes.length; R.info.keys = nodes.map(function(n){ return n.key; }).join(',').slice(0, 400);
      R.info.canvases = document.querySelectorAll('#tp-page canvas').length;
      R.info.cables = window.__tpCables ? window.__tpCables().length : null;
      R.info.dom = document.querySelectorAll('#tp-page *').length;
      R.info.fitZ = +window.__tpView().z.toFixed(2);
      announce('built'); await wait(2500);
      /* what the Patcher's 700 ms sync() costs once the patch is built (a long task there is a periodic hitch) */
      var sy = []; for (var k3 = 0; k3 < 5; k3++) { var a3 = performance.now(); try { window.__tpSync(); } catch (e) {} sy.push(+(performance.now() - a3).toFixed(1)); await wait(150); }
      R.info.syncMs = sy.join(',');
      var v0 = window.__tpView();
      announce('cpu-rest'); await wait(6000);   /* no probe at all: the host's CPU reading for this phase is the page as a user has it */
      await measure('rest-fit', 6000, null);
      if (A.exp === 'hide') {   /* attribute the GPU/WebContent cost per node: hide one at a time */
        if (A.hz) { window.__tpSetView(v0.x, v0.y, A.hz); await wait(1200); await measure('hz-rest', 3000, null); }
        var ks = window.__tpNodes().map(function(n){ return n.key; });
        R.info.cv = ks.map(function(k){ var n = window.__tpNodeByKey(k), px = 0, c = 0; if (n && n.wrap) n.wrap.querySelectorAll('canvas').forEach(function(cv){ px += cv.width * cv.height; c++; });
          return k + ':' + c + '/' + (px / 1e6).toFixed(2); }).join(' ');
        for (var hk = 0; hk < ks.length; hk++) { var nn = window.__tpNodeByKey(ks[hk]); if (!nn || !nn.wrap) continue; var od = nn.wrap.style.display; nn.wrap.style.display = 'none';
          await wait(400); await measure('hide:' + ks[hk], 3000, null); nn.wrap.style.display = od; }
        var all = ks.map(function(k){ return window.__tpNodeByKey(k); }).filter(function(n){ return n && n.wrap; });
        all.forEach(function(n){ n.wrap.style.display = 'none'; }); await wait(400); await measure('hide:ALL', 3000, null); all.forEach(function(n){ n.wrap.style.display = ''; }); await wait(600); window.__tpSetView(v0.x, v0.y, v0.z); await wait(800);
      }
      if (A.exp === 'spk') {   /* the cable sparks: their drop-shadow, then the sparks themselves, then the mod underlines too */
        var st = document.createElement('style'); document.head.appendChild(st);
        st.textContent = '#tp-page .tp-cables .cable .spk{filter:none !important}'; await wait(400); await measure('SPK-nofilter', 4000, null); announce('cpu-SPK-nofilter'); await wait(4000);
        st.textContent = '#tp-page .tp-cables .cable .spk{display:none !important}'; await wait(400); await measure('SPK-hidden', 4000, null); announce('cpu-SPK-hidden'); await wait(4000);
        window.__tiFrameUnreg('sm-ul'); await wait(400); await measure('SPK-hid+NOSMUL', 4000, null); announce('cpu-SPK-hid+NOSMUL'); await wait(4000);
        window.__tiFrameReg('sm-ul', function(){ window.__ulTick(); }); st.remove(); await wait(600);
      }
      if (A.exp === 'blank') {   /* evidence: how many on-screen canvases are BLANK (a wiped bitmap) in the frames of a zoom-in glide */
        function scan(){ var pr = page().getBoundingClientRect(), n = 0, blank = 0;
          document.querySelectorAll('#tp-page .tp-node canvas').forEach(function(cv){ var r = cv.getBoundingClientRect(); if (!cv.width || !cv.height || r.width < 8 || r.right < pr.left || r.left > pr.right || r.bottom < pr.top || r.top > pr.bottom) return;
            try { var x = cv.getContext('2d'); if (!x) return; var d = x.getImageData(0, 0, cv.width, cv.height).data, any = false; for (var i = 3; i < d.length; i += 4 * 7) if (d[i]) { any = true; break; } n++; if (!any) blank++; } catch (e) {} });
          return [n, blank]; }
        var rest0 = scan(); R.info.blankRest = rest0.join('/');
        var z0b = window.__tpView().z, r0 = pageRect(), cx = r0.left + r0.width * 0.3, cy = r0.top + r0.height * 0.35, frames = [], t0b = performance.now();
        await new Promise(function(res){ (function st(){ var el = performance.now() - t0b, zt = z0b * Math.pow(2.2 / z0b, Math.min(1, el / 2500)), zc = window.__tpView().z;
            wheel(cx, cy, 0, -Math.log(zt / zc) / 0.006, true);
            setTimeout(function(){ var sc = scan(); frames.push(sc[1] + '/' + sc[0] + '@' + window.__tpView().z.toFixed(2)); if (el < 2600) oRAF(st); else res(); }, 0); })(); });
        await wait(600); R.info.blankAfter = scan().join('/'); R.info.blankFrames = frames.filter(function(f){ return f[0] !== '0'; }).slice(0, 30).join(' '); R.info.blankN = frames.length;
        window.__tpSetView(v0.x, v0.y, v0.z); await wait(1000);
      }
      if (A.exp === 'smprof') {   /* what inside the mod-underline pass costs: count + time DOM calls during 40 direct __ulTick() passes */
        var P = {}, on2 = false;
        function wr(obj, name, key){ var o = obj[name]; if (typeof o !== 'function') return; obj[name] = function(){ if (!on2) return o.apply(this, arguments); var a = performance.now(); try { return o.apply(this, arguments); } finally { var q = P[key] || (P[key] = [0, 0]); q[0]++; q[1] += performance.now() - a; } }; }
        wr(Document.prototype, 'querySelector', 'doc.qS'); wr(Document.prototype, 'querySelectorAll', 'doc.qSA'); wr(Element.prototype, 'querySelector', 'el.qS'); wr(Element.prototype, 'querySelectorAll', 'el.qSA');
        wr(Element.prototype, 'closest', 'closest'); wr(window, '__fxModIsDest', 'fxModIsDest'); wr(window, 'getComputedStyle', 'gCS'); wr(Element.prototype, 'getBoundingClientRect', 'gBCR'); wr(Range.prototype, 'getBoundingClientRect', 'rangeBCR');
        wr(window, '__mvLfoValAt', 'lfoValAt'); wr(Element.prototype, 'setAttribute', 'setAttr'); wr(SVGGeometryElement.prototype, 'getTotalLength', 'getTotalLength'); wr(window, '__ctlDestAt', 'ctlDestAt'); wr(window, 'getSynParam', 'getSynParam'); wr(Element.prototype, 'getAttribute', 'getAttr'); wr(Node.prototype, 'contains', 'contains');
        ['offsetParent', 'offsetWidth', 'offsetHeight', 'isConnected'].forEach(function(pn){ var proto = pn === 'isConnected' ? Node.prototype : HTMLElement.prototype, d = Object.getOwnPropertyDescriptor(proto, pn); if (!d || !d.get) return; var g = d.get;
          Object.defineProperty(proto, pn, { configurable: true, get: function(){ if (!on2) return g.call(this); var a = performance.now(); try { return g.call(this); } finally { var q = P[pn] || (P[pn] = [0, 0]); q[0]++; q[1] += performance.now() - a; } } }); });
        var FN = A.prof === 'sync' ? function(){ window.__tpSync(); } : function(){ window.__ulTick(); };
        var tT = 0; on2 = true; for (var k2 = 0; k2 < 40; k2++) { var a2 = performance.now(); FN(); tT += performance.now() - a2; } on2 = false;
        R.info.smTick = (tT / 40).toFixed(2) + 'ms/pass marks=' + document.querySelectorAll('.sm-ul').length;
        R.info.smProf = Object.keys(P).sort(function(x, y){ return P[y][1] - P[x][1]; }).map(function(k){ return k + ':' + (P[k][0] / 40).toFixed(0) + 'x/' + (P[k][1] / 40).toFixed(2) + 'ms'; }).join(' ');
      }
      if (A.exp === 'painters') {   /* per painter: what the frame (JS + render) and the GPU process pay for it */
        var names = Object.keys(PF).filter(function(k){ return A.withTp || k !== 'tp'; });
        if (A.only) names = names.filter(function(k){ return A.only.indexOf(k) >= 0; });
        R.info.painters = names.join(',');
        for (var pi = 0; pi < names.length; pi++) { var nm = names[pi], fnp = PF[nm]; window.__tiFrameUnreg(nm); await wait(300);
          await measure('no:' + nm, 2500, null); window.__tiFrameReg(nm, fnp); }
        var keep = {}; names.forEach(function(k){ keep[k] = PF[k]; window.__tiFrameUnreg(k); }); await wait(300); await measure('no:ALL', 2500, null);
        names.forEach(function(k){ window.__tiFrameReg(k, keep[k]); }); await wait(500);
      }
      if (A.exp === 'nosmul') { window.__tiFrameUnreg('sm-ul'); await measure('rest-fit-NOSMUL', 5000, null); announce('cpu-rest-NOSMUL'); await wait(5000);
        window.__tiFrameReg('sm-ul', function(){ window.__ulTick(); }); }
      await dragPan('pan-drag', 5000);
      window.__tpSetView(v0.x, v0.y, v0.z); await wait(800);
      await wheelPan(4000);
      window.__tpSetView(v0.x, v0.y, v0.z); await wait(800);
      await pinch('zoom-in', 3000, 2.0);
      await measure('settle-in', 1200, null);
      await pinch('zoom-out', 3000, 0.3);
      await measure('settle-out', 1200, null);
      window.__tpSetView(v0.x, v0.y, 1.0); await wait(1200);
      await measure('rest-z1', 4000, null);
      await dragPan('pan-drag-z1', 4000);
      window.__tpFit(); await wait(1500);
      announce('cpu-silent'); await wait(7000);   /* the host releases the chord here: the Patcher at rest with nothing sounding */
      await measure('silent-fit', 4000, null);
      R.info.pageErrs = Object.keys(errSeen).length;
      announce('done'); announcing = false; publish();
    } catch (e) { R.errs.push('run: ' + (e && e.message)); announce('failed'); announcing = false; publish(); }
  })();
  return 'started';
})();
