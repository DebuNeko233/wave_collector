import { spawnSync, spawn } from 'node:child_process'
import { resolve } from 'node:path'
const cwd = resolve(import.meta.dirname, '..')
const pnpm = process.platform === 'win32' ? 'pnpm.cmd' : 'pnpm'
const build = spawnSync(process.execPath, ['scripts/build-native.mjs'], { cwd, stdio: 'inherit' })
if (build.status !== 0) process.exit(build.status || 1)
const cmd = process.platform === 'win32' ? 'node_modules\\.bin\\electron-vite.cmd' : 'node_modules/.bin/electron-vite'
const app = spawn(cmd, ['dev'], { cwd, stdio: 'inherit', shell: process.platform === 'win32' })
app.on('exit', (code) => process.exit(code || 0))
