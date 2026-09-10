/* a minimal PNG reader — enough to sample pixels out of a puppeteer screenshot.
   Node ships zlib but no image decoder, and the claim "these two bands are the same colour" is a
   PIXEL claim; anything less is an eyeball. Handles 8-bit colortype 2 (RGB) and 6 (RGBA). */
const zlib = require ('zlib');
function decode (buf) {
  if (buf.readUInt32BE (0) !== 0x89504E47) throw new Error ('not a PNG');
  let off = 8, w = 0, h = 0, depth = 0, ct = 0; const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE (off), type = buf.toString ('ascii', off + 4, off + 8);
    const data = buf.slice (off + 8, off + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE (0); h = data.readUInt32BE (4); depth = data[8]; ct = data[9]; }
    else if (type === 'IDAT') idat.push (data);
    else if (type === 'IEND') break;
    off += 12 + len;
  }
  if (depth !== 8 || (ct !== 6 && ct !== 2)) throw new Error ('unsupported PNG: depth ' + depth + ' colortype ' + ct);
  const bpp = ct === 6 ? 4 : 3, stride = w * bpp;
  const raw = zlib.inflateSync (Buffer.concat (idat));
  const out = Buffer.alloc (h * stride);
  let prev = Buffer.alloc (stride);
  for (let y = 0; y < h; ++y) {
    const f = raw[y * (stride + 1)], line = raw.slice (y * (stride + 1) + 1, (y + 1) * (stride + 1));
    const cur = Buffer.alloc (stride);
    for (let i = 0; i < stride; ++i) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0, x = line[i];
      let v;
      if (f === 0) v = x; else if (f === 1) v = x + a; else if (f === 2) v = x + b;
      else if (f === 3) v = x + ((a + b) >> 1);
      else { const p = a + b - c, pa = Math.abs (p - a), pb = Math.abs (p - b), pc = Math.abs (p - c);
             v = x + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c); }
      cur[i] = v & 255;
    }
    cur.copy (out, y * stride); prev = cur;
  }
  return { w, h, bpp, stride, data: out };
}
const px = (img, x, y) => { const i = y * img.stride + x * img.bpp; return [img.data[i], img.data[i + 1], img.data[i + 2]]; };
/* the average colour of a band, so one stray glyph pixel cannot decide a seam test */
function band (img, y0, y1, x0, x1) {
  let r = 0, g = 0, b = 0, n = 0;
  for (let y = y0; y < y1; ++y) for (let x = x0; x < x1; ++x) { const c = px (img, x, y); r += c[0]; g += c[1]; b += c[2]; ++n; }
  return [r / n, g / n, b / n];
}
const dist = (a, b) => Math.max (Math.abs (a[0] - b[0]), Math.abs (a[1] - b[1]), Math.abs (a[2] - b[2]));
const hex = c => '#' + c.map (v => Math.round (v).toString (16).padStart (2, '0')).join ('');
module.exports = { decode, px, band, dist, hex };
