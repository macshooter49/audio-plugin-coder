// ══════════════════════════════════════════════════════════════════════════════════════════════
//  _org_real.js — tp105 the Organics view measured in the REAL WebView (WKWebView inside the AU's own editor).
//
//    node Tests/_org_real.js <Terrain.component> <out-dir> [hdr,cpu,shots] [orgui binary]
//
//  Builds Tests/mac_org_ui.mm if no binary is given, writes a driver into out-dir and runs it. Phases:
//    hdr   — THE HEADER CENTERLINE on ink (Tests/_org_ink.js, the same code the headless gate runs): osc A under every
//            engine, Organics with and without the articulation pill, the swapped (←) state; red-line proof PNGs.
//    cpu   — WHAT THE VISUALISER COSTS: for each instrument, rest / 1 note / a 4-note chord / a fast run (12 notes/s),
//            each with the reaction ON and OFF (window.__orgAnim.off), 8 s of wall time each: this process (audio +
//            JUCE message thread), the page's WebContent process, WebKit's GPU process, and the plugin's own probe
//            line (DSP vs UI). The reaction's cost = ON − OFF.
//    shots — the Organics osc in the real editor (a few families, playing).
// ══════════════════════════════════════════════════════════════════════════════════════════════
const fs = require('fs'), path = require('path'), cp = require('child_process');
const INK = require('./_org_ink.js');
const COMP = process.argv[2], OUT = process.argv[3] || '/tmp/org_real', PHASES = (process.argv[4] || 'hdr,cpu,shots').split(',');
let BIN = process.argv[5];
fs.mkdirSync(OUT, { recursive: true });
if (!BIN) { BIN = path.join(OUT, 'orgui');
  cp.execSync(`clang++ -std=c++17 -fobjc-arc -O2 "${path.join(__dirname, 'mac_org_ui.mm')}" -o "${BIN}" -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework WebKit`, { stdio: 'inherit' }); }

