import assert from 'node:assert/strict'
import fs from 'node:fs'
import vm from 'node:vm'

const source = fs.readFileSync(new URL('../src/html/src/utils/feedback.js', import.meta.url), 'utf8')
const functionSource = source.slice(source.indexOf('export async function loadJSON('), source.indexOf('// Generic POST helper'))
async function scenario(statuses, expectedCalls, fails = false) {
  let calls = 0
  let unread = false
  let timers = 0
  const context = vm.createContext({
    AbortController,
    setTimeout(fn, ms) {
      if (ms === 8000) { timers++; return 1 }
      fn(); return 0
    },
    clearTimeout(id) { if (id === 1) timers-- },
    fetch: async () => {
      assert.equal(unread, false, 'retry must drain the previous error body')
      const status = statuses[Math.min(calls++, statuses.length - 1)]
      if (status === 'network') throw new TypeError('connection lost')
      unread = true
      return {
        ok: status === 200, status,
        text: async () => { unread = false; return `error ${status}` },
        json: async () => { unread = false; return {ok: true} },
      }
    },
  })
  vm.runInContext(functionSource.replace('export ', ''), context)
  if (fails) await assert.rejects(context.loadJSON('/test'))
  else assert.equal((await context.loadJSON('/test')).ok, true)
  assert.equal(calls, expectedCalls)
  assert.equal(timers, 0, 'timeout must always be cleared')
}
await scenario([200], 1)
await scenario([503, 429, 200], 3)
await scenario(['network', 200], 2)
await scenario([503], 6, true)
await scenario([404], 1, true)
await scenario([500], 1, true)
console.log('PASS: six WebUI retry/body-drain/timeout-cleanup scenarios')
