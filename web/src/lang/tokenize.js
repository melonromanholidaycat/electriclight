// Tokeniser for the effect language. Lives only in the browser: the firmware
// receives compiled bytecode and never parses source.

export class CompileError extends Error {
  constructor(message, line) {
    super(line != null ? `line ${line}: ${message}` : message);
    this.line = line;
  }
}

const PUNCT = [
  '<=', '>=', '==', '!=', '&&', '||',
  '+', '-', '*', '/', '%', '(', ')', ',', '?', ':', '<', '>', '!', '=',
];

export function tokenize(src, line) {
  const out = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === ' ' || c === '\t' || c === '\n' || c === '\r') { i++; continue; }
    if (c === '#') break;

    if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(src[i + 1] || ''))) {
      const m = /^(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?/.exec(src.slice(i));
      out.push({ t: 'num', v: parseFloat(m[0]), line });
      i += m[0].length;
      continue;
    }
    if (/[A-Za-z_]/.test(c)) {
      const m = /^[A-Za-z_][A-Za-z0-9_]*/.exec(src.slice(i));
      out.push({ t: 'id', v: m[0], line });
      i += m[0].length;
      continue;
    }
    const p = PUNCT.find((s) => src.startsWith(s, i));
    if (!p) throw new CompileError(`unexpected character '${c}'`, line);
    out.push({ t: 'punct', v: p, line });
    i += p.length;
  }
  out.push({ t: 'eof', v: null, line });
  return out;
}
