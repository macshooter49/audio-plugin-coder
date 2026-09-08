// png.js — minimal PNG decoder (8-bit RGB / RGBA, non-interlaced — what Chrome's screenshot writes).
// Enough to read a screenshot clip back as pixels so a gate can measure where a CSS chevron's INK is.
const zlib = require ('zlib');
function decode (buf) {
  if (buf.readUInt32BE (0) !== 0x89504e47) throw new Error ('not a PNG');
  let off = 8, w = 0, h = 0, depth = 0, ctype = 0, interlace = 0; const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE (off), type = buf.toString ('ascii', off + 4, off + 8), data = buf.subarray (off + 8, off + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE (0); h = data.readUInt32BE (4); depth = data[8]; ctype = data[9]; interlace = data[12]; }
    else if (type === 'IDAT') idat.push (data);
    else if (type === 'IEND') break;
    off += 12 + len;
  }
  if (depth !== 8 || interlace !== 0) throw new Error (`unsupported PNG depth ${depth} interlace ${interlace}`);
  const bpp = ctype === 6 ? 4 : ctype === 2 ? 3 : ctype === 4 ? 2 : ctype === 0 ? 1 : 0;
  if (! bpp) throw new Error ('unsupported colour type ' + ctype);
  const raw = zlib.inflateSync (Buffer.concat (idat));
  const stride = w * bpp, out = Buffer.alloc (w * h * bpp);
  let p = 0;
  for (let y = 0; y < h; ++y) {
    const f = raw[p++]; const row = out.subarray (y * stride, (y + 1) * stride), prev = y ? out.subarray ((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; ++x) {
      const a = x >= bpp ? row[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0, v = raw[p++];
      let r;
      switch (f) {
        case 0: r = v; break;
        case 1: r = v + a; break;
        case 2: r = v + b; break;
        case 3: r = v + ((a + b) >> 1); break;
        case 4: { const pa = Math.abs (b - c), pb = Math.abs (a - c), pc = Math.abs (a + b - 2 * c);
                  r = v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c); break; }
        default: throw new Error ('bad filter ' + f);
      }
      row[x] = r & 255;
    }
  }
  const px = (x, y) => { const i = (y * w + x) * bpp; return bpp >= 3 ? [out[i], out[i + 1], out[i + 2]] : [out[i], out[i], out[i]]; };
  return { w, h, bpp, px };
}
// ink of a bright glyph on a dark ground: pixels brighter than the ground by > thr. Returns bbox + centroid in PIXELS.
function ink (img, thr = 40) {
  let bg = 0, n = 0;
  for (let y = 0; y < img.h; ++y) { const [r, g, b] = img.px (0, y); bg += (r + g + b) / 3; ++n; const [r2, g2, b2] = img.px (img.w - 1, y); bg += (r2 + g2 + b2) / 3; ++n; }
  bg /= n;
  let x0 = 1e9, x1 = -1, y0 = 1e9, y1 = -1, sx = 0, sy = 0, sw = 0;
  for (let y = 0; y < img.h; ++y) for (let x = 0; x < img.w; ++x) { const [r, g, b] = img.px (x, y); const l = (r + g + b) / 3 - bg;
    if (l > thr) { x0 = Math.min (x0, x); x1 = Math.max (x1, x); y0 = Math.min (y0, y); y1 = Math.max (y1, y); sx += l * (x + 0.5); sy += l * (y + 0.5); sw += l; } }
  if (! sw) return null;
  return { bg: +bg.toFixed (1), x0, x1: x1 + 1, y0, y1: y1 + 1, cx: sx / sw, cy: sy / sw, weight: sw };
}
module.exports = { decode, ink };
