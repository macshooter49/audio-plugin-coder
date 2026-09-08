// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb570_popout.js — THE CURVE CARD POPS OUT ON A GUEST HOST, FOR REAL, AND THE POPPED PAGE SAYS SO.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/curve_probe.mm -o /tmp/curve_probe \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    CP_SETTLE=12000 CP_REPORT=<scratch>/report.js /tmp/curve_probe Tests/fb570_popout.js
//    (report.js is one line: `(function(){ return String(window.__cpTrace||'(no trace)'); })()`)
//
//  WHY IT EXISTS. Tests/crv_popout_gate.js proves the page logic with a stubbed Juce; this runs the
//  same gesture in the INSTALLED AU's real editor: the ⧉ door parks the identity with the real
//  setCardState native and pops a real native window (?card=crv, a second WebView) which boots the
//  warp host and REPORTS what it opened into the processor ('crvBoot' — the fb570 breadcrumb, the
//  only way a second WebView is observable from anywhere). Then a mod curve is opened while the
//  card floats (a retarget through the real retargetCard native — the popped page reports again),
//  and Dock (the exact JS the C++ evaluates on dockCardWindow) brings the docked card back on the
//  host it left with. Read the trace: every line names what the PROCESSOR saw, not what the page hoped.
//
//  READ IT AS: "the POPPED PAGE opened: {"key":"warp",...}" = the second WebView booted the warp host;
//  "rebooted as: {"key":"mod",...}" = the retarget reached it; "after redock: {"key":"mod"...}" = Dock
//  came home on the host it left with. getPoppedCards must say crv after the pop.
// ══════════════════════════════════════════════════════════════════════════════════════════════
(function(){
  var T=[]; function NF(n){ try{ return (window.Juce&&window.Juce.getNativeFunction)?window.Juce.getNativeFunction(n):null; }catch(e){ return null; } }
  function open(){ var c=document.querySelector('.crv-ext'); return !!(c&&c.classList.contains('open')); }
  setTimeout(function(){ try{
    var card=window.__paramCardinality('SYN_OSC_A_WARP_MODE');
    window.__setSynParam('SYN_OSC_A_WARP_MODE', 7/(card-1));      /* Fractalize */
    window.__setSynParam('SYN_OSC_A_WARP_AMOUNT', 0.6);
    setTimeout(function(){
      window.__openWarpExt('a',0,{clientX:400,clientY:300,target:document.body});
      setTimeout(function(){
        T.push('docked: '+JSON.stringify(window.__crvOpenState()));
        var pop=document.querySelector('.crv-ext .pop'); T.push('pop control: '+(pop?'"'+pop.textContent.trim()+'"':'NONE'));
        if(pop) pop.click();   /* the REAL door: setCardState('crv', identity) then popOutCard -> a native window boots ?card=crv */
        setTimeout(function(){
          T.push('after pop: docked open='+open()+'  __poppedCards='+JSON.stringify(window.__poppedCards));
          Promise.all([NF('getPoppedCards')(), NF('getCardState')('crv'), NF('getCardState')('crvBoot')]).then(function(r){
            T.push('getPoppedCards: "'+r[0]+'"'); T.push('identity parked: '+r[1]); T.push('the POPPED PAGE opened: '+(r[2]||'(nothing reported)'));
            window.__tiAddRoute(0,1,64);   /* LFO 1 -> Osc A Level */
            setTimeout(function(){ window.__tiCurveEdit(0);   /* open its curve WHILE the card floats: retarget, never a docked twin */
              setTimeout(function(){
                T.push('mod curve opened while floating: docked open='+open()+'  main host='+window.__crvHostKey());
                Promise.all([NF('getCardState')('crv'), NF('getCardState')('crvBoot')]).then(function(r2){
                  T.push('identity after retarget: '+r2[0]); T.push('the POPPED PAGE rebooted as: '+(r2[1]||'(nothing reported)'));
                  window.__cardWinGone('crv'); window.__redockCard('crv');   /* what the C++ evaluates on Dock */
                  setTimeout(function(){ T.push('after redock: docked open='+open()+'  '+JSON.stringify(window.__crvOpenState())); window.__cpTrace=T.join('\n'); },1400);
                });
              },1800);
            },350);
          });
        },4000);
      },900);
    },250);
  }catch(e){ T.push('!! '+e); window.__cpTrace=T.join('\n'); } },400);
  return 'popout'; })()
