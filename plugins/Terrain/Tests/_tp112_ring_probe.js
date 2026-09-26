/* tp112 — the Aftertouch ring on Fold A in the REAL WebView (Tests/mac_patcher_glide.mm with TPZ_PRESS set).
   Routes {at:1} → Fold A, then samples every 300 ms: __mvAT, the ring's dasharray, the underline's comet. */
(function(){
  var out = { s: [], errs: [] }, gen = 0;
  window.addEventListener('error', function(e){ out.errs.push(String(e.message).slice(0, 80)); });
  function pub(){ var str = JSON.stringify(out); document.title = 'tp34:' + (++gen) + ':0/1:' + str.slice(0, 900); }
  setTimeout(function(){
    try { document.getElementById('syn-btn').click(); } catch (e) {}
    setTimeout(function(){
      var el = document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_FOLD_AMT"]');
      var g = window.__ctlDestAt ? window.__ctlDestAt(el) : null; out.dest = g && g.dest;
      out.add = window.__tiAddSrc ? window.__tiAddSrc({ at: 1 }, g.dest) : 'nohook';
      var w = document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]'), gw = window.__ctlDestAt(w); window.__tiAddSrc({ at: 1 }, gw.dest);
      document.title = 'tpzP:built';
      var n = 0, iv = setInterval(function(){
        var R = el.querySelector('.sm-ring'), uls = [].slice.call(document.querySelectorAll('.sm-ul')).filter(function(u){ return u.style.display === 'block'; });
        var rs = [].slice.call(el.querySelectorAll('.sm-ring')).map(function(r){ return (r.style.display||'-') + '/' + r.getAttribute('stroke-dasharray'); });
        var rw = [].slice.call(w.querySelectorAll('.sm-ring')).map(function(r){ return (r.style.display||'-') + '/' + r.getAttribute('stroke-dasharray'); });
        var rts = window.__tiRoutes ? window.__tiRoutes() : [];
        if (n === 3) window.__smRingDbg = []; if (n === 5) out.dbg = window.__smRingDbg.slice(0, 8); if (n === 0 || n === 13) out.s.push({ tick: window.__ulTick(), err: window.__ulErr || '', at: +(window.__mvAT || 0).toFixed(2), fold: rs, warp: rw, uls: uls.length, rts: rts, geom: window.__smRingGeom(el), kr: !!el.querySelector('.knob-ring .kr-svg') });
        pub(); if (++n >= 14) clearInterval(iv);
      }, 300);
    }, 1500);
  }, 2500);
})();
