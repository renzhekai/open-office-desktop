'use strict';
// Functional probe for one SDKJS engine build of the zlib, spell or hash module.
// Loads the engine in a Node vm context that imitates a web page (zlib) or a Web Worker
// (spell, hash), waits for the template's own ready flag, then drives the raw exports with
// fixed inputs and writes JSON. The only change to the engine text is one test hook right
// after `var Module=...` that exposes the internal Module object; it is applied identically
// to official and rebuilt files and must match exactly once.
//
// Usage: node scripts/wasm_engine_harness.cjs <zlib|spell|hash> <engine.js> <inputs.json> <output.json>

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');

const [, , moduleName, enginePath, inputsPath, outputPath] = process.argv;
const inputs = JSON.parse(fs.readFileSync(inputsPath, 'utf8'));
const engineDir = path.dirname(enginePath);

const PROFILES = {
  zlib: { env: 'web', ready: (sb) => !!(sb.AscCommon && sb.AscCommon.CZLibEngineJS && sb.AscCommon.CZLibEngineJS.prototype.isModuleInit) },
  spell: { env: 'worker', ready: (sb) => sb.engineInit === true },
  hash: { env: 'worker', ready: (sb) => sb.engineInit === true },
  // The PDF loader template attaches its API to an existing window.AscViewer and calls
  // AscViewer.onLoadModule from __ATPOSTRUN__.
  drawingfile: {
    env: 'web',
    setup: (sb) => { sb.AscViewer = { onLoadModule() { sb.__pdfReady = true; } }; },
    ready: (sb) => sb.__pdfReady === true,
  },
};
const profile = PROFILES[moduleName];
if (!profile) throw new Error(`unknown module ${moduleName}`);

const HOOK = /var Module\s*=\s*typeof Module\s*!==?\s*["']undefined["']\s*\?\s*Module\s*:\s*\{\}\s*;/g;
let source = fs.readFileSync(enginePath, 'utf8');
const hookCount = (source.match(HOOK) || []).length;
if (hookCount !== 1) throw new Error(`expected one Module declaration, found ${hookCount}`);
source = source.replace(HOOK, (m) => m + 'self.__harnessGetModule=function(){return Module};');

const sha = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

async function load() {
  const url = 'http://localhost/engine/' + path.basename(enginePath);
  const sandbox = {
    console, setTimeout, clearTimeout, setInterval, clearInterval, TextEncoder, TextDecoder,
    navigator: { userAgent: 'open-office-wasm-harness' },
    location: { href: url, protocol: 'http:' },
    fetch: async (target) => {
      const name = String(target).split('/').pop();
      const bytes = fs.readFileSync(path.join(engineDir, name));
      return { ok: true, status: 200, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) };
    },
    // Older asm.js glue (e.g. the 2021 spell_ie.js) loads its .mem file with XHR; serve it from the engine dir.
    XMLHttpRequest: class {
      open(method, target) { this.target = String(target); }
      overrideMimeType() {}
      setRequestHeader() {}
      send() {
        const name = this.target.split('/').pop();
        const bytes = fs.readFileSync(path.join(engineDir, name));
        this.status = 200;
        this.response = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length);
        setTimeout(() => this.onload && this.onload(), 0);
      }
    },
  };
  sandbox.self = sandbox;
  if (profile.env === 'web') {
    sandbox.window = sandbox;
    sandbox.document = { currentScript: { src: url } };
  } else {
    sandbox.importScripts = () => {};
    sandbox.postMessage = () => {};
  }
  if (profile.setup) profile.setup(sandbox);
  vm.createContext(sandbox);
  vm.runInContext('delete WebAssembly.instantiateStreaming;', sandbox);
  vm.runInContext(source, sandbox, { filename: path.basename(enginePath) });
  const started = Date.now();
  while (!profile.ready(sandbox)) {
    if (Date.now() - started > 120000) throw new Error('engine not ready after 120 s');
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  return sandbox.__harnessGetModule();
}

function helpers(M) {
  const heap = () => M.HEAPU8;
  const view = () => new DataView(M.HEAPU8.buffer);
  const alloc = (bytes) => { const p = M._malloc(Math.max(bytes.length, 1)); heap().set(bytes, p); return p; };
  const cstr = (text) => alloc(Buffer.from(text + '\0', 'utf8'));
  const bytesAt = (p, n) => Buffer.from(heap().subarray(p, p + n));
  const lenPrefixed = (p) => bytesAt(p + 4, view().getInt32(p, true));
  // Lists serialized with CData SkipLen/WriteLen: the total length counts its own 4-byte header.
  const readList = (p) => {
    const buf = bytesAt(p + 4, view().getInt32(p, true) - 4);
    const out = [];
    for (let off = 0; off < buf.length;) {
      const len = buf.readInt32LE(off); off += 4;
      out.push(buf.subarray(off, off + len).toString('utf8')); off += len;
    }
    return out;
  };
  return { heap, view, alloc, cstr, bytesAt, lenPrefixed, readList };
}

