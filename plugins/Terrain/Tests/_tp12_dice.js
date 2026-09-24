// tp12 — the dice: sample engines + back panel rolled, detune never, the tape up, nothing on the compressor
const puppeteer = require('puppeteer-core'); const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', args: ['--no-sandbox'] });
  const p = await b.newPage(); await p.setViewport({ width: 820, height: 656 }); const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.evaluateOnNewDocument(() => { try { localStorage.removeItem('tpLayout'); } catch (e) {} });
  await p.goto('file://' + process.argv[2] + '?page=1', { waitUntil: 'load' }); await sleep(1500);
  await p.evaluate(() => { window.__setLog = []; const o = window.__synSliderShim; window.__synSliderShim = function (id, d) { const s = o(id, d); if (s && !s.__tp12) { const f = s.setNormalisedValue.bind(s); s.setNormalisedValue = function (v) { window.__setLog.push([id, v]); return f(v); }; s.__tp12 = 1; } return s; }; window.setActivePanel('tp'); });
  await sleep(1800);
  const R = { rolls: [] };
  for (let i = 0; i < 8; i++) {
    await p.evaluate(l => { window.__setLog.length = 0; const L = window.__tpLayout(); L.dlevel = l; L.aim = 'anything'; window.__tpGenerate(); }, i % 2 ? 'wild' : 'heavy'); await sleep(2600);
    R.rolls.push(await p.evaluate(() => { const L = window.__setLog, ids = L.map(x => x[0]); const eng = ids.filter(x => /_ENGINE$/.test(x)).map(x => [x.slice(8, 9), Math.round(L.find(y => y[0] === x)[1] * 11)]);
      const tapeOn = !document.getElementById('tape-toggle').classList.contains('off');
      return { eng: eng.map(e => e[0] + e[1]).join(' '), sampleKnobs: ids.filter(x => /_SAMPLE_/.test(x)).length, blend: ids.filter(x => /_WSLOT\d_MODE$/.test(x) && L.find(y => y[0] === x)[1] > 0).length, detune: ids.filter(x => /UDETUNE/.test(x)).length, phase: [...new Set(L.filter(x => /_PHASE$|_PHASE_AMT$|_PHASE_MODE$/.test(x[0])).map(x => x[0].slice(8) + '=' + x[1].toFixed(2)))].join(' '),
        tapeOn, tapeKnobs: L.filter(x => /^(WOW_FLUTTER|SATURATION|HISS|STUDIO_|WIRE_)/.test(x[0])).map(x => x[0].slice(0, 6) + ':' + x[1].toFixed(2)).join(' '), cmpRoutes: (window.__tiRoutes() || []).filter(r => { const c = (window.__fxrDevs() || []).find(d => d.core === 'cmp'); if (!c || !window.__fxModDest) return false; for (let k = 0; k < 12; k++) if (window.__fxModDest('cmp', c.inst, k) === r.d) return true; return false; }).length }; }));
  }
  R.errs = errs; console.log(JSON.stringify(R, null, 0)); await b.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
