<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  Monitor, Scissor, DataLine, FolderOpened, VideoPlay, VideoPause,
  Download, ArrowDown, ArrowUp, InfoFilled
} from '@element-plus/icons-vue'
import type { AcqEvent, RecordingConfig, ExportConfig } from '../../preload/index'
import { supportedSampleRates, preferredSampleRate } from './sampleRates'

type Channel = { id: number; enabled: boolean; name: string; color: string }
type LogLevel = 'info' | 'warn' | 'error'

const channels = reactive<Channel[]>([
  { id: 0, enabled: true, name: 'X1', color: '#48c6e8' },
  { id: 1, enabled: true, name: 'Y1', color: '#f1b85d' },
  { id: 2, enabled: false, name: 'X2', color: '#a58af7' },
  { id: 3, enabled: false, name: 'Y2', color: '#6add9a' }
])
const form = reactive<RecordingConfig>({
  source: 'mock', output: '', mask: 3, rate: 1_000_000,
  segmentSeconds: 60, segmentMb: 128, unit: 'mV', names: channels.map(c => c.name)
})
const splitter = reactive<ExportConfig>({
  input: '', output: '', mask: 3, start: 0, count: 0, pointsPerFile: 100000, unit: 'mV'
})

const page = ref<'capture' | 'splitter'>('capture')
const scopeMode = ref<'overlay' | 'stack'>('overlay')
const showLog = ref(false)
const bridgeReady = ref(false)
const nativeAvailable = ref(false)
const live = ref(false)
const stopping = ref(false)
const exporting = ref(false)
const actualRate = ref(0)
const captured = ref(0)
const written = ref(0)
const pending = ref(0)
const elapsed = ref(0)
const continuity = ref('')
const sessionPath = ref('')
const errorText = ref('')
const history = ref<{ at: string; message: string; level: LogLevel }[]>([])
const exportSelections = ref<number[]>([0, 1])

const selected = computed(() => channels.filter(ch => ch.enabled))
const selectedMask = computed(() => channels.reduce((v, ch) => v | (ch.enabled ? 1 << ch.id : 0), 0))
const rates = computed(() => supportedSampleRates(form.source, selectedMask.value))
const rateOptions = computed(() => rates.value.map(rate => ({ value: rate, label: formatRate(rate) })))
const displayRate = computed(() => formatRate(actualRate.value || form.rate))
const statusText = computed(() => stopping.value ? '正在保存' : live.value ? '采集中' : '就绪')
const statusTone = computed(() => stopping.value ? 'busy' : live.value ? 'active' : 'idle')
const previewFrameCount = ref(0)
const previewStart = ref(0)
const preview = ref<[number, number][][]>([])
let previewChannels: number[] = []
let unsubscribe: (() => void) | undefined
let resizeObserver: ResizeObserver | undefined
let framePending = false
const scopeCanvas = ref<HTMLCanvasElement | null>(null)
const scopeViewport = ref<HTMLDivElement | null>(null)

