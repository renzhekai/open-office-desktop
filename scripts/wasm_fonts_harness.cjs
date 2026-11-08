'use strict';
// Functional probe for one SDKJS fonts engine build (fonts.js + fonts.wasm, or fonts_ie.js).
// Loads the engine in a Node vm context that imitates a web page, then drives the raw
// exports (FreeType, HarfBuzz, zip, raster, hyphen) with fixed inputs and prints JSON.
// The only change to the engine text is one test hook that exposes the internal Module
// object; it is applied identically to official and rebuilt files and asserted to match once.
//
// Usage: node scripts/wasm_fonts_harness.cjs <engine.js> <inputs.json> <output.json>

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');

const [, , enginePath, inputsPath, outputPath] = process.argv;
const inputs = JSON.parse(fs.readFileSync(inputsPath, 'utf8'));
const engineDir = path.dirname(enginePath);

const HOOK_ANCHOR = 'var AscFonts=window["AscFonts"];';
let source = fs.readFileSync(enginePath, 'utf8');
const anchorCount = source.split(HOOK_ANCHOR).length - 1;
if (anchorCount !== 1) {
  throw new Error(`expected one hook anchor, found ${anchorCount}`);
}
source = source.replace(HOOK_ANCHOR, HOOK_ANCHOR + 'window.__harnessModule=function(){return Module};');

const sha = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');
const short = (value) => sha(Buffer.from(JSON.stringify(value))).slice(0, 16);

async function load() {
  let loads = 0;
  let ready;
  const readyPromise = new Promise((resolve) => { ready = resolve; });
  const sandbox = {
    console, setTimeout, clearTimeout, setInterval, clearInterval, TextEncoder, TextDecoder,
    navigator: { userAgent: 'open-office-wasm-harness' },
    location: { href: 'http://localhost/engine/', protocol: 'http:' },
    document: { currentScript: { src: 'http://localhost/engine/' + path.basename(enginePath) } },
    fetch: async (url) => {
      const name = String(url).split('/').pop();
      const bytes = fs.readFileSync(path.join(engineDir, name));
      return { ok: true, status: 200, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) };
    },
    XMLHttpRequest: function () { throw new Error('XMLHttpRequest is not expected in web mode'); },
    AscFonts: { onLoadModule() { loads += 1; if (loads >= 2) ready(); } },
  };
  sandbox.window = sandbox;
  sandbox.self = sandbox;
  vm.createContext(sandbox);
  vm.runInContext('delete WebAssembly.instantiateStreaming;', sandbox);
  vm.runInContext(source, sandbox, { filename: path.basename(enginePath) });
  const timeout = new Promise((_, reject) => setTimeout(() => reject(new Error(`module not ready, onLoadModule calls: ${loads}`)), 120000));
  await Promise.race([readyPromise, timeout]);
  return { Module: sandbox.__harnessModule(), loads };
}

