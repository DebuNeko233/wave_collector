import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const code = readFileSync(resolve(root, 'native/src/hantek_device.h'), 'utf8')
const preview = readFileSync(resolve(root, 'native/src/main.cpp'), 'utf8')
const ui = readFileSync(resolve(root, 'src/renderer/src/App.vue'), 'utf8')

// QTDSO and VCDSO demos initialize sampling before relay/trigger controls.
const calls = [
  'dsoInitHard(deviceIndex_)',
  'dsoHTADCCHModGain(deviceIndex_, 4)',
  'dsoHTSetSampleRate(deviceIndex_, YT_NORMAL',
  'dsoHTSetCHAndTrigger(deviceIndex_',
  'dsoHTSetRamAndTrigerControl(deviceIndex_'
]
let previous = -1
for (const call of calls) {
  const position = code.indexOf(call)
  assert.ok(position > previous, `Expected vendor sample initialization ordering: ${call}`)
  previous = position
}
assert.match(code, /dsoHTStartCollectData\(deviceIndex_,\s*1\)/)
assert.match(code, /dsoHTGetState\(deviceIndex_\)/)
assert.match(code, /state\s*&\s*0x02/)
assert.match(code, /dsoHTGetData\(deviceIndex_/)
assert.match(preview, /\\\"frames\\\"/)
assert.ok(!ui.includes('sidebar-spacer'), 'Old admin dashboard layout still present')
console.log('PASS: QTDSO/VCDSO normal capture sequence, preview frame count, and new instrument layout')