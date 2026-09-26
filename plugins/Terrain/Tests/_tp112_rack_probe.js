/* tp112 — the synth page's rack bottom edge vs the viewport, in the REAL WebView. */
(function(){
  var out = {}, gen = 0;
  function pub(){ document.title = 'tp34:' + (++gen) + ':0/1:' + JSON.stringify(out).slice(0, 900); }
  setTimeout(function(){
    try { if (typeof setActivePanel === 'function') setActivePanel('syn'); } catch (e) { out.e = e.message; }
    setTimeout(function(){
      document.title = 'tpzP:syn';
      var sp = document.getElementById('syn-panel').getBoundingClientRect();
      out.vp = [innerWidth, innerHeight, devicePixelRatio]; out.syn = [sp.top, sp.bottom];
      out.rack = [].slice.call(document.querySelectorAll('#syn-panel .fxr-dev')).slice(0, 5).map(function(d){ var b = d.getBoundingClientRect(), cs = getComputedStyle(d);
        return [Math.round(b.top * 100) / 100, Math.round(b.bottom * 100) / 100, cs.borderBottomWidth, cs.borderTopWidth]; });
      var clip = document.querySelector('#syn-panel .fxr-clip'); if (clip) { var c = clip.getBoundingClientRect(); out.clip = [c.top, c.bottom, clip.scrollHeight, clip.clientHeight, getComputedStyle(clip).overflowY]; }
      setTimeout(pub, 2200); setTimeout(pub, 3000);
    }, 1800);
  }, 2500);
})();
