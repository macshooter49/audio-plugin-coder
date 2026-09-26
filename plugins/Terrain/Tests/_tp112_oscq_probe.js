/* tp112 — the osc right-click quick sliders (Volume / Pan / Semitone) with Pan at L5, shot in the REAL WebView. */
(function(){
  var out = {}, gen = 0;
  function pub(){ document.title = 'tp34:' + (++gen) + ':0/1:' + JSON.stringify(out).slice(0, 900); }
  setTimeout(function(){
    try { setActivePanel('syn'); } catch (e) {}
    setTimeout(function(){
      try { window.Juce.getSliderState('SYN_OSC_A_PAN').setNormalisedValue(0.475); } catch (e) { out.e1 = e.message; }
      var dev = document.getElementById('osc-a-device'), cv = dev && dev.querySelector('canvas'), r = cv.getBoundingClientRect();
      cv.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2, view: window }));
      setTimeout(function(){ var m = document.querySelector('.samp-menu'); out.menu = m ? [m.getBoundingClientRect().left | 0, m.getBoundingClientRect().top | 0, m.offsetWidth, m.offsetHeight] : null;
        document.title = 'tpzP:menu'; setTimeout(pub, 1600); }, 500);
    }, 1500);
  }, 2500);
})();
