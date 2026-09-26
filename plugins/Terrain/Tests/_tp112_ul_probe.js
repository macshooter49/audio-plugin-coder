/* tp112 — WHERE DO THE MOD MARKS LAND? Real WebView (mac_patcher_glide.mm): load a preset, open the Patcher, and list
   every visible .sm-ul — its knob (data-syn), whether that knob is inside #tp-page, and whether the knob's own box is
   actually what the eye sees at that point (elementFromPoint). A mark whose knob is hidden under the Patcher is a stray. */
(function(){
  var A = window.__tpzArgs || {}, out = { errs: [] }, gen = 0;
  function NF(n){ try { return window.Juce.getNativeFunction(n); } catch (e) { return null; } }
  function pub(){ document.title = 'tp34:' + (++gen) + ':0/1:' + JSON.stringify(out).slice(0, 950); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function marks(){ var r = []; document.querySelectorAll('.sm-ul').forEach(function(u){
      if (u.style.display !== 'block') return; var b = u.getBoundingClientRect(), tgt = u.__el || null;
      var hit = document.elementFromPoint(b.left + b.width / 2, b.top - 4);
      r.push([Math.round(b.left), Math.round(b.top), Math.round(b.width), hit ? (hit.id || String(hit.className).split(' ')[0]).slice(0, 18) : '-']); });
    return r; }
  setTimeout(async function(){
    try {
      if (A.preset) { await NF('loadPatchFile')(A.preset); await wait(2500); }
      if (typeof setActivePanel === 'function') setActivePanel('syn'); await wait(1200);
      out.syn = marks(); document.title = 'tpzP:syn'; await wait(1500);
      if (A.silent) { document.title = 'tpzP:cpu-silent'; await wait(2500); }   /* the harness releases the chord: an idle page, like Max's */
      setActivePanel('tp'); await wait(2500);
      out.tp = marks(); out.view = window.__tpView ? window.__tpView() : null; out.moving = !!window.__tpMoving; out.frozen = !!window.__tiFrozen;
      /* pan with the trackpad (wheel over blank canvas) and zoom (ctrl-wheel), as a hand does, then let it settle */
      var pg = document.getElementById('tp-page'), pr = pg.getBoundingClientRect(), x = pr.left + 30, y = pr.top + 60;
      function wh(dx, dy, ctrl){ var t = document.elementFromPoint(x, y) || pg; t.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, clientX: x, clientY: y, deltaX: dx, deltaY: dy, deltaMode: 0, ctrlKey: !!ctrl, view: window })); }
      for (var i = 0; i < 30; i++) { wh(-6, -5); await wait(16); if (i === 20) { out.mid = marks(); out.midMoving = !!window.__tpMoving; } }   /* MID-GLIDE: Max's video */
      for (var j = 0; j < 20; j++) { wh(0, 4, true); await wait(16); }
      await wait(900);
      out.pan = marks(); out.view2 = window.__tpView ? window.__tpView() : null;
      document.title = 'tpzP:panned'; await wait(1500);
      document.title = 'tpzP:tp'; await wait(1500);
    } catch (e) { out.errs.push(String(e.message)); }
    pub(); setTimeout(pub, 600);
  }, 2500);
})();
