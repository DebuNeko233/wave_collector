import { mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { tmpdir } from 'node:os'
import { spawnSync } from 'node:child_process'
import assert from 'node:assert/strict'
const root = resolve(import.meta.dirname, '..')
const bin = join(root, 'resources', 'bin', process.platform === 'win32' ? 'zw_acq.exe' : 'zw_acq')
const temp = mkdtempSync(join(tmpdir(), 'zw-acq-test-'))
const run = (args) => {
  const p = spawnSync(bin, args, { cwd: root, encoding: 'utf8', timeout: 15000 })
  assert.equal(p.status, 0, `Native failed:\n${p.stdout}\n${p.stderr}`)
  return p.stdout
}
try {
  const logs = run(['--mode','record','--source','mock','--rate','20000','--mask','3','--max-seconds','1','--segment-seconds','1','--segment-mb','1','--output',temp,'--unit','mV','--names','X|Y|x2|y2'])
  const start = logs.split('\n').map(s => { try { return JSON.parse(s) } catch { return null } }).find(x => x?.type === 'started')
  const stop = logs.split('\n').map(s => { try { return JSON.parse(s) } catch { return null } }).find(x => x?.type === 'stopped')
  assert.ok(start?.folder && stop?.written > 0)
  assert.equal(stop.captured, stop.written)
  const files = readdirSync(start.folder).filter(f => f.endsWith('.zwraw'))
  assert.ok(files.length >= 1)
  const metadata = JSON.parse(readFileSync(join(start.folder,'metadata.json'),'utf8'))
  assert.equal(metadata.channel_mask, 3)
  assert.equal(metadata.canonical_unit, 'V')
  assert.equal(metadata.total_frames, stop.written)
  const exportDir = join(temp, 'export')
  const exp = run(['--mode','export','--input',start.folder,'--output',exportDir,'--mask','3','--unit','mV','--points-per-file','2000','--start','0','--count','5000'])
  assert.match(exp, /export_done/)
  const csvFiles = readdirSync(exportDir).filter(f => f.endsWith('.csv'))
  assert.equal(csvFiles.length, 3)
  const first = readFileSync(join(exportDir, csvFiles[0]),'utf8')
  assert.ok(first.startsWith('meta_key,meta_value,sample_index,X1,Y1\n'))
  assert.match(first, /sample_rate_hz,20000/)
  assert.match(first, /amplitude_unit,mV/)
  assert.match(first, /\n,,0,[\d.-]+,[\d.-]+/)
  console.log('PASS: continuous mock recording, segmented native storage, metadata, XY-compatible CSV export')
} finally { rmSync(temp, { recursive: true, force: true }) }