function formatRate(rate: number): string {
  if (rate >= 1e9) return `${Number((rate / 1e9).toFixed(3))} GSa/s`
  if (rate >= 1e6) return `${Number((rate / 1e6).toFixed(3))} MSa/s`
  if (rate >= 1e3) return `${Number((rate / 1e3).toFixed(3))} kSa/s`
  return `${Number(rate.toFixed(2))} Sa/s`
}
function formatCount(n: number): string { return Math.max(0, n).toLocaleString('zh-CN') }
function formatSeconds(n: number): string {
  const s = Math.max(0, Math.floor(n))
  return `${String(Math.floor(s / 3600)).padStart(2, '0')}:${String(Math.floor(s % 3600 / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`
}
function formatDuration(s: number): string {
  if (s >= 1) return `${Number(s.toFixed(2))} s`
  if (s >= 0.001) return `${Number((s * 1000).toFixed(2))} ms`
  return `${Number((s * 1e6).toFixed(2))} μs`
}
function log(message: string, level: LogLevel = 'info') {
  history.value.unshift({ at: new Date().toLocaleTimeString('zh-CN', { hour12: false }), message, level })
  history.value = history.value.slice(0, 100)
  if (level === 'error') errorText.value = message
}
function scheduleDraw() {
  if (framePending) return
  framePending = true
  requestAnimationFrame(drawScope)
}
function drawScope() {
  framePending = false
  const canvas = scopeCanvas.value
  if (!canvas) return
  const bounds = canvas.getBoundingClientRect()
  const width = Math.max(160, bounds.width)
  const height = Math.max(160, bounds.height)
  const dpr = Math.max(1, Math.min(2, window.devicePixelRatio || 1))
  canvas.width = Math.round(width * dpr)
  canvas.height = Math.round(height * dpr)
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
  ctx.fillStyle = '#0a101a'
  ctx.fillRect(0, 0, width, height)
  const left = 22, right = width - 23, top = 22, bottom = height - 29
  const gridWidth = right - left, gridHeight = bottom - top
  ctx.lineWidth = 1
  for (let x = 0; x <= 10; x++) {
    const px = left + x * gridWidth / 10
    ctx.strokeStyle = x === 5 ? 'rgba(108,139,170,.23)' : 'rgba(91,123,149,.115)'
    ctx.beginPath(); ctx.moveTo(px + .5, top); ctx.lineTo(px + .5, bottom); ctx.stroke()
  }
  for (let y = 0; y <= 8; y++) {
    const py = top + y * gridHeight / 8
    ctx.strokeStyle = y === 4 ? 'rgba(108,139,170,.23)' : 'rgba(91,123,149,.115)'
    ctx.beginPath(); ctx.moveTo(left, py + .5); ctx.lineTo(right, py + .5); ctx.stroke()
  }
  ctx.fillStyle = '#60748b'
  ctx.font = '11px Consolas, Menlo, monospace'
  ctx.fillText('0', left, height - 10)
  const windowSeconds = previewFrameCount.value / (actualRate.value || form.rate || 1)
  ctx.textAlign = 'right'
  ctx.fillText(previewFrameCount.value ? formatDuration(windowSeconds) : 'TIME', right, height - 10)
  ctx.textAlign = 'left'
  if (!preview.value.length || !previewFrameCount.value) {
    ctx.fillStyle = '#5d7289'
    ctx.textAlign = 'center'
    ctx.font = '13px "Microsoft YaHei UI", system-ui, sans-serif'
    ctx.fillText('等待采样数据', width / 2, height / 2)
    ctx.textAlign = 'left'
    return
  }
  const available = previewChannels.map((id, index) => ({ id, data: preview.value[index] })).filter(x => x.data?.length)
  const multiplier = form.unit === 'mV' ? 1000 : 1
  let globalMaximum = 0
  if (scopeMode.value === 'overlay') for (const line of available) {
    for (const [minimum, maximum] of line.data) globalMaximum = Math.max(globalMaximum, Math.abs(minimum), Math.abs(maximum))
  }
  globalMaximum = Math.max(globalMaximum, 0.0005)
  available.forEach(({ id, data }, lane) => {
    const laneCount = scopeMode.value === 'stack' ? available.length : 1
    const laneHeight = gridHeight / laneCount
    const laneTop = top + (scopeMode.value === 'stack' ? lane * laneHeight : 0)
    const center = laneTop + laneHeight / 2
    let peak = globalMaximum
    if (scopeMode.value === 'stack') {
      peak = data.reduce((n, [lo, hi]) => Math.max(n, Math.abs(lo), Math.abs(hi)), 0)
      peak = Math.max(peak, .0005)
    }
    const scale = laneHeight * .43 / (peak * 1.12)
    const pointY = (value: number) => center - value * scale
    const color = channels[id].color
    ctx.save()
    ctx.beginPath(); ctx.rect(left, laneTop, gridWidth, laneHeight); ctx.clip()
    ctx.strokeStyle = color
    ctx.globalAlpha = .88
    ctx.lineWidth = 1.2
    ctx.beginPath()
    data.forEach(([lo, hi], index) => {
      const px = left + (index + .5) / data.length * gridWidth
      ctx.moveTo(px, pointY(lo)); ctx.lineTo(px, pointY(hi))
    })
    ctx.stroke()
    ctx.globalAlpha = .45
    ctx.lineWidth = 1
    ctx.beginPath()
    data.forEach(([lo, hi], index) => {
      const px = left + (index + .5) / data.length * gridWidth
      const y = pointY((lo + hi) / 2)
      if (index === 0) ctx.moveTo(px, y)
      else ctx.lineTo(px, y)
    })
    ctx.stroke()
    ctx.restore()
    if (scopeMode.value === 'stack') {
      ctx.fillStyle = color; ctx.font = '11px Consolas, Menlo, monospace'
      ctx.fillText(channels[id].name, left + 10, laneTop + 18)
      ctx.fillStyle = '#73849a';ctx.textAlign = 'right'
      ctx.fillText(`±${Number((peak * multiplier).toFixed(2))} ${form.unit}`, right - 8, laneTop + 18)
      ctx.textAlign = 'left'
      if (lane > 0) {
        ctx.strokeStyle = '#25364a';ctx.beginPath();ctx.moveTo(left, laneTop);ctx.lineTo(right, laneTop);ctx.stroke()
      }
    }
  })
  ctx.fillStyle = '#65798e';ctx.textAlign = 'right'
  ctx.fillText(`${formatCount(previewStart.value)} #`, right - 5, top + 14)
  ctx.textAlign = 'left'
}
function handleEvent(event: AcqEvent & { start_index?: number }) {
  switch (event.type) {
    case 'connecting': log('连接采集设备'); break
    case 'started':
      live.value = true; stopping.value = false; errorText.value = ''
      sessionPath.value = event.folder || ''
      actualRate.value = event.rate || 0
      continuity.value = event.continuity || ''
      log(`采集开始 · ${sessionPath.value}`)
      if (continuity.value.includes('UNVERIFIED')) log('硬件触发窗口间的连续性未经验证', 'warn')
      break
    case 'stats':
      captured.value = event.captured || 0
      written.value = event.written || 0
      pending.value = event.pending_blocks || 0
      elapsed.value = event.elapsed_s || 0
      break
    case 'preview':
      preview.value = event.data || []
      previewStart.value = event.start_index || 0
      previewFrameCount.value = event.frames || 0
      scheduleDraw()
      break
    case 'stopped':
      captured.value = event.captured || 0; written.value = event.written || 0
      live.value = false; stopping.value = false
      log(`写入完成 · ${formatCount(written.value)} 帧`)
      break
    case 'process_exit':
      live.value = false; stopping.value = false
      if (event.code !== 0) log(`采集进程异常结束，代码 ${event.code}`, 'error')
      break
    case 'error':
      log(event.message || '采集错误', 'error')
      ElMessage.error(event.message || '采集错误')
      break
    case 'export_done': log(`导出 ${event.files ?? 0} 份 CSV · ${formatCount(event.frames || 0)} 帧`); break
    case 'log': log(event.message || '系统消息', 'warn'); break
  }
}
watch([rates, selectedMask], () => {
  if (!live.value && rates.value.length && !rates.value.includes(form.rate)) form.rate = preferredSampleRate(rates.value)
}, { immediate: true })
watch([scopeMode, () => form.unit, selectedMask], scheduleDraw)
watch(page, async () => {
  await nextTick()
  resizeObserver?.disconnect()
  if (scopeViewport.value) resizeObserver?.observe(scopeViewport.value)
  scheduleDraw()
})