const driver = `(function () {
  if (window.__orgReal) return;
  var C = ${INK.COLLECT.toString()}, AN = ${INK.ANALYZE.toString()};
  var queue = [], cur = null, OUT = ${JSON.stringify(OUT)}, PH = ${JSON.stringify(PHASES)};
  function host (cmd) { return new Promise(function (res) { queue.push({ cmd: cmd, res: res }); }); }
  window.__orgReal = { next: function (r) { if (cur) { var c = cur; cur = null; c.res(r); } if (queue.length) { cur = queue.shift(); return cur.cmd; } return { cmd: 'wait', ms: 40 }; } };
  var sleep = function (ms) { return new Promise(function (r) { setTimeout(r, ms); }); };
  var log = function (t) { return host({ cmd: 'log', text: String(t) }); };
  function setEngine (o, i) { var s = document.getElementById('osc-' + o + '-engine-select'); if (!s) return; s.value = String(i); s.dispatchEvent(new Event('change', { bubbles: true })); }
  async function inkOf (ids, snapName) { var g = C(ids); var s = await host({ cmd: 'snap', scale: 3 / (window.devicePixelRatio || 1), path: OUT + '/' + snapName + '.png' });
    var dpr = s.w / window.innerWidth; var r = await AN(s.b64, g, dpr); return { dpr: dpr, g: g, r: r, scale: ids.map(function (id) { var d = document.getElementById(id); return d ? d.getBoundingClientRect().height / d.offsetHeight : 1; }) }; }
  function redLine (y, x0, x1) { var e = document.createElement('div'); e.className = 'org-redline'; e.style.cssText = 'position:fixed;z-index:99999;pointer-events:none;height:1px;background:#ff2020;left:' + x0 + 'px;width:' + (x1 - x0) + 'px;top:' + (y - 0.5) + 'px'; document.body.appendChild(e); return e; }
  (async function main () {
    var R = { ua: navigator.userAgent, dpr: window.devicePixelRatio, vw: window.innerWidth, vh: window.innerHeight };
    try {
      await log('page ' + window.innerWidth + 'x' + window.innerHeight + ' dpr ' + window.devicePixelRatio);
      try { window.__setSynParam('SYN_OSC_A_ENABLE', 1); } catch (e) {}
      if (PH.indexOf('hdr') >= 0) {
        R.hdr = [];
        for (var eng = 0; eng < 8; eng++) { setEngine('a', eng); await sleep(1200);
          if (eng === 7) { await window.__orgSetInstrument('a', 'vsco2.strings.violin-section'); await sleep(900); }
          var m = await inkOf(['osc-a-device'], 'hdr-eng' + eng); R.hdr.push({ eng: eng, cls: document.getElementById('osc-a-device').className, ink: m.r['osc-a-device'], scale: m.scale[0], dpr: m.dpr });
          await log('hdr engine ' + eng + ' measured'); }
        // Organics without the pill (one articulation) and the glockenspiel (Max's screenshot)
        await window.__orgSetInstrument('a', 'vcsl.mallets.glockenspiel'); await sleep(900);
        var mg = await inkOf(['osc-a-device'], 'hdr-glock'); R.hdr.push({ eng: 'org-glock', ink: mg.r['osc-a-device'], scale: mg.scale[0], dpr: mg.dpr });
        // the swapped side (the + becomes the ←)
        var sw = document.querySelector('#osc-a-device .swap-btn'); sw.click(); await sleep(700);
        var ms = await inkOf(['osc-a-device'], 'hdr-swapped'); R.hdr.push({ eng: 'org-swapped', ink: ms.r['osc-a-device'], scale: ms.scale[0], dpr: ms.dpr });
        sw.click(); await sleep(500);
        R.hdrRects = C(['osc-a-device']);
      }
      if (PH.indexOf('shots') >= 0) {
        setEngine('a', 7); await sleep(600);
        var list = [['vcsl.mallets.glockenspiel', [79, 84, 88, 91]], ['salamander.grand.v3', [48, 55, 64, 72]], ['vcsl.mallets.marimba', [55, 62, 76, 83]], ['karoryfer.strings.cello', [43, 50]], ['mtg.sax.alto', [61]]];
        for (var i = 0; i < list.length; i++) { await window.__orgSetInstrument('a', list[i][0]); await sleep(1500);
          await host({ cmd: 'snap', path: OUT + '/real-' + list[i][0] + '.png' });
          await host({ cmd: 'notes', notes: list[i][1], vel: 100 }); await sleep(450);
          await host({ cmd: 'snap', path: OUT + '/real-' + list[i][0] + '-play.png' });
          await host({ cmd: 'notes', notes: [] }); await sleep(900); }
      }
      if (PH.indexOf('cpu') >= 0) {
        R.cpu = []; setEngine('a', 7); await sleep(600);
        var inst = (${process.env.ORG_CPU_INST || "null"} || [['vcsl.mallets.glockenspiel', 79], ['salamander.grand.v3', 60], ['vcsl.mallets.marimba', 60], ['vcsl.mallets.kalimba', 60], ['vsco2.strings.cello-section', 48], ['mtg.sax.alto', 61], ['vsco2.woodwinds.flute', 72], ['freepats.voice.synth-choir', 60]]);
        for (var k = 0; k < inst.length; k++) { var id = inst[k][0], b = inst[k][1];
          await window.__orgSetInstrument('a', id); await sleep(2500);
          var states = [['rest', []], ['1 note', [b]], ['chord', [b, b + 4, b + 7, b + 12]], ['run', [b, b + 2, b + 4, b + 5, b + 7, b + 9, b + 11, b + 12, b + 14, b + 16, b + 17, b + 19]]];
          for (var s = 0; s < states.length; s++) { var acc = { on: [], off: [] };
            /* the reaction ON and OFF alternate twice over the SAME notes (a slow drift of the page cannot pose as the reaction) */
            if (states[s][0] === 'run') await host({ cmd: 'run', notes: states[s][1], hz: 12, vel: 100 }); else await host({ cmd: 'notes', notes: states[s][1], vel: 100 });
            for (var rep = 0; rep < 4; rep++) { var on = (rep % 2) === 0;
              window.__orgAnim.off = !on; try { window.__orgRelease('a'); } catch (e) {}   /* every window re-triggers from the next feed snapshot (the feed repeats while notes sound) */
              var f0 = window.__orgAnim.frames, c0 = window.__orgAnim.cost, v0 = window.__orgVizN || 0; await sleep(700); var fxN = document.querySelectorAll('#osc-a-device .org-fx .pt').length;
              var cpu = await host({ cmd: 'cpu', secs: 6 });
              acc[on ? 'on' : 'off'].push({ host: cpu.host, wc: cpu.webcontent, gpu: cpu.gpu, frames: window.__orgAnim.frames - f0, js: window.__orgAnim.cost - c0, viz: (window.__orgVizN || 0) - v0, fx: fxN, occ: cpu.occluded ? 1 : 0, beacon: String(cpu.beacon || '').split('\\n').slice(0, 2).join(' || ') }); }
            await host({ cmd: 'notes', notes: [] }); await sleep(900);
            var mean = function (a, f) { return a.reduce(function (x, q) { return x + q[f]; }, 0) / a.length; };
            var row = { id: id, fam: (window.__orgView('a') || {}).fam, twin: ((window.__orgVars('a') || {}).vars || []).length === 2, state: states[s][0] };
            ['host', 'wc', 'gpu', 'frames', 'js', 'viz', 'fx', 'occ'].forEach(function (f) { row[f + 'On'] = +mean(acc.on, f).toFixed(2); row[f + 'Off'] = +mean(acc.off, f).toFixed(2); });
            row.beacon = acc.on[0].beacon; R.cpu.push(row); await log(JSON.stringify(row).slice(0, 300)); }
        }
        window.__orgAnim.off = false;
      }
    } catch (e) { R.error = String(e && e.stack || e); }
    await host({ cmd: 'done', result: R });
  })();
})();`;
fs.writeFileSync(path.join(OUT, 'driver.js'), driver);
const res = path.join(OUT, 'result.json');
try { fs.unlinkSync(res); } catch (e) {}
cp.spawnSync(BIN, [COMP, path.join(OUT, 'driver.js'), res, '1500'], { stdio: 'inherit' });
if (!fs.existsSync(res)) { console.log('no result'); process.exit(1); }
const R = JSON.parse(fs.readFileSync(res, 'utf8'));
if (R.error) console.log('DRIVER ERROR', R.error);
console.log(`page ${R.vw}x${R.vh} dpr ${R.dpr}`);
(R.hdr || []).forEach(h => { const c = INK.centreline(h.ink); if (!c) { console.log('hdr', h.eng, 'no osc ink'); return; }
  console.log(`hdr ${String(h.eng).padEnd(12)} worst ${(c.worst / h.scale).toFixed(2)} css px (${c.worst.toFixed(2)} vp px, scale ${h.scale.toFixed(3)}) | ` + c.items.map(q => q.key + (q.txt ? '(' + q.txt.slice(0, 8) + ')' : '') + ' ' + (q.d / h.scale).toFixed(2)).join('  ')); });
(R.cpu || []).forEach(r => console.log(`cpu ${r.id.padEnd(28)} ${r.twin ? 'twin' : '    '} ${r.state.padEnd(7)} | WebContent ${r.wcOn.toFixed(2).padStart(6)} on ${r.wcOff.toFixed(2).padStart(6)} off Δ ${(r.wcOn - r.wcOff).toFixed(2).padStart(6)} | GPU ${r.gpuOn.toFixed(2).padStart(6)} on ${r.gpuOff.toFixed(2).padStart(6)} off Δ ${(r.gpuOn - r.gpuOff).toFixed(2).padStart(6)} | host ${r.hostOn.toFixed(2)}/${r.hostOff.toFixed(2)} | painter ${r.framesOn} fr ${r.jsOn} ms, feed ${r.vizOn} events, lifted ${r.fxOn}${(r.occOn || r.occOff) ? ' !! OCCLUDED' : ''} | ${r.beacon.replace(/\r/g, '').slice(0, 120)}`));
