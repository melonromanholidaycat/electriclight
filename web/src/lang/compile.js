// AST -> bytecode. Runs only in the browser; the firmware executes the result.

import { parse, CompileError } from './parse.js';
import { OP, VAR_INDEX, FUNC_INDEX, FUNCS, CONSTANTS, OUT, OUT_NAMES, FORMAT_VERSION } from './ops.js';

export const LIMITS = { locals: 64, consts: 256, params: 32, stack: 32, code: 4096 };

class Emitter {
  constructor() {
    this.code = [];
    this.consts = [];
    this.depth = 0;
    this.maxDepth = 0;
  }
  push(n = 1) { this.depth += n; if (this.depth > this.maxDepth) this.maxDepth = this.depth; }
  pop(n = 1) { this.depth -= n; }
  op(o, arg) {
    this.code.push(o);
    if (arg !== undefined) this.code.push(arg);
  }
  constant(v) {
    const f = Math.fround(v);
    let i = this.consts.indexOf(f);
    if (i < 0) {
      if (this.consts.length >= LIMITS.consts) throw new CompileError('too many constants');
      i = this.consts.push(f) - 1;
    }
    this.op(OP.CONST, i);
    this.push();
  }
}

const BIN_OP = {
  '+': OP.ADD, '-': OP.SUB, '*': OP.MUL, '/': OP.DIV, '%': OP.MOD,
  '<': OP.LT, '<=': OP.LE, '>': OP.GT, '>=': OP.GE, '==': OP.EQ, '!=': OP.NE,
  '&&': OP.AND, '||': OP.OR,
};

export function compile(src) {
  const { params, statements } = parse(src);
  if (params.length > LIMITS.params) throw new CompileError('too many params');

  for (const p of params) {
    if (p.name in CONSTANTS || p.name in VAR_INDEX || p.name in FUNC_INDEX) {
      throw new CompileError(`'${p.name}' is a built-in name`, p.line);
    }
    if (OUT_NAMES.includes(p.name)) {
      throw new CompileError(`'${p.name}' is an output name`, p.line);
    }
  }

  const paramIndex = Object.fromEntries(params.map((p, i) => [p.name, i]));
  const locals = new Map(OUT_NAMES.map((n, i) => [n, i]));
  const e = new Emitter();

  // h, s, v start at black-with-full-saturation so a partial effect still runs.
  for (const [name, dflt] of [['h', 0], ['s', 1], ['v', 0]]) {
    e.constant(dflt);
    e.op(OP.STORE, OUT[name]);
    e.pop();
  }

  const emit = (node) => {
    switch (node.k) {
      case 'num':
        e.constant(node.v);
        return;
      case 'ref': {
        const n = node.name;
        if (n in CONSTANTS) return e.constant(CONSTANTS[n]);
        if (locals.has(n)) { e.op(OP.LOAD, locals.get(n)); return e.push(); }
        if (n in paramIndex) { e.op(OP.PARAM, paramIndex[n]); return e.push(); }
        if (n in VAR_INDEX) { e.op(OP.VAR, VAR_INDEX[n]); return e.push(); }
        throw new CompileError(`unknown name '${n}'`, node.line);
      }
      case 'bin':
        emit(node.left); emit(node.right);
        e.op(BIN_OP[node.op]); e.pop();
        return;
      case 'neg': emit(node.a); e.op(OP.NEG); return;
      case 'not': emit(node.a); e.op(OP.NOT); return;
      case 'select':
        emit(node.cond); emit(node.a); emit(node.b);
        e.op(OP.SELECT); e.pop(2);
        return;
      case 'call': {
        const idx = FUNC_INDEX[node.name];
        if (idx === undefined) throw new CompileError(`unknown function '${node.name}'`, node.line);
        const want = FUNCS[idx].arity;
        if (node.args.length !== want) {
          throw new CompileError(
            `${node.name}() takes ${want} argument${want === 1 ? '' : 's'}, got ${node.args.length}`,
            node.line
          );
        }
        for (const a of node.args) emit(a);
        e.op(OP.CALL, idx);
        e.pop(want - 1);
        return;
      }
      default:
        throw new CompileError(`internal: bad node ${node.k}`);
    }
  };

  for (const st of statements) {
    if (st.name in CONSTANTS || st.name in VAR_INDEX || st.name in FUNC_INDEX) {
      throw new CompileError(`'${st.name}' is a built-in name`, st.line);
    }
    if (st.name in paramIndex) throw new CompileError(`'${st.name}' is already a param`, st.line);
    emit(st.expr);
    if (!locals.has(st.name)) {
      if (locals.size >= LIMITS.locals) throw new CompileError('too many locals', st.line);
      locals.set(st.name, locals.size);
    }
    e.op(OP.STORE, locals.get(st.name));
    e.pop();
  }

  e.op(OP.END);

  if (e.code.length > LIMITS.code) throw new CompileError('effect too long');
  if (e.maxDepth > LIMITS.stack) throw new CompileError('expression nests too deeply');

  return {
    version: FORMAT_VERSION,
    code: Uint8Array.from(e.code),
    consts: Float32Array.from(e.consts),
    nLocals: locals.size,
    stack: e.maxDepth,
    params,
    usesPrev: /(^|[^A-Za-z0-9_])prev([^A-Za-z0-9_]|$)/.test(src.replace(/#.*$/gm, '')),
  };
}

export { CompileError };
