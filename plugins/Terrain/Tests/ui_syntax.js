// Syntax-gate every inline <script> block in index.html. A parse error anywhere in the 34k-line
// UI is invisible until the WebView silently renders a dead page, so this is the cheapest
// possible gate on a JS edit.
const fs = require('fs');
const vm = require('vm');

const file = process.argv[2];
const src = fs.readFileSync(file, 'utf8');
const lines = src.split('\n');

// find <script ...> ... </script> spans, skipping non-JS types
const open = /<script(\s[^>]*)?>/gi;
let m, blocks = [], bad = 0, checked = 0;
while ((m = open.exec(src)) !== null) {
  const attrs = m[1] || '';
  if (/type\s*=\s*["'](?!text\/javascript|application\/javascript)/i.test(attrs)) continue;
  const start = m.index + m[0].length;
  const end = src.indexOf('</script>', start);
  if (end < 0) continue;
  blocks.push({ start, end, line: src.slice(0, start).split('\n').length });
}

for (const b of blocks) {
  const code = src.slice(b.start, b.end);
  if (!code.trim()) continue;
  checked++;
  try {
    new vm.Script(code, { filename: `${file}:<script@${b.line}>` });
  } catch (e) {
    bad++;
    console.log(`FAIL  script starting at line ${b.line}: ${e.message}`);
    const mm = /<anonymous>:(\d+)/.exec(e.stack || '');
    if (mm) console.log(`      => index.html line ~${b.line + parseInt(mm[1], 10) - 1}`);
  }
}
console.log(`\n${checked} script blocks parsed, ${bad} failed`);
console.log(`total lines: ${lines.length}`);
process.exit(bad ? 1 : 0);
