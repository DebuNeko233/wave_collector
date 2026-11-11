import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const vue = readFileSync(resolve(root, 'src/renderer/src/App.vue'), 'utf8')
const css = readFileSync(resolve(root, 'src/renderer/src/style.css'), 'utf8')

// Icon components render an SVG sized in em, not a component supporting :size.
// All free-standing SVG components must be inside a sized ElIcon wrapper.
const wrappers = [...vue.matchAll(/<el-icon\b([^>]*)>([\s\S]*?)<\/el-icon>/g)]
assert.equal(wrappers.length, 5, 'Expected brand, 2 nav, event, and SDK icons')
const required = new Map([
  ['DataLine', { px: 24, klass: 'brand-icon' }],
  ['Monitor', { px: 20, klass: 'rail-icon' }],
  ['Scissor', { px: 20, klass: 'rail-icon' }],
  ['InfoFilled', { px: 14, klass: 'sdk-icon' }]
])
for (const [tag, config] of required) {
  const found = wrappers.filter(([, attrs, inner]) =>
    attrs.includes(`class="${config.klass}"`) &&
    attrs.includes(`:size="${config.px}"`) &&
    inner.includes(`<${tag} />`))
  assert.equal(found.length, 1, `Unwrapped or incorrectly sized icon: ${tag}`)
  assert.doesNotMatch(vue, new RegExp(`<${tag}[^>]*:size=`), `Do not set :size on ${tag} SVG component`)
}
const arrow = wrappers.filter(([, attrs, inner]) =>
  attrs.includes('class="events-chevron"') &&
  attrs.includes(':size="14"') &&
  inner.includes('ArrowUp : ArrowDown'))
assert.equal(arrow.length, 1, 'Event chevron must be inside 14px ElIcon')
for (const selector of [
  '.application .el-icon>svg',
  '.brand-symbol .brand-icon',
  '.rail-link .rail-icon',
  '.sdk-caveat .sdk-icon',
  '.events-toggle .events-chevron svg',
  '.application .el-button>.el-icon',
  '.application .el-select__caret'
]) {
  assert.ok(css.includes(selector + '{'), `Missing bounded icon styling: ${selector}`)
}
for (const icon of ['FolderOpened', 'VideoPlay', 'VideoPause', 'Download']) {
  assert.ok(vue.includes(`:icon="${icon}"`), `ElButton icon should use the button icon API: ${icon}`)
}
assert.doesNotMatch(css, /(?:^|\n)\s*(?:svg|\.application svg)\s*\{/, 'Avoid broad global SVG size rules')
console.log('PASS: all app SVG icons have explicit wrappers or bounded Element Plus button sizing')
