import { spawnSync } from 'node:child_process'
import { mkdirSync, copyFileSync, existsSync, readdirSync } from 'node:fs'
import { join, resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const build = join(root, 'native', 'build')
const binaryName = process.platform === 'win32' ? 'zw_acq.exe' : 'zw_acq'
const run = (cmd, args) => {
  const result = spawnSync(cmd, args, { cwd: root, stdio: 'inherit', shell: false })
  if (result.error) throw result.error
  if (result.status !== 0) throw new Error(`${cmd} failed with exit code ${result.status}`)
}
const config = ['-S', join(root, 'native'), '-B', build]
if (process.platform === 'win32') {
  // Use the default Visual Studio CMake generator; select x64. Users may override
  // with CMAKE_GENERATOR, e.g. "Visual Studio 18 2026".
  const generator = process.env.CMAKE_GENERATOR || ''
  if (!generator || generator.toLowerCase().includes('visual studio')) config.push('-A', 'x64')
  config.push('-DCMAKE_BUILD_TYPE=Release')
} else {
  config.push('-DCMAKE_BUILD_TYPE=Release')
}
run('cmake', config)
run('cmake', ['--build', build, '--config', 'Release', '--parallel'])
const locations = [join(build, 'Release', binaryName), join(build, binaryName)]
const source = locations.find(existsSync)
if (!source) throw new Error(`Compiled ${binaryName} was not found`)
const dest = join(root, 'resources', 'bin')
mkdirSync(dest, { recursive: true })
copyFileSync(source, join(dest, binaryName))
// The user's licensed SDK files are local, never checked in or distributed by default.
// Developers may opt in by supplying vendor/hantek/dll/x64 contents.
if (process.platform === 'win32') {
  const vendor = join(root, 'vendor', 'hantek', 'dll', 'x64')
  if (existsSync(vendor)) {
    for (const name of readdirSync(vendor)) {
      if (name.toLowerCase().endsWith('.dll')) copyFileSync(join(vendor, name), join(dest, name))
    }
  }
}
console.log(`Native executable -> ${join(dest, binaryName)}`)