onMounted(async () => {
  const saved = localStorage.getItem('wave-collector-ui')
  if (saved) {
    try {
      const values = JSON.parse(saved)
      if (Array.isArray(values.names) && values.names.length === 4) channels.forEach((c, i) => c.name = String(values.names[i]).slice(0, 60))
      if (typeof values.output === 'string') form.output = values.output
    } catch { /* ignore invalid saved settings */ }
  }
  window.addEventListener('resize', scheduleDraw)
  if (scopeViewport.value) {
    resizeObserver = new ResizeObserver(scheduleDraw)
    resizeObserver.observe(scopeViewport.value)
  }
  scheduleDraw()
  const api = window.acquisition
  if (!api || typeof api.status !== 'function' || typeof api.onEvent !== 'function') {
    log('Electron 预加载桥接不可用；请启动桌面程序而非直接打开网页', 'error')
    return
  }
  bridgeReady.value = true
  unsubscribe = api.onEvent(handleEvent)
  try {
    const status = await api.status()
    nativeAvailable.value = status.available
    if (!form.output) form.output = status.defaultOutput
    splitter.output = form.output
  } catch (e) { log(String(e), 'error') }
})
onBeforeUnmount(() => {
  unsubscribe?.()
  resizeObserver?.disconnect()
  window.removeEventListener('resize', scheduleDraw)
})

