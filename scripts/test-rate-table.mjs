import assert from 'node:assert/strict'
import { nominalHantekRate, preferredSampleRate, supportedSampleRates } from '../src/renderer/src/sampleRates.ts'

// SDK timebase 0: S/D/Q modes
assert.equal(nominalHantekRate(0, 1), 1_000_000_000)
assert.equal(nominalHantekRate(0, 2), 500_000_000)
assert.equal(nominalHantekRate(0, 4), 250_000_000)
// SDK 500 ns/div corner case
assert.equal(nominalHantekRate(7, 1), 500_000_000)
assert.equal(nominalHantekRate(7, 3), 250_000_000)
// SDK 200 us and 500 us nominal bins; no exact 1 MSa/s
for (const mask of [1, 3, 7, 15]) {
  const rates = supportedSampleRates('hantek', mask)
  assert.ok(rates.includes(1_250_000) && rates.includes(500_000))
  assert.ok(!rates.includes(1_000_000))
  assert.equal(preferredSampleRate(rates), 1_250_000)
  assert.ok(rates.every((r, i) => i === 0 || r < rates[i-1]))
}
assert.equal(supportedSampleRates('mock', 3)[0], 1_000_000)
assert.deepEqual(supportedSampleRates('hantek', 0), [])
console.log('PASS: offline HT6004BX SDK timebase table and sample rates')
