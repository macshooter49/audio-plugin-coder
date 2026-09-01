// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb562_warpfollow.js — THE WARP CURVE CARD FOLLOWS ITS SLOT, AND STARTS EXACTLY ONE POLL.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/curve_probe.mm -o /tmp/curve_probe \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    CP_SETTLE=9000 CP_REPORT=<scratch>/report.js /tmp/curve_probe Tests/fb562_warpfollow.js
//    (report.js is one line: `(function(){ return String(window.__cpTrace||'(no trace)'); })()`)
//
//  WHY IT EXISTS. fb560 added a 300 ms poll so the card follows the slot instead of being a
//  one-shot snapshot — and it NEVER RAN ONCE. It called `f`, which lives in __openWarpExt's scope
//  and not in openWarpCurve's, so it threw a ReferenceError on its first tick; the outer catch,
//  there to stop a runaway interval, swallowed it and cleared the timer. Silent, permanent, and
//  invisible to every other gate. It was found by COUNTING THE INTERVALS during a clean-up sweep.
//  Two bars, because either one alone would have missed it:
//    1  it FOLLOWS   — change the warp mode with the card open; the title and the points must move
//    2  it is ONE    — four opens must leave exactly one live poll (the stop condition used to be
//                      "the host stopped being warp", which is false when the next card is warp too)
//
//  MEASURED: "Fractalize · OSC A · WARP 1" -> slot changed -> "Sine Fold · OSC A · WARP 1", 19 pts;
//            four opens -> 1 poll alive.
// ══════════════════════════════════════════════════════════════════════════════════════════════
(function(){
  var T=[];
  var live={}, SI=window.setInterval, CI=window.clearInterval;
  window.setInterval=function(f,ms){ var id=SI.apply(window,arguments); if(ms===300) live[id]=1; return id; };
  window.clearInterval=function(id){ if(live[id]) delete live[id]; return CI.apply(window,arguments); };
  function alive(){ var k,c=0; for(k in live) c++; return c; }
  function ttl(){ var t=document.querySelector('.crv-ext .tt'); return t?t.textContent.trim():'?'; }
  setTimeout(function(){ try{
    var card=window.__paramCardinality('SYN_OSC_A_WARP_MODE');
    window.__setSynParam('SYN_OSC_A_WARP_MODE', 7/(card-1));      /* Fractalize */
    window.__setSynParam('SYN_OSC_A_WARP_AMOUNT', 0.6);
    setTimeout(function(){
      window.__openWarpExt('a',0,{clientX:400,clientY:300,target:document.body});
      setTimeout(function(){
        T.push('opened on: "'+ttl()+'"   polls alive='+alive());
        /* change the warp mode with the card OPEN — it must follow */
        window.__setSynParam('SYN_OSC_A_WARP_MODE', 24/(card-1));  /* Sine Fold */
        setTimeout(function(){
          T.push('after changing the slot to Sine Fold: "'+ttl()+'"   pts='+window.__crvOpenState().pts);
          /* now open THREE more cards and count the polls */
          window.__openWarpExt('a',1,{clientX:400,clientY:300,target:document.body});
          setTimeout(function(){ window.__openWarpExt('b',0,{clientX:400,clientY:300,target:document.body});
            setTimeout(function(){ window.__openWarpExt('a',0,{clientX:400,clientY:300,target:document.body});
              setTimeout(function(){
                T.push('after 4 opens: polls alive='+alive()+'   ('+(alive()===1?'ONE, as designed':'!! leaked')+')');
                window.setInterval=SI; window.clearInterval=CI;
                window.__cpTrace=T.join('\n');
              },700);
            },700);
          },700);
        },900);
      },800);
    },250);
  }catch(e){ T.push('!! '+e); window.__cpTrace=T.join('\n'); } },400);
  return 'warpfollow'; })()
