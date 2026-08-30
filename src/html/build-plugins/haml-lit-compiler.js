/**
 * ÆtherHAML — HAML→Lit Compiler
 * 
 * Transforms .lithaml files into Lit html`...` tagged template exports.
 * 
 * HAML Syntax:
 *   %tagname.class#id{attr: "val"}  →  <tagname class="class" id="id" attr="val">
 *   .class                           →  <div class="class">
 *   #id                              →  <div id="id">
 *     text content                   →  text content
 *     = self.expr                    →  ${self.expr}
 *     != self.unsafe                 →  ${unsafeHTML(self.unsafe)}
 *     %tag{@event: self.fn}          →  <tag @event="${self.fn}">
 *     %tag{.prop: self.val}          →  <tag .prop="${self.val}">
 *     %tag{?attr: self.bool}         →  <tag ?attr="${self.bool}">
 *     - if self.cond                 →  ${self.cond ? html`...` : html`...`}
 *     - else                         →  else branch of nearest - if
 *     - elsif self.cond2             →  else-if branch
 *     - for item of self.list        →  ${self.list.map(item => html`...`)}
 *     - choose self.expr             →  ${(expr === val) ? html`...` : ...}
 *     - when val                     →  branch of nearest - choose
 *     - otherwise                    →  default branch of - choose
 *     - for item, idx of self.list   →  ${self.list.map((item, idx) => html`...`)}
 *     :plain                         →  raw text block
 *     / comment                      →  skipped
 */

/**
 * Parse a single HAML line into its components.
 * Returns {type, tag, classes, id, attrs, inlineContent, codeKeyword, codeExpr, filterName}
 */
function parseLine(line) {
  const trimmed = line.trimStart()
  if (!trimmed) return { type: 'blank' }

  // Comment
  if (trimmed.startsWith('/ ')) return { type: 'comment' }

  // Filter
  if (trimmed.startsWith(':plain')) return { type: 'filter', filterName: 'plain' }
  if (trimmed.startsWith(':coffee')) return { type: 'filter', filterName: 'coffee' }
  if (trimmed.startsWith(':css')) return { type: 'filter', filterName: 'css' }
  if (trimmed.startsWith(':javascript')) return { type: 'filter', filterName: 'javascript' }

  // Dynamic expression: = expr
  if (trimmed.startsWith('= ')) {
    return { type: 'expr', inlineContent: trimmed.slice(2).trim() }
  }
  // Unescaped: != expr
  if (trimmed.startsWith('!= ')) {
    return { type: 'unescaped', inlineContent: trimmed.slice(3).trim() }
  }

  // Code: - if/else/elsif/for/while/switch/return/...
  if (trimmed.startsWith('- ')) {
    const code = trimmed.slice(2).trim()
    const codeMatch = code.match(/^(if|else|elsif|for|while|switch|unless|until|choose|when|otherwise)\b/)
    if (codeMatch) {
      return {
        type: 'code',
        codeKeyword: codeMatch[1],
        codeExpr: code.slice(codeMatch[1].length).trim()
      }
    }
    // Arbitrary code line (we treat as expression output for simplicity)
    return { type: 'code', codeKeyword: 'expr', codeExpr: code }
  }

  // Tag: %tagname[.class][#id][{attrs}][ content]
  if (trimmed.startsWith('%')) {
    return parseTagLine(trimmed)
  }

  // Class shorthand: .classname[ content]
  if (trimmed.startsWith('.')) {
    return parseClassShorthand(trimmed)
  }

  // ID shorthand: #idname[ content]
  if (trimmed.startsWith('#')) {
    return parseIdShorthand(trimmed)
  }

  // Plain text
  return { type: 'text', inlineContent: trimmed }
}

