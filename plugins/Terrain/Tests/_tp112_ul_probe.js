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
      setActivePanel('tp'); await wait(2500);
      out.tp = marks(); out.view = window.__tpView ? window.__tpView() : null; out.moving = !!window.__tpMoving; out.frozen = !!window.__tiFrozen;
      document.title = 'tpzP:tp'; await wait(1500);
    } catch (e) { out.errs.push(String(e.message)); }
    pub(); setTimeout(pub, 600);
  }, 2500);
})();