// A C++ throw escapes the module as a JS exception (a bare pointer number when exception catching
// is disabled). Record only that it happened; the pointer value is build-specific.
const describeThrow = (err) => (typeof err === 'number' ? 'cxx_exception_pointer' : String((err && err.message) || err));

async function runZlib(M, reload) {
  let h = helpers(M);
  const result = { zip: {}, raster: [] };
  const created = M._Zlib_Create();
  for (const file of inputs.zip_entries) {
    const data = Buffer.from(file.base64, 'base64');
    const p = M._Zlib_Malloc(Math.max(data.length, 1)); h.heap().set(data, p);
    const name = h.cstr(file.path);
    M._Zlib_AddFile(created, name, p, data.length);
    M._free(name);
  }
  const saved = M._Zlib_Save(created);
  const zipBytes = saved ? h.lenPrefixed(saved) : Buffer.alloc(0);
  fs.writeFileSync(inputs.zip_output, zipBytes);
  result.zip.created_bytes = zipBytes.length;
  M._Zlib_Close(created);

  const readZip = (bytes) => {
    const p = M._Zlib_Malloc(bytes.length); h.heap().set(bytes, p);
    const archive = M._Zlib_Open(p, bytes.length);
    if (!archive) return null;
    const files = {};
    for (const name of h.readList(M._Zlib_GetPaths(archive))) {
      const np = h.cstr(name);
      const fp = M._Zlib_GetFile(archive, np);
      files[name] = fp ? sha(h.lenPrefixed(fp)) : null;
      M._free(np);
    }
    M._Zlib_Close(archive);
    return files;
  };
  result.zip.reread_created = readZip(zipBytes);
  result.zip.fixtures = inputs.zip_fixtures.map((f) => ({ file: path.basename(f), files: readZip(fs.readFileSync(f)) }));

  result.reloads_after_throw = 0;
  for (const file of inputs.images) {
    const data = fs.readFileSync(file);
    const entry = { image: path.basename(file) };
    const steps = [['format', () => M._Image_GetFormat(h.alloc(data), data.length)]];
    for (const rgba of [0, 1]) {
      steps.push([`decode_${rgba}`, () => {
        const p = h.alloc(data);
        const frame = M._Raster_DecodeFile(p, data.length, rgba);
        if (!frame) return null;
        const w = M._Raster_GetWidth(frame); const ht = M._Raster_GetHeight(frame); const stride = M._Raster_GetStride(frame);
        const out = { w, h: ht, stride, pixels: sha(h.bytesAt(M._Raster_GetDecodedBuffer(frame), Math.abs(stride) * ht)) };
        M._Raster_Destroy(frame); M._free(p);
        return out;
      }]);
    }
    for (const format of inputs.encode_formats) {
      steps.push([`encode_${format}`, () => {
        const p = h.alloc(data);
        const enc = M._Raster_Encode(p, data.length, format);
        if (!enc) return null;
        const size = M._Raster_GetEncodedSize(enc);
        const bytes = h.bytesAt(M._Raster_GetEncodedBuffer(enc), size);
        if (format === 24 && inputs.svg_dir) fs.writeFileSync(path.join(inputs.svg_dir, path.basename(file) + '.svg'), bytes);
        M._Raster_DestroyEncodedData(enc); M._free(p);
        return { size, sha256: sha(bytes) };
      }]);
    }
    for (const [key, step] of steps) {
      try {
        entry[key] = step();
      } catch (err) {
        entry[key] = { threw: describeThrow(err) }; if (process.env.HARNESS_STACK) console.error(err && err.stack);
        M = await reload();  // fresh instance so later steps do not inherit a broken state
        h = helpers(M);
        result.reloads_after_throw += 1;
      }
    }
    result.raster.push(entry);
  }
  return result;
}

