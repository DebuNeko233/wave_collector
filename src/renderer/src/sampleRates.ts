/**
 * HT6004BX sampling rates from the vendor SDK manual (timebase table 0..35).
 * 2 ns/div..1000 s/div, using 1-2-5 progression.
 * Source: HT6004BX SDK, SDK_HTHardDLL-EN.pdf, Table 1/2.
 *
 * The table is nominal hardware sample rate, NOT sustainable USB throughput.
 * User-facing selections intentionally do not require a connected device.
 */
export type SampleSource = 'mock' | 'hantek'

const TIMEBASE_SECONDS = [
  2e-9, 5e-9, 10e-9, 20e-9, 50e-9, 100e-9, 200e-9, 500e-9,
  1e-6, 2e-6, 5e-6, 10e-6, 20e-6, 50e-6, 100e-6, 200e-6, 500e-6,
  1e-3, 2e-3, 5e-3, 10e-3, 20e-3, 50e-3, 100e-3, 200e-3, 500e-3,
  1, 2, 5, 10, 20, 50, 100, 200, 500, 1000
] as const

const MOCK_RATES = [1_000_000, 500_000, 250_000, 125_000, 50_000, 20_000, 10_000, 1_000] as const

export function nominalHantekRate(timebaseIndex: number, enabledCount: number): number {
  if (!Number.isInteger(timebaseIndex) || timebaseIndex < 0 || timebaseIndex >= TIMEBASE_SECONDS.length ||
      !Number.isInteger(enabledCount) || enabledCount < 1 || enabledCount > 4) {
    throw new RangeError('Invalid HT6004BX timebase index or channel count')
  }
  // SDK table 2: indices 0..6 use S / D / Q modes.
  if (timebaseIndex <= 6) return enabledCount === 1 ? 1e9 : enabledCount === 2 ? 5e8 : 2.5e8
  // Index 7: even one enabled channel uses the dual-channel 500 MSa/s mode.
  if (timebaseIndex === 7) return enabledCount <= 2 ? 5e8 : 2.5e8
  // SDK table 1: 250 actual samples per horizontal division from 1 us/div.
  return 250 / TIMEBASE_SECONDS[timebaseIndex]
}

export function supportedSampleRates(source: SampleSource, mask: number): number[] {
  if (!Number.isInteger(mask) || mask < 1 || mask > 15) return []
  if (source === 'mock') return [...MOCK_RATES]
  if (source !== 'hantek') return []
  const enabledCount = [0, 1, 2, 3].reduce((n, bit) => n + ((mask >> bit) & 1), 0)
  return [...new Set(TIMEBASE_SECONDS.map((_, i) => Math.round(nominalHantekRate(i, enabledCount)))
    .filter(rate => rate >= 100))].sort((a, b) => b - a)
}

export function preferredSampleRate(rates: number[], targetHz = 1_000_000): number {
  return rates.reduce((closest, value) =>
    Math.abs(value - targetHz) < Math.abs(closest - targetHz) ? value : closest, rates[0] ?? 0)
}