async function pickFolder(target: 'record' | 'export') {
  if (!bridgeReady.value) return ElMessage.error('桌面桥接不可用')
  const path = await window.acquisition.chooseDirectory()
  if (path) {
    if (target === 'record') form.output = path
    else splitter.output = path
  }
}
async function pickInput() {
  if (!bridgeReady.value) return ElMessage.error('桌面桥接不可用')
  const path = await window.acquisition.chooseInput()
  if (path) splitter.input = path
}
async function beginRecord() {
  if (!bridgeReady.value || !nativeAvailable.value) return ElMessage.error('原生采集程序不可用')
  if (!selectedMask.value) return ElMessage.warning('请开启至少一个通道')
  if (!form.output) return ElMessage.warning('请选择记录目录')
  if (!rates.value.includes(form.rate)) return ElMessage.warning('请选择有效采样率')
  if (channels.some(c => !c.name.trim() || c.name.includes('|'))) return ElMessage.warning('通道名不能为空且不能包含 |')
  form.mask = selectedMask.value
  form.names = channels.map(c => c.name.trim())
  previewChannels = selected.value.map(c => c.id)
  preview.value = []; previewFrameCount.value = 0
  captured.value = 0; written.value = 0; pending.value = 0; elapsed.value = 0
  actualRate.value = 0; errorText.value = ''
  await nextTick()
  scheduleDraw()
  try {
    localStorage.setItem('wave-collector-ui', JSON.stringify({ names: form.names, output: form.output }))
    await window.acquisition.record({ ...form, names: [...form.names] })
    live.value = true
  } catch (e) { log(String(e), 'error'); ElMessage.error(String(e)) }
}
async function stopRecord() {
  if (!bridgeReady.value || !live.value || stopping.value) return
  stopping.value = true
  try { await window.acquisition.stop(); log('等待剩余数据写盘') }
  catch (e) { stopping.value = false; log(String(e), 'error'); ElMessage.error(String(e)) }
}
async function exportCsv() {
  if (!bridgeReady.value) return ElMessage.error('桌面桥接不可用')
  if (!splitter.input || !splitter.output) return ElMessage.warning('请设置输入记录和导出目录')
  const mask = exportSelections.value.reduce((m, id) => m | (1 << id), 0)
  if (!mask) return ElMessage.warning('请选择导出通道')
  exporting.value = true
  try {
    const result = await window.acquisition.exportCsv({ ...splitter, mask })
    ElMessage.success(`导出完成 · ${result.files || 0} 份 CSV`)
  } catch (e) { log(String(e), 'error'); ElMessage.error(String(e)) }
  finally { exporting.value = false }
}
async function changeSource() {
  if (form.source !== 'hantek') return
  await ElMessageBox.alert('原厂示例使用触发窗口采集，暂未证明窗口之间无丢点、无重复。采样率为手册标称档位，实际以设备回读为准。',
    'HT6004BX', { type: 'warning', confirmButtonText: '知道了' })
}
</script>