function runSpell(M) {
  const h = helpers(M);
  const result = { dictionaries: [] };
  const engine = M._Spellchecker_Create(3);
  for (const dict of inputs.dictionaries) {
    const entry = { lang: dict.lang, words: {} };
    for (const [suffix, file] of [['.aff', dict.aff], ['.dic', dict.dic]]) {
      const data = fs.readFileSync(file);
      const p = M._Spellchecker_Malloc(data.length); h.heap().set(data, p);
      const sid = h.cstr(dict.lang + suffix);
      entry['add' + suffix] = M._Spellchecker_AddDictionary(engine, sid, p, data.length);
      M._free(sid);
    }
    const aff = h.cstr(dict.lang + '.aff'); const dic = h.cstr(dict.lang + '.dic');
    entry.load = M._Spellchecker_Load(engine, aff, dic);
    M._free(aff); M._free(dic);
    for (const word of dict.words) {
      const wp = h.cstr(word);
      const correct = M._Spellchecker_Spell(engine, wp);
      const sp = correct === 1 ? 0 : M._Spellchecker_Suggest(engine, wp);
      entry.words[word] = { spell: correct, suggestions: sp ? h.readList(sp) : [] };
      M._free(wp);
    }
    result.dictionaries.push(entry);
  }
  return result;
}

function runHash(M) {
  const h = helpers(M);
  const sizes = [16, 16, 16, 20, 20, 32, 48, 64, 64];
  const result = { hash: [], hash2: [] };
  for (const item of inputs.hash_inputs) {
    const data = Buffer.from(item.base64, 'base64');
    const p = h.alloc(data);
    const digests = {};
    for (let alg = 0; alg < sizes.length; alg++) {
      const out = M._hash(p, data.length, alg);
      digests[alg] = out ? h.bytesAt(out, sizes[alg]).toString('hex') : null;
      if (out) M._free(out);
    }
    M._free(p);
    result.hash.push({ name: item.name, digests });
  }
  for (const item of inputs.hash2_inputs) {
    const pw = h.cstr(item.password); const salt = h.cstr(item.salt_base64);
    const out = M._hash2(pw, salt, item.spin, item.alg);
    result.hash2.push({ ...item, digest: out ? h.bytesAt(out, sizes[item.alg]).toString('hex') : null });
    if (out) M._free(out);
    M._free(pw); M._free(salt);
  }
  return result;
}

