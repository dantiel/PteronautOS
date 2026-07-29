/**
 * ÆtherCSX — CSX→Lit Transform for Vite
 * 
 * Converts CoffeeScript+JSX (.csx) to CoffeeScript with Lit html`...` backtick escapes,
 * then pipes through the CoffeeScript compiler.
 * 
 * Syntax:  <div class="foo">  →  `html\`<div class="foo">\``
 *          {expression}        →  ${expression}  (inside JSX only)
 *          <br/>               →  `html\`<br/>\``
 */

const coffee = require('coffeescript');

// State machine for JSX extraction
function transformCSX(source, filename) {
  const lines = source.split('\n');
  const output = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];
    const trimmed = line.trimStart();
    const indent = line.slice(0, line.length - trimmed.length);

    // Detect JSX element start: <[A-Za-z] at beginning of expression position
    const jsxMatch = trimmed.match(/^<([A-Za-z][A-Za-z0-9]*)/);
    
    if (jsxMatch) {
      const result = extractJSX(lines, i, indent);
      output.push(result.transformed);
      i = result.endLine + 1;
    } else {
      // Check inline JSX in expressions: return <div>...
      const inlineJsx = trimmed.match(/^(.*?)((?:return|=>|then|else|=)\s+)<([A-Za-z][A-Za-z0-9]*)/);
      if (inlineJsx) {
        const prefix = inlineJsx[1] + inlineJsx[2];
        const rest = trimmed.slice(prefix.length);
        lines[i] = indent + rest; // Temporarily rewrite line to start with JSX
        const result = extractJSX(lines, i, indent + ' '.repeat(prefix.length));
        output.push(indent + prefix.trimStart() + result.transformed.trimStart());
        i = result.endLine + 1;
      } else {
        output.push(line);
        i++;
      }
    }
  }

  return output.join('\n');
}

function extractJSX(lines, startLine, baseIndent) {
  // Collect all lines belonging to this JSX tree
  const jsxLines = [];
  let i = startLine;
  let depth = 0;
  let started = false;
  let jsxStartCol = null;

  while (i < lines.length) {
    const line = lines[i];
    const trimmed = line.trimStart();

    if (i === startLine) {
      // Find where JSX starts on this line
      jsxStartCol = line.indexOf('<');
      const afterJsxStart = line.slice(jsxStartCol);
      jsxLines.push(afterJsxStart);
      
      // Count tags on first line
      const tagCount = countTagDelta(afterJsxStart);
      depth += tagCount;
      started = true;
    } else if (started) {
      // Check if this line is still part of the JSX (indented or continuation)
      const lineIndent = line.length - trimmed.length;
      
      if (trimmed.startsWith('</') || trimmed.startsWith('<') || trimmed.startsWith('/>') || 
          trimmed.startsWith('{') || trimmed.startsWith('}') || trimmed === '' ||
          (depth > 0 && lineIndent > baseIndent.length)) {
        jsxLines.push(trimmed);
        const tagCount = countTagDelta(trimmed);
        depth += tagCount;
      } else {
        // JSX ended
        break;
      }
    }
    
    if (depth <= 0 && started && i > startLine) {
      i++;
      break;
    }
    
    i++;
  }

  const jsxRaw = jsxLines.join('\n');
  const jsxTransformed = convertJSXToLit(jsxRaw);
  
  // Build the output: `html\`...\``
  // Escape backticks in the content
  const escaped = jsxTransformed.replace(/`/g, '\\`');
  const litWrapped = '`html\\`' + escaped + '\\``';
  
  return {
    transformed: ' '.repeat(jsxStartCol || 0) + litWrapped,
    endLine: i - 1
  };
}

function countTagDelta(line) {
  let delta = 0;
  // Opening tags: <tag ...> or <tag ... />
  const openTags = line.match(/<[A-Za-z][A-Za-z0-9]*(?:\s[^>]*)?(?<!\/)>/g);
  const closeTags = line.match(/<\/[A-Za-z][A-Za-z0-9]*>/g);
  const selfClose = line.match(/<[A-Za-z][A-Za-z0-9]*(?:\s[^>]*)?\/>/g);
  
  if (openTags) delta += openTags.length;
  if (closeTags) delta -= closeTags.length;
  // self-closing tags don't affect depth
  
  return delta;
}

function convertJSXToLit(jsx) {
  // Convert {expression} to ${expression} — but only JSX expressions (not template literals)
  // Strategy: find { that's not inside a string
  let result = '';
  let i = 0;
  let inString = false;
  let stringChar = '';
  
  while (i < jsx.length) {
    const ch = jsx[i];
    
    if (inString) {
      result += ch;
      if (ch === '\\') {
        result += jsx[i + 1] || '';
        i += 2;
        continue;
      }
      if (ch === stringChar) {
        inString = false;
      }
      i++;
      continue;
    }
    
    if (ch === '"' || ch === "'") {
      inString = true;
      stringChar = ch;
      result += ch;
      i++;
      continue;
    }
    
    if (ch === '{' && jsx[i - 1] !== '$' && jsx[i - 1] !== '\\') {
      // JSX expression start
      result += '${';
      i++;
      continue;
    }
    
    // } at end of expression becomes }
    if (ch === '}' && i > 0 && jsx[i - 1] !== '\\') {
      // Could be end of ${expr} or a lone }
      // If preceded by ${ we already handled. If it's standalone, it's JSX closing.
      // Check if we're inside a ${...} context
      const before = result.slice(-2);
      if (before === '${') {
        // Already handled as part of ${ — just keep the }
        result += '}';
        i++;
        continue;
      }
      // Standalone } — keep as is (could be CSS or literal)
      result += '}';
      i++;
      continue;
    }
    
    result += ch;
    i++;
  }
  
  return result;
}

module.exports = function csxPlugin() {
  return {
    name: 'aether-csx',
    
    transform(src, id) {
      if (!id.endsWith('.csx')) return null;
      
      // Phase 1: CSX → CoffeeScript (with backtick escapes)
      const coffeeSource = transformCSX(src, id);
      
      // Phase 2: CoffeeScript → JavaScript
      try {
        const result = coffee.compile(coffeeSource, {
          bare: true,
          sourceMap: true,
          filename: id
        });
        
        return {
          code: result.js,
          map: result.sourceMap || null
        };
      } catch (e) {
        console.error(`ÆtherCSX error in ${id}:`, e.message);
        console.error('Generated CoffeeScript:\n', coffeeSource);
        throw e;
      }
    }
  };
};