function parseTagLine(str) {
  // %tagname.class1.class2#id{attr: val, @event: fn, .prop: val} inline content
  let rest = str.slice(1) // remove %
  
  // Extract tag name
  const tagMatch = rest.match(/^([a-zA-Z][a-zA-Z0-9_-]*)/)
  if (!tagMatch) return { type: 'text', inlineContent: str }
  
  const tag = tagMatch[1]
  rest = rest.slice(tagMatch[0].length)

  // Extract classes
  const classes = []
  while (rest.startsWith('.')) {
    const clsMatch = rest.match(/^\.([a-zA-Z][a-zA-Z0-9_-]*)/)
    if (!clsMatch) break
    classes.push(clsMatch[1])
    rest = rest.slice(clsMatch[0].length)
  }

  // Extract id
  let id = null
  if (rest.startsWith('#')) {
    const idMatch = rest.match(/^#([a-zA-Z][a-zA-Z0-9_-]*)/)
    if (idMatch) {
      id = idMatch[1]
      rest = rest.slice(idMatch[0].length)
    }
  }

  // Extract attributes hash {...}
  let attrs = {}
  if (rest.startsWith('{')) {
    const attrResult = parseAttrs(rest)
    attrs = attrResult.attrs
    rest = attrResult.rest
  }

  // Remaining is inline content
  const inlineContent = rest.trim() || null

  return { type: 'tag', tag, classes, id, attrs, inlineContent }
}

function parseClassShorthand(str) {
  // .class1.class2#id{attrs} content
  let rest = str
  const classes = []
  while (rest.startsWith('.')) {
    const clsMatch = rest.match(/^\.([a-zA-Z][a-zA-Z0-9_-]*)/)
    if (!clsMatch) break
    classes.push(clsMatch[1])
    rest = rest.slice(clsMatch[0].length)
  }

  let id = null
  if (rest.startsWith('#')) {
    const idMatch = rest.match(/^#([a-zA-Z][a-zA-Z0-9_-]*)/)
    if (idMatch) {
      id = idMatch[1]
      rest = rest.slice(idMatch[0].length)
    }
  }

  let attrs = {}
  if (rest.startsWith('{')) {
    const attrResult = parseAttrs(rest)
    attrs = attrResult.attrs
    rest = attrResult.rest
  }

  const inlineContent = rest.trim() || null
  return { type: 'tag', tag: 'div', classes, id, attrs, inlineContent }
}

function parseIdShorthand(str) {
  // #id.class1.class2{attrs} content
  let rest = str
  let id = null
  if (rest.startsWith('#')) {
    const idMatch = rest.match(/^#([a-zA-Z][a-zA-Z0-9_-]*)/)
    if (idMatch) {
      id = idMatch[1]
      rest = rest.slice(idMatch[0].length)
    }
  }

  const classes = []
  while (rest.startsWith('.')) {
    const clsMatch = rest.match(/^\.([a-zA-Z][a-zA-Z0-9_-]*)/)
    if (!clsMatch) break
    classes.push(clsMatch[1])
    rest = rest.slice(clsMatch[0].length)
  }

  let attrs = {}
  if (rest.startsWith('{')) {
    const attrResult = parseAttrs(rest)
    attrs = attrResult.attrs
    rest = attrResult.rest
  }

  const inlineContent = rest.trim() || null
  return { type: 'tag', tag: 'div', classes, id, attrs, inlineContent }
}

function parseAttrs(str) {
  // {attr: val, attr2: "val2", @event: fn, .prop: val, ?bool: cond}
  const attrs = {}
  let depth = 1 // we start after the opening {
  let i = 1
  let currentKey = ''
  let currentVal = ''
  let inKey = true
  let inString = false
  let stringChar = ''
  let braceDepth = 0
  let bracketDepth = 0

  while (i < str.length) {
    const ch = str[i]

    if (inString) {
      currentVal += ch
      if (ch === '\\') {
        currentVal += str[i + 1] || ''
        i += 2
        continue
      }
      if (ch === stringChar) {
        inString = false
      }
      i++
      continue
    }

    if (ch === '"' || ch === "'") {
      inString = true
      stringChar = ch
      currentVal += ch
      i++
      continue
    }

    if (ch === '{') { braceDepth++; currentVal += ch; i++; continue }
    if (ch === '}') {
      if (braceDepth > 0) { braceDepth--; currentVal += ch; i++; continue }
      // End of attributes
      if (currentKey.trim()) {
        attrs[formatAttrKey(currentKey.trim())] = currentVal.trim()
      }
      return { attrs, rest: str.slice(i + 1) }
    }
    if (ch === '(') { bracketDepth++; currentVal += ch; i++; continue }
    if (ch === ')') { bracketDepth--; currentVal += ch; i++; continue }

    if (ch === ',' && braceDepth === 0 && bracketDepth === 0) {
      // End of key:value pair
      if (currentKey.trim()) {
        attrs[formatAttrKey(currentKey.trim())] = currentVal.trim()
      }
      currentKey = ''
      currentVal = ''
      inKey = true
      i++
      // skip whitespace after comma
      while (str[i] === ' ') i++
      continue
    }

    if (ch === ':' && inKey && braceDepth === 0 && bracketDepth === 0) {
      inKey = false
      i++
      // skip whitespace after colon
      while (str[i] === ' ') i++
      continue
    }

    if (inKey) {
      currentKey += ch
    } else {
      currentVal += ch
    }
    i++
  }

  // If we get here, the attributes hash didn't close properly
  if (currentKey.trim()) {
    attrs[formatAttrKey(currentKey.trim())] = currentVal.trim()
  }
  return { attrs, rest: '' }
}

function formatAttrKey(key) {
  // Lit special attributes: @event, .prop, ?bool
  if (key.startsWith('@') || key.startsWith('.') || key.startsWith('?')) {
    return key
  }
  return key
}

/**
 * Build a tree of {indent, parsed, children} from token list
 */
function buildTree(tokens) {
  const stack = [{ indent: -1, parsed: { type: 'root' }, children: [] }]
  
  for (const tok of tokens) {
    const node = { indent: tok.indent, parsed: tok.parsed, children: [] }
    
    // Pop stack until we find a parent with smaller indent
    while (stack.length > 1 && stack[stack.length - 1].indent >= node.indent) {
      stack.pop()
    }
    
    stack[stack.length - 1].children.push(node)
    stack.push(node)
  }
  
  return stack[0] // root
}

/**
 * Tokenize HAML source into {indent, parsed} tokens
 */
function tokenize(source) {
  const lines = source.split('\n')
  const tokens = []
  let indentSize = null

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const trimmed = line.trimStart()
    
    // Skip blank lines and comments
    if (!trimmed || trimmed.startsWith('/ ')) {
      // Comment: skip entirely
      if (trimmed.startsWith('/ ')) continue
      // Blank line in filter block? Keep as empty token
      if (!trimmed) continue
    }

    // Determine indent
    const indent = line.length - trimmed.length
    
    // Auto-detect indent size from first indented line
    if (indentSize === null && indent > 0) {
      indentSize = indent
    }

    const parsed = parseLine(trimmed)
    if (parsed.type === 'comment') continue
    if (parsed.type === 'blank') continue
    
    tokens.push({ indent, parsed })
  }

  return tokens
}

/**
 * Generate Lit html`...` output from the HAML tree
 */
function generateLit(node, indent, vars) {
  const { parsed, children } = node
  
  // Root: just output children
  if (parsed.type === 'root') {
    return children.map(c => generateLit(c, indent + 1, vars)).join('\n')
  }

  // Filter block
  if (parsed.type === 'filter') {
    if (parsed.filterName === 'plain') {
      // :plain filter — children are raw text lines, output as-is
      const rawLines = children.map(c => c.parsed.inlineContent || '').join('\n')
      return rawLines
    }
    return ''
  }

  // Expression
  if (parsed.type === 'expr') {
    const expr = processExpr(parsed.inlineContent)
    return `\${${expr}}`
  }

  // Unescaped expression
  if (parsed.type === 'unescaped') {
    const expr = processExpr(parsed.inlineContent)
    // In Lit, unsafeHTML must be imported. We generate it as a raw expression
    // and add unsafeHTML to the import if needed
    vars.needsUnsafeHTML = true
    return `\${unsafeHTML(${expr})}`
  }

  // Code: if/else/elsif/for
  if (parsed.type === 'code') {
    return generateCodeBlock(node, indent, vars)
  }

  // Text
  if (parsed.type === 'text') {
    return escapeLitText(parsed.inlineContent)
  }

  // Tag
  if (parsed.type === 'tag') {
    return generateTag(node, indent, vars)
  }

  return ''
}

function replaceSelfRefs(expr) {
  return expr.replace(/@(\w+)/g, 'self.$1')
}

/**
 * Convert Haml/CoffeeScript operators to JavaScript:
 *   is → ===, isnt → !==, and → &&, or → ||, not → !
 */
function hamlExprToJs(expr) {
  let js = expr
  js = js.replace(/\bisnt\b/g, '!==')
  js = js.replace(/\bis\b/g, '===')
  js = js.replace(/\band\b/g, '&&')
  js = js.replace(/\bor\b/g, '||')
  js = js.replace(/\bnot\b/g, '!')
  return js
}

/**
 * Process a Haml expression: @ → self., operators, fn calls
 */
function processExpr(raw) {
  let expr = replaceSelfRefs(raw)
  expr = hamlInterpolateToJs(expr)
  expr = hamlExprToJs(expr)
  expr = convertHamlFnCall(expr)
  return expr
}

/**
 * Convert Haml-style #{} string interpolation to JS template literal ${}:
 *   "text #{expr} more"  →  `text ${expr} more`
 */
function hamlInterpolateToJs(expr) {
  // Only process if expression contains #{
  if (!expr.includes('#{')) return expr

  // Case: entire expression is a double-quoted string with interpolation
  // e.g. "#{axis.label} Axis PID"
  if (expr.startsWith('"') && expr.endsWith('"') && expr.indexOf('#{') > 0) {
    const inner = expr.slice(1, -1)
    const converted = inner.replace(/#\{([^}]+)\}/g, '${$1}')
    return '`' + converted + '`'
  }

  // Case: mixed — convert each quoted substring independently
  // e.g.  "Title: #{val}" + suffix
  return expr.replace(/"([^"]*)#\{([^}]+)\}([^"]*)"/g, '`$1${$2}$3`')
}

/**
 * Convert Haml-style function calls (space-separated args) to JS:
 *   "self.func arg1 arg2" → "self.func(arg1, arg2)"
 * Detects patterns like: identifier (identifier|number|string)+
 */
function convertHamlFnCall(expr) {
  // Only convert if the expression looks like a function call:
  // methodPath arg1 [arg2 ...] where args are identifiers/numbers/strings
  if (!/\s/.test(expr)) return expr
  
  const match = expr.match(/^(\S+)\s+(.+)$/)
  if (!match) return expr
  
  const fnPath = match[1]
  const argsStr = match[2]
  
  // Only convert if fnPath looks like a method call target
  if (!/^[@\w.]+$/.test(fnPath)) return expr
  
  // Split args by whitespace but respect strings
  const args = []
  let current = ''
  let inStr = false
  let strChar = ''
  for (let i = 0; i < argsStr.length; i++) {
    const ch = argsStr[i]
    if (inStr) {
      current += ch
      if (ch === '\\') { current += argsStr[i+1] || ''; i++; continue }
      if (ch === strChar) inStr = false
      continue
    }
    if (ch === '"' || ch === "'") { inStr = true; strChar = ch; current += ch; continue }
    if (ch === ' ' || ch === '\t') {
      if (current) { args.push(current); current = '' }
      continue
    }
    current += ch
  }
  if (current) args.push(current)
  
  if (args.length === 0) return expr
  
  // Guard: only convert if args look like identifiers/numbers/strings, not operators
  for (const arg of args) {
    // Reject args that start with operators or contain comparison operators
    if (/^[=!<>+\-*\/%&|^~?.:;,\[\]{}()]/.test(arg)) return expr
  }
  
  return `${fnPath}(${args.join(', ')})`
}

function escapeLitText(text) {
  // Escape characters that would break the template literal
  return text
    .replace(/`/g, '\\`')
    .replace(/\$/g, '\\$')
    .replace(/\\/g, '\\\\')
}

function cleanAttrVal(val) {
  // Strip surrounding quotes from parsed attribute values
  const trimmed = val.trim()
  if ((trimmed.startsWith('"') && trimmed.endsWith('"')) ||
      (trimmed.startsWith("'") && trimmed.endsWith("'"))) {
    return trimmed.slice(1, -1)
  }
  return trimmed
}

function generateTag(node, indent, vars) {
  const { tag, classes, id, attrs, inlineContent } = node.parsed
  const children = node.children || []
  
  // Build tag string
  let classStr = ''
  if (classes && classes.length > 0) {
    classStr = ` class="${classes.join(' ')}"`
  }
  
  let idStr = ''
  if (id) {
    idStr = ` id="${id}"`
  }

  // Build attributes string, handling Lit special attrs
  let attrStr = ''
  let litEvents = ''
  let litProps = ''
  let litBools = ''
  
  for (const [key, val] of Object.entries(attrs)) {
    if (key.startsWith('@')) {
      // Event binding: @event="handler"
      const eventName = key.slice(1)
      const handler = processExpr(val)
      litEvents += ` @${eventName}="\${${handler}}"`
    } else if (key.startsWith('.')) {
      // Property binding: .prop="value"
      const propName = key.slice(1)
      const propVal = processExpr(val)
      litProps += ` .${propName}="\${${propVal}}"`
    } else if (key.startsWith('?')) {
      // Boolean attribute: ?attr="bool"
      const boolName = key.slice(1)
      const boolVal = processExpr(val)
      litBools += ` ?${boolName}="\${${boolVal}}"`
    } else {
      // Unquoted values are dynamic expressions, quoted values are static
      const trimmed = val.trim()
      const isQuoted = (trimmed.startsWith('"') && trimmed.endsWith('"')) ||
                       (trimmed.startsWith("'") && trimmed.endsWith("'"))
      if (!isQuoted) {
        const dynVal = processExpr(trimmed)
        attrStr += ` ${key}="\${${dynVal}}"`
      } else {
        attrStr += ` ${key}="${cleanAttrVal(val)}"`
      }
    }
  }

  const openTag = `<${tag}${classStr}${idStr}${attrStr}${litEvents}${litProps}${litBools}`
  
  // Self-closing tags
  const voidElements = new Set([
    'br', 'hr', 'img', 'input', 'link', 'meta', 'area', 'base', 'col',
    'embed', 'source', 'track', 'wbr'
  ])
  
  if (voidElements.has(tag) && !inlineContent && children.length === 0) {
    return `${openTag}/>`
  }

  let result = ''
  
  if (children.length === 0) {
    // Leaf tag with optional inline content
    if (inlineContent) {
      if (inlineContent.startsWith('= ')) {
        const expr = processExpr(inlineContent.slice(2).trim())
        result = `${openTag}>\${${expr}}</${tag}>`
      } else if (inlineContent.startsWith('!= ')) {
        vars.needsUnsafeHTML = true
        const expr = processExpr(inlineContent.slice(3).trim())
        result = `${openTag}>\${unsafeHTML(${expr})}</${tag}>`
      } else {
        result = `${openTag}>${escapeLitText(inlineContent)}</${tag}>`
      }
    } else {
      result = `${openTag}></${tag}>`
    }
  } else {
    // Tag with children
    result = `${openTag}>`
    const childOutput = children.map(c => generateLit(c, indent + 1, vars)).join('')
    result += childOutput
    result += `</${tag}>`
  }

  return result
}

function generateCodeBlock(node, indent, vars) {
  const { codeKeyword, codeExpr } = node.parsed
  const children = node.children || []

  if (codeKeyword === 'if' || codeKeyword === 'unless') {
    return generateConditional(node, indent, vars)
  }

  if (codeKeyword === 'for') {
    return generateLoop(node, indent, vars)
  }

  if (codeKeyword === 'choose') {
    return generateChoose(node, indent, vars)
  }

  if (codeKeyword === 'else' || codeKeyword === 'elsif' || codeKeyword === 'when' || codeKeyword === 'otherwise') {
    // These are handled by generateConditional / generateChoose when processing the parent
    return ''
  }

  // Arbitrary code (var assignments, calls, etc.)
  const assignMatch = codeExpr.match(/^(\w+)\s*=\s*(.+)$/)
  if (assignMatch) {
    const varName = assignMatch[1]
    const valExpr = processExpr(assignMatch[2])
    // IIFE with var declaration (strict-mode safe), stores on self
    return `\${(() => { var ${varName} = ${valExpr}; self._${varName} = ${varName}; return ''; })()}`
  }
  // Other: suppress output via comma operator
  const expr = processExpr(codeExpr)
  return `\${(${expr}, '')}`
}

function generateConditional(ifNode, indent, vars) {
  const results = []
  let current = ifNode
  
  while (current) {
    const { codeKeyword, codeExpr } = current.parsed
    const children = current.children || []
    
    if (codeKeyword === 'if') {
      const cond = processExpr(codeExpr || 'false')
      const body = wrapChildrenInHtml(children, indent, vars)
      results.push({ cond, body, type: 'if' })
    } else if (codeKeyword === 'elsif') {
      const cond = processExpr(codeExpr || 'false')
      const body = wrapChildrenInHtml(children, indent, vars)
      results.push({ cond, body, type: 'elsif' })
    } else if (codeKeyword === 'unless') {
      const cond = `!(${processExpr(codeExpr || 'false')})`
      const body = wrapChildrenInHtml(children, indent, vars)
      results.push({ cond, body, type: 'if' })
    } else if (codeKeyword === 'else') {
      const body = wrapChildrenInHtml(children, indent, vars)
      results.push({ cond: null, body, type: 'else' })
    }
    
    // Find the next sibling that is else/elsif
    const parent = findParent(ifNode)
    if (!parent) break
    
    const idx = parent.children.indexOf(current)
    const next = parent.children[idx + 1]
    if (next && (next.parsed.codeKeyword === 'else' || next.parsed.codeKeyword === 'elsif')) {
      current = next
    } else {
      current = null
    }
  }
  
  // Build nested ternary — always include fallback for valid JS
  const hasElse = results.some(r => r.type === 'else')
  let out = '${'
  for (let i = 0; i < results.length; i++) {
    const r = results[i]
    const isLast = i === results.length - 1
    if (r.type === 'if') {
      if (isLast && !hasElse) {
        out += `${r.cond} ? ${r.body} : null`
      } else {
        out += `${r.cond} ? ${r.body}`
      }
    } else if (r.type === 'elsif') {
      if (isLast && !hasElse) {
        out += ` : ${r.cond} ? ${r.body} : null`
      } else {
        out += ` : ${r.cond} ? ${r.body}`
      }
    } else if (r.type === 'else') {
      out += ` : ${r.body}`
    }
  }
  out += '}'
  
  return out
}

function generateChoose(chooseNode, indent, vars) {
  const subject = processExpr(chooseNode.parsed.codeExpr || 'null')
  const results = []

  for (const child of (chooseNode.children || [])) {
    const { codeKeyword, codeExpr } = child.parsed
    const body = wrapChildrenInHtml(child.children, indent, vars)
    if (codeKeyword === 'when') {
      const val = processExpr(codeExpr || 'null')
      results.push({ cond: `(${subject}) === (${val})`, body, type: 'when' })
    } else if (codeKeyword === 'otherwise') {
      results.push({ cond: null, body, type: 'else' })
    }
  }

  if (results.length === 0) return '${null}'

  const hasElse = results.some(r => r.type === 'else')
  let out = '${'
  for (let i = 0; i < results.length; i++) {
    const r = results[i]
    const isLast = i === results.length - 1
    if (r.type === 'when') {
      const prefix = i === 0 ? '' : ' : '
      if (isLast && !hasElse) {
        out += `${prefix}${r.cond} ? ${r.body} : null`
      } else {
        out += `${prefix}${r.cond} ? ${r.body}`
      }
    } else if (r.type === 'else') {
      out += ` : ${r.body}`
    }
  }
  out += '}'
  return out
}

function generateLoop(forNode, indent, vars) {
  const { codeExpr } = forNode.parsed
  // "item of self.list" or "item, idx of self.list"
  const loopMatch = codeExpr.match(/^(\w+)(?:\s*,\s*(\w+))?\s+(?:of|in)\s+(.+)$/)
  
  if (!loopMatch) {
    // Can't parse — output as expression
    return `\${${processExpr(codeExpr)}}`
  }
  
  const itemVar = loopMatch[1]
  const idxVar = loopMatch[2] || null
  const listExpr = processExpr(loopMatch[3])
  
  const body = wrapChildrenInHtml(forNode.children, indent, vars, { itemVar, idxVar })
  
  const args = idxVar ? `(${itemVar}, ${idxVar})` : `${itemVar}`
  return `\${${listExpr}.map(${args} => ${body})}`
}

function wrapChildrenInHtml(children, indent, vars, loopVars) {
  if (!children || children.length === 0) return 'html``'
  
  // Generate content from children, replacing loop vars in self-refs
  const savedReplace = replaceSelfRefs
  let inner = ''
  
  if (loopVars) {
    // In loop context, don't replace @ with self.
    inner = children.map(c => generateLit(c, indent + 1, vars)).join('')
  } else {
    inner = children.map(c => generateLit(c, indent + 1, vars)).join('')
  }
  
  return `html\`${inner}\``
}

function findParent(node) {
  // Find parent by traversing — we need the tree structure
  // For now, use a simple approach: walk the tree
  return findParentInTree(rootNode, node)
}

let rootNode = null

function findParentInTree(root, target) {
  if (!root) return null
  for (const child of root.children || []) {
    if (child === target) return root
    const found = findParentInTree(child, target)
    if (found) return found
  }
  return null
}

/**
 * Main compilation function
 */
export function compileHamlToLit(source, filename) {
  const tokens = tokenize(source)
  const tree = buildTree(tokens)
  rootNode = tree
  
  const vars = { needsUnsafeHTML: false }
  const body = tree.children.map(c => generateLit(c, 1, vars)).join('\n')
  
  // Clean up the body for template literal embedding
  // We need to ensure the body is valid inside a template literal
  const cleanBody = body
  
  // Build the import statement based on needs
  let imports = `import { html${vars.needsUnsafeHTML ? ', unsafeHTML' : ''} } from 'lit'`
  
  const output = `${imports}
export default (self) => html\`${cleanBody}\``
  
  return output
}