async function runPdf(M, reload) {
  let h = helpers(M);
  const result = { documents: [], storages: {}, reloads_after_throw: 0 };
  // CWasmPointer.getMemory in the loader: total length (including its 4-byte header) then data.
  const readMem = (ptr) => {
    if (!ptr) return null;
    const len = h.view().getInt32(ptr, true);
    if (len <= 4) return { len };
    return h.bytesAt(ptr + 4, len - 4);
  };
  const digest = (b) => (b && Buffer.isBuffer(b) ? { bytes: b.length, sha256: sha(b) } : b);
  const int32s = (values) => {
    const ptr = M._malloc(Math.max(values.length, 1) * 4);
    values.forEach((v, i) => h.view().setInt32(ptr + 4 * i, v, true));
    return ptr;
  };
  const openBytes = (bytes) => {
    const ptr = M._malloc(bytes.length); h.heap().set(bytes, ptr);
    return M._Open(ptr, bytes.length, 0);
  };
  // PdfReader::GetAPWidget -> CAnnotAP::ToWASM per widget: refnum, x*100, y*100, width, height, count, then per
  // appearance: name, state, pointer (lo, hi) to a 4*width*height BGRA buffer, blend mode. Pointers are heap
  // addresses that differ between builds, so each is replaced by the digest of the buffer it points to.
  const formsAP = (ptr) => {
    if (!ptr) return null;
    const v = h.view();
    const end = ptr + v.getInt32(ptr, true);
    let at = ptr + 4;
    const int = () => { const x = v.getInt32(at, true); at += 4; return x; };
    const str = () => { const n = int(); const t = h.bytesAt(at, n).toString('utf8'); at += n; return t; };
    const widgets = [];
    while (at < end) {
      const w = { ref: int(), x: int(), y: int(), width: int(), height: int(), views: [] };
      for (let n = int(); n > 0; n--) {
        const view = { name: str(), state: str() };
        const lo = v.getUint32(at, true); const hi = v.getUint32(at + 4, true); at += 8;
        view.pixels = hi === 0 && lo ? sha(h.bytesAt(lo, 4 * w.width * w.height)) : { lo, hi };
        view.blend = v.getUint8(at); at += 1;
        w.views.push(view);
      }
      widgets.push(w);
    }
    return { widgets, parsed_to_end: at === end };
  };
  const pixmap = (file, page, w, ht) => {
    const ptr = M._GetPixmap(file, page, w, ht, 0xFFFFFF);
    return ptr ? sha(h.bytesAt(ptr, 4 * w * ht)) : null;
  };

  for (const doc of inputs.pdfs) {
    const data = fs.readFileSync(doc.path);
    const entry = { pdf: path.basename(doc.path), pages: [] };
    let file = 0;
    const reopen = () => { file = openBytes(data); };
    const step = async (target, key, fn) => {
      try {
        target[key] = fn();
      } catch (err) {
        target[key] = { threw: describeThrow(err) };
        if (process.env.HARNESS_STACK) console.error(err && err.stack);
        M = await reload(); h = helpers(M); result.reloads_after_throw += 1;
        reopen();
      }
    };
    reopen();
    await step(entry, 'open', () => ({ error: M._GetErrorCode(file), type: M._GetType(file) }));
    await step(entry, 'info', () => digest(readMem(M._GetInfo(file))));
    await step(entry, 'structure', () => digest(readMem(M._GetStructure(file))));
    await step(entry, 'forms_info', () => digest(readMem(M._GetInteractiveFormsInfo(file))));
    await step(entry, 'macros', () => digest(readMem(M._GetMacros(file))));
    await step(entry, 'need_cmap', () => M._IsNeedCMap(file));
    await step(entry, 'perm_print', () => M._CheckPerm(file, 4));
    for (let page = 0; page < doc.pages; page++) {
      const p = { page };
      await step(p, 'glyphs', () => digest(readMem(M._GetGlyphs(file, page))));
      await step(p, 'links', () => digest(readMem(M._GetLinks(file, page))));
      await step(p, 'annotations', () => digest(readMem(M._GetAnnotationsInfo(file, page))));
      for (const [w, ht] of inputs.render_sizes) await step(p, `pixmap_${w}x${ht}`, () => pixmap(file, page, w, ht));
      await step(p, 'annotations_ap', () => digest(readMem(M._GetAnnotationsAP(file, 306, 396, 0xFFFFFF, page, -1, -1))));
      await step(p, 'forms_ap', () => formsAP(M._GetInteractiveFormsAP(file, 306, 396, 0xFFFFFF, page, -1, -1, -1)));
      entry.pages.push(p);
    }
    const pages = [...Array(doc.pages).keys()];
    await step(entry, 'scan_page_fonts', () => digest(readMem(M._SetScanPageFonts(file, int32s(pages), pages.length))));
    // Split / build produce new PDFs that may embed dates or IDs: keep their bytes as info only and
    // compare how they reopen and render.
    for (const [key, make] of [
      ['split_first_page', () => readMem(M._SplitPages(file, int32s([0]), 1, 0, 0))],
      ['build_pdf', () => readMem(M._BuildPdf(file, 0, 0, 0, 0, 0))],
    ]) {
      await step(entry, key, () => {
        const out = make();
        if (!out || !Buffer.isBuffer(out)) return { result: out };
        const reopened = openBytes(out);
        const res = { info_bytes: out.length, info_sha256: sha(out), starts_pdf: out.subarray(0, 5).toString('latin1') === '%PDF-',
                      reopen_error: M._GetErrorCode(reopened), reopen_pixmap: pixmap(reopened, 0, 153, 198) };
        M._Close(reopened);
        // Close releases the global storages; reopen the main document for later steps.
        reopen();
        return res;
      });
    }
    await step(entry, 'close', () => { M._Close(file); return 'ok'; });
    result.documents.push(entry);
  }

  // Global storages are created by Open and released by Close.
  const font = fs.readFileSync(inputs.storage_font);
  const image = fs.readFileSync(inputs.storage_image);
  const session = openBytes(fs.readFileSync(inputs.pdfs[0].path));
  const exists = (fn, id) => { const ip = h.cstr(id); const r = fn(ip); M._free(ip); return r; };
  const put = (fn, id, bytes) => { const ip = h.cstr(id); const dp = M._malloc(bytes.length); h.heap().set(bytes, dp); fn(ip, dp, bytes.length); M._free(dp); M._free(ip); };
  const s = result.storages;
  s.font_before = exists(M._IsFontBinaryExist, 'test-font');
  put(M._SetFontBinary, 'test-font', font);
  s.font_after_set = exists(M._IsFontBinaryExist, 'test-font');
  M._ClearFontsStorage();
  s.font_after_clear = exists(M._IsFontBinaryExist, 'test-font');
  s.image_before = exists(M._IsImageBinaryExist, 'test-image');
  put(M._SetImageBinary, 'test-image', image);
  s.image_after_set = exists(M._IsImageBinaryExist, 'test-image');
  M._ClearImagesStorage();
  s.image_after_clear = exists(M._IsImageBinaryExist, 'test-image');
  M._Close(session);
  return result;
}

const RUNNERS = { zlib: runZlib, spell: runSpell, hash: runHash, drawingfile: runPdf };

load().then(async (M) => {
  const result = await RUNNERS[moduleName](M, load);
  result.engine = path.basename(enginePath);
  fs.writeFileSync(outputPath, JSON.stringify(result));
  console.log(`ok ${outputPath}`);
}).catch((err) => { console.error(err && err.stack || err); process.exit(1); });
