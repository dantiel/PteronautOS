/**
 * Vite plugin: CoffeeScript → JavaScript
 * Transpiles .coffee files on-the-fly using the coffeescript package.
 * Converts CommonJS module.exports patterns to ESM for browser compatibility.
 */
import { compile } from 'coffeescript';

/**
 * Convert CJS `module.exports = {...}` and `module.exports.foo =` to ESM.
 * Uses a `__exports` proxy object to handle cross-references within the module,
 * then exports all keys without redeclaring existing `var` bindings (rolldown-safe).
 */
function cjsToEsm(code) {
  // Collect all export names (both from object literal and .foo = assignments)
  const names = [];

  // module.exports = { a, b: c, ... }  →  const __exports = { a, b: c, ... }
  code = code.replace(
    /module\.exports\s*=\s*\{/g,
    () => 'const __exports = {'
  );

  // module.exports.foo = expr  →  __exports.foo = expr (track name)
  code = code.replace(
    /module\.exports\.(\w+)\s*=/g,
    (_, name) => {
      if (!names.includes(name)) names.push(name);
      return `__exports.${name} =`;
    }
  );

  // Standalone module.exports references  →  __exports
  code = code.replace(/module\.exports/g, '__exports');

  // Extract keys from the __exports = { ... } object literal
  const objMatch = code.match(/const __exports = \{([^}]*)\}/s);
  if (objMatch) {
    let body = objMatch[1];
    body = body.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');
    const parts = [];
    let depth = 0, start = 0;
    for (let i = 0; i < body.length; i++) {
      if (body[i] === '{' || body[i] === '(' || body[i] === '[') depth++;
      if (body[i] === '}' || body[i] === ')' || body[i] === ']') depth--;
      if (body[i] === ',' && depth === 0) {
        parts.push(body.slice(start, i).trim());
        start = i + 1;
      }
    }
    parts.push(body.slice(start).trim());

    for (const p of parts) {
      if (!p) continue;
      const colonIdx = p.indexOf(':');
      const key = colonIdx >= 0 ? p.slice(0, colonIdx).trim() : p.trim();
      if (key && !names.includes(key)) names.push(key);
    }
  }

  // Find names already declared with `var` (CoffeeScript hoists all at top)
  const varNames = [];
  const varMatch = code.match(/^var\s+([^;]+);/m);
  if (varMatch) {
    varMatch[1].split(',').forEach(s => {
      const n = s.trim();
      if (n) varNames.push(n);
    });
  }

  // Split export names into: already-declared (re-export) vs new (need const)
  const reExports = [];
  const newExports = [];
  for (const n of names) {
    if (varNames.includes(n)) {
      reExports.push(n);
    } else {
      newExports.push(n);
    }
  }

  // Generate export statements (rolldown-safe: no redeclaration)
  if (reExports.length > 0) {
    code += `\nexport { ${reExports.join(', ')} }`;
  }
  for (const n of newExports) {
    code += `\nexport const ${n} = __exports.${n}`;
  }

  return code;
}

export function coffeePlugin() {
  return {
    name: 'vite-plugin-coffee',
    enforce: 'pre',
    transform(src, id) {
      if (!id.endsWith('.coffee')) return null;
      let js = compile(src, {
        bare: true,
        sourceMap: false,
      });
      js = cjsToEsm(js);
      return {
        code: js,
        map: null,
      };
    },
  };
}