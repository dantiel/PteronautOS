/**
 * ÆtherHAML Vite Plugin
 * Transforms .lithaml files into Lit html`...` template exports.
 * Uses `load` hook to intercept before Vite's import analysis, which would
 * otherwise choke on non-JS .lithaml syntax.
 */
import { compileHamlToLit } from './haml-lit-compiler.js'
import fs from 'fs'

export function hamlLitPlugin() {
  return {
    name: 'aether-haml-lit',
    enforce: 'pre',
    load(id) {
      if (!id.endsWith('.lithaml')) return null
      try {
        const src = fs.readFileSync(id, 'utf8')
        const js = compileHamlToLit(src, id)
        return js
      } catch (e) {
        console.error(`ÆtherHAML error in ${id}:`, e.message)
        throw e
      }
    }
  }
}