<template>
  <div class="application">
    <aside class="rail">
      <div class="brand-symbol" aria-label="Wave Collector"><el-icon :size="24" class="brand-icon"><DataLine /></el-icon></div>
      <div class="rail-navigation">
        <button class="rail-link" :class="{ selected: page === 'capture' }" title="采集" aria-label="采集" @click="page = 'capture'">
          <el-icon :size="20" class="rail-icon"><Monitor /></el-icon>
        </button>
        <button class="rail-link" :class="{ selected: page === 'splitter' }" title="分割器" aria-label="分割器" @click="page = 'splitter'">
          <el-icon :size="20" class="rail-icon"><Scissor /></el-icon>
        </button>
      </div>
      <div class="rail-bottom">WC</div>
    </aside>

    <div class="app-content">
      <header class="app-topbar">
        <div class="app-heading">
          <h1>{{ page === 'capture' ? '波形采集' : '记录分割' }}</h1>
          <span class="header-divider"></span>
          <span class="hardware-name">{{ form.source === 'hantek' ? 'HT6004BX' : '模拟设备' }}</span>
        </div>
        <div class="topbar-actions">
          <span v-if="page === 'capture'" class="header-rate">{{ displayRate }}</span>
          <span class="capture-state" :class="statusTone"><i></i>{{ page === 'capture' ? statusText : '离线处理' }}</span>
        </div>
      </header>

      <div v-if="page === 'capture'" class="capture-layout">
        <div class="capture-main">
          <div v-if="errorText" class="inline-alert">{{ errorText }} <button aria-label="关闭错误" @click="errorText = ''">×</button></div>
          <div v-if="!bridgeReady || !nativeAvailable" class="inline-alert subtle">
            {{ !bridgeReady ? 'Electron 桥接未加载；请使用桌面程序启动' : '未找到 zw_acq，请先运行 pnpm build:native' }}
          </div>
          <section class="scope-surface">
            <div class="scope-toolbar">
              <div class="scope-label"><span class="live-indicator" :class="{ running: live }"></span>示波视图</div>
              <div class="scope-toolbar-end">
                <div class="trace-legend">
                  <span v-for="ch in selected" :key="ch.id" class="trace-key"><i :style="{ background: ch.color }"></i>{{ ch.name }}</span>
                </div>
                <el-segmented v-model="scopeMode" class="scope-mode" :options="[{ label: '叠加', value: 'overlay' }, { label: '分轨', value: 'stack' }]" size="small" />
              </div>
            </div>
            <div ref="scopeViewport" class="scope-viewport"><canvas ref="scopeCanvas" class="scope-canvas"></canvas></div>
            <div class="scope-foot">
              <span>{{ previewFrameCount ? `${formatCount(previewFrameCount)} 点 / 窗口` : '未开始采集' }}</span>
              <span>{{ form.unit }} <span class="muted">·</span> {{ selected.length }} CH</span>
            </div>
          </section>

          <div class="record-metrics">
            <div class="metric"><small>已采集</small><strong>{{ formatCount(captured) }}</strong><span>frames</span></div>
            <div class="metric"><small>已写入</small><strong>{{ formatCount(written) }}</strong><span>frames</span></div>
            <div class="metric"><small>缓冲队列</small><strong>{{ pending }}</strong><span>/ 128</span></div>
            <div class="metric"><small>记录时长</small><strong>{{ formatSeconds(elapsed) }}</strong></div>
          </div>

          <section class="event-area" :class="{ expanded: showLog }">
            <button class="events-toggle" @click="showLog = !showLog">
              <span>事件记录 <em>{{ history.length }}</em></span>
              <el-icon :size="14" class="events-chevron"><component :is="showLog ? ArrowUp : ArrowDown" /></el-icon>
            </button>
            <div v-if="showLog" class="event-list">
              <div v-if="!history.length" class="events-empty">暂无记录</div>
              <div v-for="(item, index) in history.slice(0, 12)" :key="index" class="event-item" :class="item.level">
                <time>{{ item.at }}</time><span>{{ item.message }}</span>
              </div>
            </div>
          </section>
        </div>

        <aside class="control-aside">
          <div class="control-scroll">
            <section class="settings-group">
              <h2>采集</h2>
              <div class="field"><label>设备</label>
                <el-select v-model="form.source" :disabled="live" @change="changeSource">
                  <el-option label="模拟设备" value="mock" />
                  <el-option label="HT6004BX" value="hantek" />
                </el-select>
              </div>
              <div class="field"><label>采样率</label>
                <el-select v-model="form.rate" :disabled="live || !rates.length" filterable>
                  <el-option v-for="item in rateOptions" :key="item.value" :label="item.label" :value="item.value" />
                </el-select>
              </div>
              <div class="field"><label>电压单位</label>
                <el-segmented v-model="form.unit" :options="['V', 'mV']" :disabled="live" block />
              </div>
              <div v-if="form.source === 'hantek'" class="sdk-caveat"><el-icon :size="14" class="sdk-icon"><InfoFilled /></el-icon>触发窗口采集，连续性未验证</div>
            </section>

            <section class="settings-group">
              <div class="group-heading"><h2>通道</h2><small>{{ selected.length }} / 4</small></div>
              <div v-for="ch in channels" :key="ch.id" class="channel-control" :class="{ off: !ch.enabled }">
                <span class="channel-swatch" :style="{ background: ch.color }"></span>
                <span class="channel-index">CH{{ ch.id + 1 }}</span>
                <el-input v-model="ch.name" size="small" :disabled="live" maxlength="60" :aria-label="`CH${ch.id+1} 名称`" />
                <el-switch v-model="ch.enabled" size="small" :disabled="live" :aria-label="`CH${ch.id+1} 开关`" />
              </div>
            </section>

            <section class="settings-group">
              <h2>记录</h2>
              <div class="field"><label>保存目录</label>
                <div class="path-input"><el-input v-model="form.output" :disabled="live" placeholder="选择记录文件夹" :title="form.output" /><el-button :icon="FolderOpened" :disabled="live" aria-label="选择保存目录" @click="pickFolder('record')" /></div>
              </div>
              <div class="numeric-fields">
                <div class="field"><label>分段时长 <span>s</span></label><el-input-number v-model="form.segmentSeconds" :disabled="live" :min="1" :max="3600" controls-position="right" /></div>
                <div class="field"><label>分段大小 <span>MB</span></label><el-input-number v-model="form.segmentMb" :disabled="live" :min="1" :max="4096" controls-position="right" /></div>
              </div>
            </section>
          </div>
          <div class="capture-actions">
            <el-button v-if="!live" type="primary" size="large" :icon="VideoPlay" :disabled="!bridgeReady || !nativeAvailable || !rates.length || !selected.length" @click="beginRecord">开始采集</el-button>
            <el-button v-else type="danger" size="large" :icon="VideoPause" :loading="stopping" @click="stopRecord">停止并保存</el-button>
            <span v-if="sessionPath" :title="sessionPath" class="record-location">{{ sessionPath }}</span>
            <span v-else class="action-hint">{{ live ? '正在写入' : '设备就绪后可开始' }}</span>
          </div>
        </aside>
      </div>

      <main v-else class="splitter-page">
        <div v-if="errorText" class="inline-alert">{{ errorText }}</div>
        <div class="splitter-layout">
          <section class="split-panel">
            <div class="split-head"><h3>输入 / 输出</h3></div>
            <div class="field"><label>采集记录</label><div class="path-input"><el-input v-model="splitter.input" placeholder=".zwraw 文件或记录目录" :title="splitter.input" /><el-button :icon="FolderOpened" aria-label="选择输入" @click="pickInput" /></div></div>
            <div class="field"><label>导出位置</label><div class="path-input"><el-input v-model="splitter.output" placeholder="CSV 保存目录" :title="splitter.output" /><el-button :icon="FolderOpened" aria-label="选择输出目录" @click="pickFolder('export')" /></div></div>
          </section>
          <section class="split-panel">
            <div class="split-head"><h3>导出选项</h3></div>
            <div class="field"><label>通道</label>
              <el-checkbox-group v-model="exportSelections" class="export-channel-list">
                <el-checkbox v-for="ch in channels" :key="ch.id" :value="ch.id"><span class="checkbox-label"><i :style="{ background: ch.color }"></i>CH{{ ch.id + 1 }}</span></el-checkbox>
              </el-checkbox-group>
            </div>
            <div class="numeric-fields"><div class="field"><label>起始帧</label><el-input-number v-model="splitter.start" :min="0" :max="Number.MAX_SAFE_INTEGER" controls-position="right" /></div>
              <div class="field"><label>导出帧数 <span>0 = 全部</span></label><el-input-number v-model="splitter.count" :min="0" :max="Number.MAX_SAFE_INTEGER" controls-position="right" /></div></div>
            <div class="numeric-fields"><div class="field"><label>每份 CSV 帧数</label><el-input-number v-model="splitter.pointsPerFile" :min="256" :max="200000" controls-position="right" /></div>
              <div class="field"><label>单位</label><el-segmented v-model="splitter.unit" :options="['V', 'mV']" block /></div></div>
          </section>
        </div>
        <div class="splitter-footer"><span>zhuru-wave-v1 格式 · 按相同样本帧导出</span><el-button type="primary" size="large" :icon="Download" :loading="exporting" :disabled="!bridgeReady" @click="exportCsv">导出 CSV</el-button></div>
      </main>

      <footer class="app-statusbar"><span class="status-connection" :class="{ ok: bridgeReady && nativeAvailable }"><i></i>{{ bridgeReady && nativeAvailable ? '采集服务已就绪' : '采集服务未连接' }}</span><span>Wave Collector · HT6004BX SDK</span></footer>
    </div>
  </div>
</template>