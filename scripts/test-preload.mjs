import assert from 'node:assert/strict'
import { existsSync, readFileSync } from 'node:fs'
import { resolve } from 'node:path'
const root = resolve(import.meta.dirname, '..')
const preload = resolve(root, 'out/preload/index.mjs')
const main = resolve(root, 'out/main/index.js')
assert.ok(existsSync(preload), 'electron-vite did not generate out/preload/index.mjs')
assert.ok(existsSync(main), 'electron-vite did not generate out/main/index.js')
assert.match(readFileSync(main, 'utf8'), /preload\\/index\\.mjs/, 'Electron main must load the generated ESM preload')
assert.match(readFileSync(preload, 'utf8'), /acquisition/, 'Preload must expose acquisition API')
console.log('PASS: Electron main points to built ESM preload')
