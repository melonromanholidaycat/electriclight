// Source -> { params, statements(AST) }. Statement-per-line, with a line
// continued automatically while its brackets are unbalanced.

import { tokenize, CompileError } from './tokenize.js';

const NUM = '(-?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?)';
const PARAM_RE = new RegExp(
  `^param\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+${NUM}\\s*\\.\\.\\s*${NUM}\\s*=\\s*${NUM}` +
  `\\s*(?:"([^"]*)")?\\s*(?:step:${NUM})?\\s*$`
);

function splitStatements(src) {
  const raw = src.split(/\r?\n/);
  const out = [];
  let buf = null;
  let depth = 0;
  let startLine = 0;

  for (let i = 0; i < raw.length; i++) {
    const stripped = raw[i].replace(/#.*$/, '');
    if (buf === null) {
      if (!stripped.trim()) continue;
      buf = stripped;
      startLine = i + 1;
      depth = 0;
    } else {
      buf += ' ' + stripped;
    }
    for (const ch of stripped) {
      if (ch === '(') depth++;
      else if (ch === ')') depth--;
    }
    if (depth <= 0) {
      out.push({ text: buf.trim(), line: startLine });
      buf = null;
    }
  }
  if (buf !== null) throw new CompileError('unbalanced parentheses', startLine);
  return out;
}

// --- expression parser (precedence climbing) ---------------------------------

const BINARY = [
  ['||'], ['&&'], ['==', '!='], ['<', '<=', '>', '>='], ['+', '-'], ['*', '/', '%'],
];

class Parser {
  constructor(tokens) { this.toks = tokens; this.i = 0; }
  peek() { return this.toks[this.i]; }
  next() { return this.toks[this.i++]; }
  at(v) { const t = this.peek(); return t.t === 'punct' && t.v === v; }
  eat(v) { if (this.at(v)) { this.i++; return true; } return false; }
  expect(v) {
    if (!this.eat(v)) throw new CompileError(`expected '${v}'`, this.peek().line);
  }

  parseExpr() { return this.parseTernary(); }

  parseTernary() {
    const cond = this.parseBinary(0);
    if (!this.eat('?')) return cond;
    const a = this.parseTernary();
    this.expect(':');
    const b = this.parseTernary();
    return { k: 'select', cond, a, b };
  }

  parseBinary(level) {
    if (level >= BINARY.length) return this.parseUnary();
    let left = this.parseBinary(level + 1);
    for (;;) {
      const t = this.peek();
      if (t.t !== 'punct' || !BINARY[level].includes(t.v)) return left;
      this.next();
      const right = this.parseBinary(level + 1);
      left = { k: 'bin', op: t.v, left, right };
    }
  }

  parseUnary() {
    if (this.eat('-')) return { k: 'neg', a: this.parseUnary() };
    if (this.eat('!')) return { k: 'not', a: this.parseUnary() };
    if (this.eat('+')) return this.parseUnary();
    return this.parsePrimary();
  }

  parsePrimary() {
    const t = this.next();
    if (t.t === 'num') return { k: 'num', v: t.v };
    if (t.t === 'punct' && t.v === '(') {
      const e = this.parseExpr();
      this.expect(')');
      return e;
    }
    if (t.t === 'id') {
      if (this.at('(')) {
        this.next();
        const args = [];
        if (!this.at(')')) {
          do { args.push(this.parseExpr()); } while (this.eat(','));
        }
        this.expect(')');
        return { k: 'call', name: t.v, args, line: t.line };
      }
      return { k: 'ref', name: t.v, line: t.line };
    }
    throw new CompileError('unexpected end of expression', t.line);
  }
}

export function parseExpression(text, line) {
  const p = new Parser(tokenize(text, line));
  const e = p.parseExpr();
  if (p.peek().t !== 'eof') {
    throw new CompileError(`unexpected '${p.peek().v}'`, line);
  }
  return e;
}

export function parse(src) {
  const params = [];
  const statements = [];
  const seen = new Set();

  for (const { text, line } of splitStatements(src)) {
    if (/^param\b/.test(text)) {
      const m = PARAM_RE.exec(text);
      if (!m) {
        throw new CompileError(
          'bad param declaration, expected: param name min..max = default "Label" [step:n]',
          line
        );
      }
      const [, name, min, max, def, label, step] = m;
      if (seen.has(name)) throw new CompileError(`duplicate name '${name}'`, line);
      seen.add(name);
      const lo = parseFloat(min), hi = parseFloat(max), dv = parseFloat(def);
      if (!(hi > lo)) throw new CompileError(`param '${name}': max must exceed min`, line);
      if (dv < lo || dv > hi) throw new CompileError(`param '${name}': default outside range`, line);
      params.push({
        name, min: lo, max: hi, def: dv,
        label: label || name,
        step: step ? parseFloat(step) : +((hi - lo) / 100).toPrecision(2),
        line,
      });
      continue;
    }

    const eq = findAssign(text);
    if (eq < 0) throw new CompileError('expected an assignment, "name = expression"', line);
    const name = text.slice(0, eq).trim();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) {
      throw new CompileError(`'${name}' is not a valid name`, line);
    }
    statements.push({ name, expr: parseExpression(text.slice(eq + 1), line), line });
  }

  return { params, statements };
}

// Index of the assignment '=', skipping ==, <=, >=, !=.
function findAssign(text) {
  for (let i = 0; i < text.length; i++) {
    if (text[i] !== '=') continue;
    if (text[i + 1] === '=') { i++; continue; }
    if ('<>!='.includes(text[i - 1])) continue;
    return i;
  }
  return -1;
}

export { CompileError };
