/* tp112 — layout truth in the REAL WebView (Tests/mac_patcher_glide.mm, TPZ_SHOTS=<dir>): is anything scrollable at the
   root or under the output strip, does the rack's bottom row fit, and a shot of SYN / browser / Settings / Patcher. */
(function(){
  var out = { errs: [] }, gen = 0;
  window.addEventListener('error', function(e){ out.errs.push(String(e.message).slice(0, 80)); });
  function pub(){ document.title = 'tp34:' + (++gen) + ':0/1:' + JSON.stringify(out).slice(0, 900); }
  function scrollers(){ var r = [], all = document.querySelectorAll('body *');
    for (var i = 0; i < all.length; i++){ var e = all[i], cs = getComputedStyle(e);
      if ((cs.overflowY === 'auto' || cs.overflowY === 'scroll') && e.scrollHeight > e.clientHeight + 1 && e.offsetParent !== null)
        r.push((e.id ? '#' + e.id : e.className.toString().split(' ')[0]) + ':' + e.scrollHeight + '/' + e.clientHeight); }
    return r.slice(0, 8); }
  function step(ph, fn, ms){ return new Promise(function(res){ try { fn(); } catch (e) { out.errs.push(ph + ':' + e.message); } document.title = 'tpzP:' + ph; setTimeout(res, ms || 1400); }); }
  setTimeout(async function(){
    await step('syn', function(){ document.getElementById('syn-btn').click(); });
    var de = document.scrollingElement || document.documentElement;
    out.vp = [innerWidth, innerHeight, de.scrollWidth, de.scrollHeight, de.clientHeight, getComputedStyle(document.documentElement).overflow];
    var sp = document.getElementById('syn-panel').getBoundingClientRect(); out.syn = [Math.round(sp.top), Math.round(sp.bottom)];
    var rk = [].slice.call(document.querySelectorAll('#syn-panel .fxr-dev')).map(function(d){ var b = d.getBoundingClientRect(); return Math.round(b.bottom * 10) / 10; });
    out.rackBottoms = rk.slice(0, 6); out.scrollers = scrollers();
    var y = innerHeight - 6, hit = document.elementFromPoint(innerWidth * 0.8, y); out.bottomHit = hit ? (hit.id || hit.className.toString().slice(0, 40)) : null;
    pub();
    await step('browser', function(){ var b = document.querySelector('#preset-name, .preset-name, #pn-name'); if (b) b.click(); else if (window.openPresetBrowser) window.openPresetBrowser(); });
    await step('close1', function(){ document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })); });
    await step('settings', function(){ var g = document.querySelector('#settings-btn, .settings-btn, [title*="Settings"]'); if (g) g.click(); });
    await step('close2', function(){ document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })); if (window.__st && window.__st.open) window.__st.open(false); });
    await step('tp', function(){ document.getElementById('syn-btn').click(); }, 2500);
    out.done = 1; pub();
  }, 2500);
})();
