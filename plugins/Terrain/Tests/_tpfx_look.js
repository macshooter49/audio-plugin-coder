/* tpfx — Terrain FX in the REAL WebView: a shot of the boot page, the add palette, and the MOD page. */
(function(){
  var out = {}, gen = 0;
  function pub(){ document.title = 'tp34:' + (++gen) + ':0/1:' + JSON.stringify(out).slice(0, 900); }
  function wait(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  setTimeout(async function(){
    out.panel = (typeof currentActivePanel !== 'undefined') ? currentActivePanel : '?';
    out.nodes = [].slice.call(document.querySelectorAll('#tp-page .tp-node')).map(function(n){ return (n.getAttribute('data-kind') || '') + ':' + (n.getAttribute('data-id') || '').slice(0, 14); }).slice(0, 12);
    document.title = 'tpzP:boot'; await wait(1600);
    try { document.getElementById('mod-btn').click(); } catch (e) {} document.title = 'tpzP:mod'; await wait(1600);
    try { document.getElementById('syn-btn').click(); } catch (e) {} document.title = 'tpzP:back'; await wait(1600);
    pub(); setTimeout(pub, 500);
  }, 3500);
})();
