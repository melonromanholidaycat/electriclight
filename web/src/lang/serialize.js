// Wire format for compiled effects. The firmware decodes this and nothing else;
// source text travels beside it as an opaque blob for the editor's benefit.
//
//   magic  "ELFX"        4 bytes
//   u8     version
//   u8     nLocals
//   u8     stack
//   u8     nParams
//   u16LE  nConsts
//   u16LE  codeLen
//   f32LE  consts[nConsts]
//   u8     code[codeLen]

import { FORMAT_VERSION } from './ops.js';

// Written and read one byte at a time on purpose. Setting a u32 and calling it
// "ELFX little-endian" is how this field spent its first week actually spelling
// "EFLX": the byte order of a character constant is not something either side
// of the wire should have to reason about.
const MAGIC = [0x45, 0x4c, 0x46, 0x58]; // "ELFX"

export function encodeProgram(p) {
  const size = 12 + p.consts.length * 4 + p.code.length;
  const buf = new ArrayBuffer(size);
  const dv = new DataView(buf);
  for (let i = 0; i < 4; i++) dv.setUint8(i, MAGIC[i]);
  dv.setUint8(4, p.version);
  dv.setUint8(5, p.nLocals);
  dv.setUint8(6, p.stack);
  dv.setUint8(7, p.params.length);
  dv.setUint16(8, p.consts.length, true);
  dv.setUint16(10, p.code.length, true);
  let o = 12;
  for (const c of p.consts) { dv.setFloat32(o, c, true); o += 4; }
  new Uint8Array(buf, o).set(p.code);
  return new Uint8Array(buf);
}

export function decodeProgram(bytes, params = []) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < 4; i++) {
    if (dv.getUint8(i) !== MAGIC[i]) throw new Error('not an effect program');
  }
  const version = dv.getUint8(4);
  if (version !== FORMAT_VERSION) throw new Error(`effect format v${version}, expected v${FORMAT_VERSION}`);
  const nLocals = dv.getUint8(5);
  const stack = dv.getUint8(6);
  const nParams = dv.getUint8(7);
  const nConsts = dv.getUint16(8, true);
  const codeLen = dv.getUint16(10, true);
  const consts = new Float32Array(nConsts);
  let o = 12;
  for (let i = 0; i < nConsts; i++) { consts[i] = dv.getFloat32(o, true); o += 4; }
  const code = bytes.slice(o, o + codeLen);
  return { version, nLocals, stack, nParams, consts, code, params };
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

export function toBase64(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2];
    out += B64[a >> 2];
    out += B64[((a & 3) << 4) | ((b || 0) >> 4)];
    out += i + 1 < bytes.length ? B64[((b & 15) << 2) | ((c || 0) >> 6)] : '=';
    out += i + 2 < bytes.length ? B64[c & 63] : '=';
  }
  return out;
}

export function fromBase64(str) {
  const clean = str.replace(/=+$/, '');
  const out = new Uint8Array((clean.length * 3) >> 2);
  let acc = 0, bits = 0, o = 0;
  for (const ch of clean) {
    const v = B64.indexOf(ch);
    if (v < 0) continue;
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8) { bits -= 8; out[o++] = (acc >> bits) & 255; }
  }
  return out.subarray(0, o);
}