function run(M) {
  const heap = () => M.HEAPU8;
  const view = () => new DataView(M.HEAPU8.buffer);
  const alloc = (bytes) => { const p = M._malloc(bytes.length); heap().set(bytes, p); return p; };
  const cstr = (text) => alloc(Buffer.from(text + '\0', 'utf8'));
  const ints = (p, n) => { const v = view(); const out = []; for (let i = 0; i < n; i++) out.push(v.getInt32(p + 4 * i, true)); return out; };
  const bytesAt = (p, n) => Buffer.from(heap().subarray(p, p + n));
  const lenPrefixed = (p) => bytesAt(p + 4, view().getInt32(p, true));

  const result = { freetype: [], harfbuzz: [], zlib: {}, raster: [], hyphen: [] };
  const lib = M._ASC_FT_Init();

  function openFace(file, index) {
    const data = fs.readFileSync(file);
    const mem = M._ASC_FT_Malloc(data.length);
    heap().set(data, mem);
    return { face: M._ASC_FT_Open_Face(lib, mem, data.length, index), mem };
  }

  function faceInfo(face) {
    const p = M._ASC_FT_GetFaceInfo(face);
    if (!p) return null;
    const v = view();
    let i = 0;
    const read = () => v.getInt32(p + 4 * i++, true);
    const fixed = []; for (let k = 0; k < 10; k++) fixed.push(read());
    const name = () => { const cps = []; for (let c = read(); c !== 0; c = read()) cps.push(c); return String.fromCodePoint(...cps); };
    const family = name();
    const style = name();
    const tail = []; for (let k = 0; k < 19; k++) tail.push(read());
    M._ASC_FT_Free(p);
    return { fixed, family, style, tail };
  }

  for (const item of inputs.fonts) {
    for (const index of item.faces) {
      const { face, mem } = openFace(item.path, index);
      const entry = { font: path.basename(item.path), face_index: index, opened: face !== 0 };
      if (!face) { result.freetype.push(entry); continue; }
      entry.info = faceInfo(face);
      entry.set_char_size = M._ASC_FT_Set_Char_Size(face, 16 * 64, 16 * 64, 96, 96);
      const cmap = inputs.codepoints.map((cp) => M._ASC_FT_SetCMapForCharCode(face, cp));
      entry.cmap = cmap;
      entry.kerning = cmap.slice(1).map((gid, k) => M._ASC_FT_GetKerningX(face, cmap[k], gid));
      entry.max_advance = M._ASC_FT_GetFaceMaxAdvanceX(face);
      const numGlyphs = entry.info.fixed[6];
      const gids = new Set(cmap.filter((g) => g > 0));
      for (let g = 0; g < Math.min(numGlyphs, inputs.glyph_scan); g++) gids.add(g);
      const glyphs = [];
      for (const flags of inputs.load_flags) {
        for (const gid of [...gids].sort((a, b) => a - b)) {
          const record = { gid, flags, load: M._ASC_FT_Load_Glyph(face, gid, flags) };
          if (record.load === 0) {
            const vec = M._ASC_FT_Get_Glyph_Measure_Params(face, 1);
            record.vector = vec ? short(ints(vec + 4, ints(vec, 1)[0] - 1)) : null;
            if (vec) M._ASC_FT_Free(vec);
            const meas = M._ASC_FT_Get_Glyph_Measure_Params(face, 0);
            record.measure = meas ? ints(meas, ints(meas, 1)[0]) : null;
            if (meas) M._ASC_FT_Free(meas);
            const params = M._ASC_FT_Get_Glyph_Render_Params(face, 0);
            if (params) {
              record.render = ints(params, 6);
              const size = Math.abs(record.render[4]) * record.render[3];
              record.bitmap = size ? sha(bytesAt(M._ASC_FT_Get_Glyph_Render_Buffer(face), size)).slice(0, 16) : 'empty';
              M._ASC_FT_Free(params);
            }
          }
          glyphs.push(record);
        }
      }
      entry.glyph_count = glyphs.length;
      entry.glyph_digests = glyphs.map((g) => `${g.flags}:${g.gid}:${short(g)}`);
      entry.digest = short(glyphs);
      entry.sample = glyphs.filter((g) => cmap.includes(g.gid)).slice(0, 4);
      result.freetype.push(entry);

      for (const shape of inputs.shaping.filter((s) => s.font === path.basename(item.path) && index === 0)) {
        const lang = M._ASC_HB_LanguageFromString(cstr(shape.language));
        const text = cstr(shape.text);
        const out = M._ASC_HB_ShapeText(face, 0, text, shape.features, shape.script, shape.direction, lang);
        const record = { font: shape.font, text: shape.text, features: shape.features };
        if (out) {
          const v = view();
          const size = v.getInt32(out, true);
          const hbFont = v.getUint32(out + 4, true);
          const glyphRecords = [];
          for (let p = out + 12; p + 26 <= out + size; p += 26) {
            glyphRecords.push([v.getUint8(p), v.getUint8(p + 1), v.getUint32(p + 2, true), v.getUint32(p + 6, true),
              v.getInt32(p + 10, true), v.getInt32(p + 14, true), v.getInt32(p + 18, true), v.getInt32(p + 22, true)]);
          }
          record.glyphs = glyphRecords;
          M._ASC_FT_Free(out);
          if (hbFont) M._ASC_HB_FontFree(hbFont);
        } else {
          record.glyphs = null;
        }
        M._free(text);
        result.harfbuzz.push(record);
      }
      M._ASC_FT_Done_Face(face);
    }
  }
  M._ASC_FT_Done_FreeType(lib);

  // zip: create an archive, read it back, and parse a fixed OOXML package
  const created = M._Zlib_Create();
  for (const file of inputs.zip_entries) {
    const data = Buffer.from(file.base64, 'base64');
    const p = M._Zlib_Malloc(data.length); heap().set(data, p);
    const name = cstr(file.path);
    M._Zlib_AddFile(created, name, p, data.length);
    M._free(name);
  }
  const saved = M._Zlib_Save(created);
  const zipBytes = saved ? lenPrefixed(saved) : Buffer.alloc(0);
  fs.writeFileSync(inputs.zip_output, zipBytes);
  result.zlib.created_bytes = zipBytes.length;
  M._Zlib_Close(created);

  function readZip(bytes) {
    const p = M._Zlib_Malloc(bytes.length); heap().set(bytes, p);
    const archive = M._Zlib_Open(p, bytes.length);
    if (!archive) return null;
    // Unlike GetFile/Save, the GetPaths length header counts itself (CData SkipLen/WriteLen).
    const pathsPointer = M._Zlib_GetPaths(archive);
    const pathsBuffer = bytesAt(pathsPointer + 4, view().getInt32(pathsPointer, true) - 4);
    const names = [];
    for (let off = 0; off < pathsBuffer.length;) {
      const len = pathsBuffer.readInt32LE(off); off += 4;
      names.push(pathsBuffer.subarray(off, off + len).toString('utf8')); off += len;
    }
    const files = {};
    for (const name of names) {
      const np = cstr(name);
      const fp = M._Zlib_GetFile(archive, np);
      files[name] = fp ? sha(lenPrefixed(fp)) : null;
      M._free(np);
    }
    M._Zlib_Close(archive);
    return files;
  }
  result.zlib.reread_created = readZip(zipBytes);
  result.zlib.fixture = readZip(fs.readFileSync(inputs.zip_fixture));

  // raster: format detection, decode (BGRA and RGBA), re-encode
  for (const file of inputs.images) {
    const data = fs.readFileSync(file);
    const p = alloc(data);
    const entry = { image: path.basename(file), format: M._Image_GetFormat(p, data.length) };
    for (const rgba of [0, 1]) {
      const frame = M._Raster_DecodeFile(p, data.length, rgba);
      if (!frame) { entry[`decode_${rgba}`] = null; continue; }
      const w = M._Raster_GetWidth(frame); const h = M._Raster_GetHeight(frame); const stride = M._Raster_GetStride(frame);
      entry[`decode_${rgba}`] = { w, h, stride, pixels: sha(bytesAt(M._Raster_GetDecodedBuffer(frame), Math.abs(stride) * h)) };
      M._Raster_Destroy(frame);
    }
    for (const format of inputs.encode_formats) {
      const enc = M._Raster_Encode(p, data.length, format);
      if (!enc) { entry[`encode_${format}`] = null; continue; }
      const size = M._Raster_GetEncodedSize(enc);
      entry[`encode_${format}`] = { size, sha256: sha(bytesAt(M._Raster_GetEncodedBuffer(enc), size)) };
      M._Raster_DestroyEncodedData(enc);
    }
    M._free(p);
    result.raster.push(entry);
  }

  // hyphenation
  const app = M._hyphenCreateApplication();
  for (const dict of inputs.hyphen) {
    const data = fs.readFileSync(dict.path);
    const p = alloc(data);
    const entry = { dict: path.basename(dict.path), lcid: dict.lcid, load: M._hyphenLoadDictionary(app, dict.lcid, p, data.length), words: {} };
    M._free(p);
    for (const word of dict.words) {
      const bytes = Buffer.from(word, 'utf8');
      const wp = alloc(Buffer.concat([bytes, Buffer.alloc(1)]));
      const vp = M._hyphenWord(app, dict.lcid, wp, bytes.length);
      const marks = [];
      for (let k = 0; vp && heap()[vp + k] !== 0 && k < bytes.length + 5; k++) if (heap()[vp + k] & 1) marks.push(k + 1);
      entry.words[word] = marks;
      M._free(wp);
    }
    result.hyphen.push(entry);
  }
  M._hyphenDestroyApplication(app);
  return result;
}

load().then(({ Module, loads }) => {
  const result = run(Module);
  result.engine = path.basename(enginePath);
  result.on_load_module_calls = loads;
  fs.writeFileSync(outputPath, JSON.stringify(result));
  console.log(`ok ${outputPath}`);
}).catch((err) => { console.error(err && err.stack || err); process.exit(1); });
