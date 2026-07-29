/**
 * Vite plugin: CoffeeScript → JavaScript
 * Transpiles .coffee files on-the-fly using the coffeescript package.
 * Converts CommonJS module.exports patterns to ESM for browser compatibility.
 */
import { compile } from 'coffeescript';

/**
 * Convert CJS `module.exports = {...}` and `module.exports.foo =` to ESM.
 * Uses a `__exports` proxy object to handle cross-references within the module,
 * then destructure-exports all keys at the end.
 */
function cjsToEsm(code) {
  // Track all export names from both the object literal and .foo = assignments
  const names = [];

  // module.exports = { a, b: c, ... }  →  const __exports = { a, b: c, ... }
  // Extract keys for later export
  code = code.replace(
    /module\.exports\s*=\s*\{/g,
    (match) => {
      return 'const __exports = {';
    }
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
    // Strip comments (both // and /* */) before parsing
    body = body.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');
    // Split on commas respecting nesting depth
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

  // Append destructure export
  if (names.length > 0) {
    code += `\nexport const { ${names.join(', ')} } = __exports`;
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