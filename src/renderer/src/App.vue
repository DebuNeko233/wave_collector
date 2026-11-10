<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Connection, FolderOpened, VideoPlay, VideoPause, Download, Scissor, InfoFilled, DataLine } from '@element-plus/icons-vue'
import type { AcqEvent, RecordingConfig, ExportConfig } from '../../preload/index'
import { supportedSampleRates, preferredSampleRate } from './sampleRates'

type Channel = { id: number, enabled: boolean, name: string, color: string }
const channels = reactive<Channel[]>([
  { id: 0, enabled: true, name: 'X1', color: '#4b9ee9' },
  { id: 1, enabled: true, name: 'Y1', color: '#a184f5' },
  { id: 2, enabled: false, name: 'X2', color: '#4ec9a7' },
  { id: 3, enabled: false, name: 'Y2', color: '#e9ae63' }
])
const form = reactive<RecordingConfig>({
  source: 'mock', output: '', mask: 3, rate: 1000000,
  segmentSeconds: 60, segmentMb: 128, unit: 'mV', names: channels.map(c => c.name)
})
const splitter = reactive<ExportConfig>({
  input: '', output: '', mask: 3, start: 0, count: 0,
  pointsPerFile: 100000, unit: 'mV'
})
const tab = ref('capture')
const live = ref(false)
const stopping = ref(false)
const exporting = ref(false)
const nativeAvailable = ref(true)
const actualRate = ref(0)
const captured = ref(0)
const written = ref(0)
const backlog = ref(0)
const sessionPath = ref('')
const continuity = ref('')
const history = ref<{ time: string, message: string, level: 'info' | 'error' | 'warn' }[]>([])
const selected = computed(() => channels.filter(c => c.enabled))
const selectedMask = computed(() => channels.reduce((mask, c) => mask | (c.enabled ? (1 << c.id) : 0), 0))
const rateOptions = ref<{ label: string, value: number }[]>([])
const rateNote = ref('等待查询设备支持的采样率')
const exportSelections = ref<number[]>([0, 1])
const rateLabel = (r: number) => r >= 1000000
  ? `${Number((r / 1000000).toFixed(3))} MSa/s`
  : r >= 1000 ? `${Number((r / 1000).toFixed(3))} kSa/s` : `${r} Sa/s`
function refreshRates() {
  const mask = selectedMask.value
  const rates = supportedSampleRates(form.source, mask)
  rateOptions.value = rates.map(value => ({ label: rateLabel(value), value }))
  if (rates.length && !rates.includes(form.rate)) form.rate = preferredSampleRate(rates)
  rateNote.value = mask === 0 ? '请至少启用一个通道' : rates.length
    ? '按 HT6004BX SDK 说明书时基表选择；实际采样率以设备回读为准'
    : '未找到有效采样率'
}
watch([() => form.source, selectedMask], () => { if (!live.value) refreshRates() })

const displayedRate = computed(() => {
  const r = actualRate.value || form.rate
  if (r >= 1000000) return (r / 1000000).toFixed(2) + ' MSa/s'
  return (r / 1000).toFixed(r % 1000 ? 2 : 0) + ' kSa/s'
})
const formatInt = (n: number) => Math.floor(n).toLocaleString('zh-CN')
function log(message: string, level: 'info' | 'error' | 'warn' = 'info') {
  history.value.unshift({ time: new Date().toLocaleTimeString('zh-CN', { hour12: false }), message, level })
  history.value = history.value.slice(0, 80)
}
const canvasRefs = new Map<number, HTMLCanvasElement>()
const previewCache = ref<[number, number][][]>([])
let previewChannels: number[] = [0, 1]
let framePending = false
function updateCanvasRef(index: number, el: unknown) {
  if (el instanceof HTMLCanvasElement) canvasRefs.set(index, el)
  else canvasRefs.delete(index)
}
function drawPreview() {
  framePending = false
  for (let j = 0; j < previewChannels.length; ++j) {
    const id = previewChannels[j]
    const canvas = canvasRefs.get(id)
    const data = previewCache.value[j]
    if (!canvas || !data?.length) continue
    const width = Math.max(100, canvas.clientWidth)
    const height = Math.max(100, canvas.clientHeight)
    const ratio = window.devicePixelRatio || 1
    canvas.width = Math.round(width * ratio)
    canvas.height = Math.round(height * ratio)
    const ctx = canvas.getContext('2d')
    if (!ctx) continue
    ctx.scale(ratio, ratio)
    ctx.fillStyle = '#101c2b'; ctx.fillRect(0, 0, width, height)
    const gridLeft = 38, gridRight = 12, gridTop = 10, gridBottom = 22
    const chartW = width - gridLeft - gridRight, chartH = height - gridTop - gridBottom
    ctx.strokeStyle = '#2a3b4d'; ctx.lineWidth = 1
    for (let k=0;k<=8;k++) { const x=gridLeft + chartW*k/8; ctx.beginPath();ctx.moveTo(x,gridTop);ctx.lineTo(x,gridTop+chartH);ctx.stroke() }
    for (let k=0;k<=4;k++) { const y=gridTop+chartH*k/4;ctx.beginPath();ctx.moveTo(gridLeft,y);ctx.lineTo(gridLeft+chartW,y);ctx.stroke() }
    const mult = form.unit === 'mV' ? 1000 : 1
    const values = data.flatMap(([lo, hi]) => [lo*mult,hi*mult])
    const ymin = Math.min(...values), ymax = Math.max(...values)
    const center = (ymin+ymax)/2
    const span = Math.max((ymax-ymin)*1.15,.001)
    const y = (v: number) => gridTop+chartH/2-(v-center)*chartH/span
    ctx.strokeStyle = channels[id].color;ctx.lineWidth = 1.35
    ctx.beginPath()
    for (let k=0;k<data.length;k++) {
      const x=gridLeft+chartW*k/Math.max(1,data.length-1)
      ctx.moveTo(x,y(data[k][0]*mult)); ctx.lineTo(x,y(data[k][1]*mult))
    }
    ctx.stroke()
    ctx.font='11px ui-monospace, SFMono-Regular, Consolas, monospace'
    ctx.fillStyle='#8498af'
    ctx.fillText(`${(center + span/2).toFixed(1)} ${form.unit}`, 4, 19)
    ctx.fillText(`${(center - span/2).toFixed(1)}`, 4, height-15)
  }
}
function scheduleDraw() {
  if (!framePending) { framePending = true; requestAnimationFrame(drawPreview) }
}
function handleEvent(event: AcqEvent) {
  switch (event.type) {
    case 'started':
      live.value = true; stopping.value = false
      sessionPath.value = event.folder || ''
      actualRate.value = event.rate || 0
      continuity.value = event.continuity || ''
      log(`采集已开始：${sessionPath.value}`)
      if (continuity.value.includes('UNVERIFIED')) log('厂商 SDK 触发窗口之间的连续性未经验证', 'warn')
      break
    case 'stats':
      captured.value = event.captured || 0
      written.value = event.written || 0
      backlog.value = event.pending_blocks || 0
      break
    case 'preview':
      previewCache.value = event.data || []
      scheduleDraw()
      break
    case 'stopped':
      captured.value = event.captured || 0;written.value = event.written || 0
      log(`完成写盘：${formatInt(written.value)} 帧`)
      live.value = false;stopping.value = false
      break
    case 'process_exit':
      live.value = false;stopping.value = false
      if (event.code !== 0) log(`采集进程异常结束（退出码 ${event.code}）`,'error')
      break
    case 'error':
      log(event.message || '未知错误','error'); ElMessage.error(event.message || '采集失败')
      break
    case 'export_done': log(`导出完成：${event.files} 个 CSV，共 ${formatInt(event.frames || 0)} 帧`); break
    case 'log': log(event.message || '原生进程消息','warn');break
  }
}
const bridgeReady = ref(false)
let unsubscribe: (() => void) | undefined
onMounted(async () => {
  // Show manual-based rates even when preload/driver/device is unavailable.
  refreshRates()
  const saved = localStorage.getItem('zhuru-acq-ui-settings')
  if (saved) {
    try {
      const obj = JSON.parse(saved)
      if (obj.names?.length === 4) for (let i = 0; i < 4; i++) channels[i].name = String(obj.names[i]).slice(0, 60)
      if (obj.output) form.output = obj.output
    } catch { /* ignore invalid saved settings */ }
  }
  window.addEventListener('resize', scheduleDraw)
  const api = window.acquisition
  if (!api || typeof api.status !== 'function' || typeof api.onEvent !== 'function') {
    nativeAvailable.value = false
    log('Electron preload 未加载。请重新构建并从 Electron 启动应用。', 'error')
    return
  }
  bridgeReady.value = true
  unsubscribe = api.onEvent(handleEvent)
  try {
    const status = await api.status()
    nativeAvailable.value = status.available
    if (!form.output) form.output = status.defaultOutput
    splitter.output = form.output
  } catch (e) {
    nativeAvailable.value = false
    log(String(e), 'error')
  }
})

onBeforeUnmount(() => { unsubscribe?.();window.removeEventListener('resize',scheduleDraw) })
async function pickOutput() { if (!bridgeReady.value) return ElMessage.error('Electron 桥接不可用'); const p=await window.acquisition.chooseDirectory();if(p)form.output=p }
async function pickSplitterInput() { if (!bridgeReady.value) return ElMessage.error('Electron 桥接不可用'); const p=await window.acquisition.chooseInput();if(p)splitter.input=p }
async function pickSplitterOutput() { if (!bridgeReady.value) return ElMessage.error('Electron 桥接不可用'); const p=await window.acquisition.chooseDirectory();if(p)splitter.output=p }
async function start() {
  if (!bridgeReady.value) return ElMessage.error('Electron 桥接不可用')
  const mask = selectedMask.value
  if (!mask) return ElMessage.warning('至少选择一个采集通道')
  if (!form.output) return ElMessage.warning('请选择保存路径')
  if (!rateOptions.value.some(item => item.value === form.rate)) return ElMessage.warning('请先选择设备支持的采样率档位')
  if (channels.some(c => !c.name.trim() || c.name.includes('|'))) return ElMessage.warning('通道名称不能为空且不能包含 |')
  form.mask = mask; form.names = channels.map(c => c.name)
  previewChannels = selected.value.map(c => c.id)
  previewCache.value = []
  await nextTick()
  captured.value = 0; written.value = 0; backlog.value = 0; actualRate.value = 0
  try {
    localStorage.setItem('zhuru-acq-ui-settings', JSON.stringify({ names: form.names, output: form.output }))
    await window.acquisition.record({ ...form, names: [...form.names] })
    live.value = true;log('已发送开始采集命令，正在初始化设备…')
  } catch (e) { log(String(e),'error');ElMessage.error(String(e)) }
}
async function stop() {
  if (!bridgeReady.value) return ElMessage.error('Electron 桥接不可用')
  if (!live.value || stopping.value) return
  stopping.value = true
  try { await window.acquisition.stop();log('已请求停止，原生进程正在冲刷缓冲数据…') }
  catch (e) { stopping.value=false;ElMessage.error(String(e)) }
}
async function exportData() {
  if (!bridgeReady.value) return ElMessage.error('Electron 桥接不可用')
  if (!splitter.input || !splitter.output) return ElMessage.warning('请选择输入记录和导出目录')
  splitter.mask = exportSelections.value.reduce((mask, id) => mask | (1 << id), 0)
  if (!splitter.mask) return ElMessage.warning('请选择输出通道')
  exporting.value = true
  try {
    const answer = await window.acquisition.exportCsv({ ...splitter })
    ElMessage.success(`已导出 ${answer.files || 0} 个 CSV 文件`)
  } catch(e) { log(String(e),'error');ElMessage.error(String(e)) }
  finally { exporting.value=false }
}
async function warnHantek() {
  if (form.source !== 'hantek') return
  await ElMessageBox.alert('HT6004BX 的 NORMAL 模式每次读取的是一个硬件触发窗口，跨窗口是否无间断、无重复和无丢点尚未验证。请通过实机标定。','厂商 SDK 采集提示', { type: 'warning' })
}
</script>

<template>
  <div class="app-shell">
    <aside class="side-bar">
      <div class="brand"><span class="brand-mark"><DataLine :size="23" /></span><strong>WAVE <span>COLLECTOR</span></strong></div>
      <nav class="side-navigation" aria-label="主导航">
        <button class="nav-link" :class="{ active: tab === 'capture' }" @click="tab = 'capture'"><Connection :size="18" /> 连续采集</button>
        <button class="nav-link" :class="{ active: tab === 'splitter' }" @click="tab = 'splitter'"><Scissor :size="18" /> 记录分割器</button>
      </nav>
      <div class="sidebar-spacer"></div>
      <div class="side-footer">v0.1.0</div>
    </aside>
    <div class="main-content">
      <header class="top-bar">
        <h1>{{ tab === 'capture' ? '连续采集' : '记录分割器' }}</h1>
        <div class="top-right">
          <span v-if="tab === 'capture'" class="top-rate" :title="actualRate ? '设备实际采样率' : '预设采样率，尚未由设备确认'">{{ displayedRate }}</span>
          <span class="status"><i class="status-dot" :class="{ working: live }"></i>{{ live ? (stopping ? '正在停止' : '采集中') : '空闲' }}</span>
        </div>
      </header>
      <main class="page-scroll">
        <template v-if="tab === 'capture'">
          <div v-if="!bridgeReady" class="warning-banner">Electron 桥接不可用，请重启桌面应用。</div><div v-else-if="!nativeAvailable" class="warning-banner">缺少采集程序 zw_acq。请运行 <code>pnpm build:native</code>。</div>
          <div class="stats-grid">
            <div class="stat"><span>已采集</span><strong>{{ formatInt(captured) }}</strong></div>
            <div class="stat"><span>已写入</span><strong>{{ formatInt(written) }}</strong></div>
            <div class="stat"><span>待写缓冲</span><strong>{{ backlog }} <em>/ 128</em></strong></div>
          </div>
          <div class="workspace-grid">
            <section class="panel chart-panel">
              <div class="panel-title"><h2>实时波形</h2></div>
              <div class="wave-list">
                <div v-for="channel of selected" :key="channel.id" class="wave-card">
                  <div class="wave-label"><span class="wave-indicator" :style="{ backgroundColor: channel.color }"></span><b>{{ channel.name }}</b><small>CH{{ channel.id + 1 }}</small><span>{{ form.unit }}</span></div>
                  <canvas :ref="el => updateCanvasRef(channel.id, el)" class="wave-canvas"></canvas>
                </div>
                <div v-if="selected.length === 0" class="empty-chart">选择采集通道以预览波形</div>
              </div>
              <p v-if="continuity.includes('UNVERIFIED')" class="safety-note"><InfoFilled :size="15"/> 旧 DLL 的跨窗口连续性尚未验证</p>
            </section>
            <div class="controls-column">
              <section class="panel configuration">
                <div class="panel-title"><h2>采集设置</h2></div>
                <div class="control-group"><label>设备</label><el-select v-model="form.source" :disabled="live" @change="warnHantek"><el-option label="模拟设备" value="mock"/><el-option label="HT6004BX（厂商 SDK）" value="hantek"/></el-select></div>
                <div class="control-grid">
                  <div class="control-group"><label>采样率</label><el-select v-model="form.rate" :disabled="live || !rateOptions.length" placeholder="查询设备档位"><el-option v-for="option in rateOptions" :key="option.value" :label="option.label" :value="option.value"/></el-select></div>
                  <div class="control-group"><label>单位</label><el-segmented v-model="form.unit" :options="['V', 'mV']" :disabled="live" block/></div>
                </div>
                <div class="control-group"><label>保存位置</label><div class="input-with-button"><el-input v-model="form.output" :disabled="live" placeholder="选择文件夹"/><el-button :disabled="live" :icon="FolderOpened" aria-label="选择保存目录" @click="pickOutput"/></div></div>
                <div class="control-grid"><div class="control-group"><label>分段时长 (s)</label><el-input-number v-model="form.segmentSeconds" :disabled="live" :min="1" :max="3600" controls-position="right"/></div><div class="control-group"><label>分段大小 (MB)</label><el-input-number v-model="form.segmentMb" :disabled="live" :min="1" :max="4096" controls-position="right"/></div></div>
                <p v-if="form.source === 'hantek'" class="device-warning"><InfoFilled /> NORMAL 模式尚未验证无间断采集</p>
                <p v-else-if="rateNote && !rateOptions.length && !rateLoading" class="device-warning"><InfoFilled /> {{ rateNote }}</p>
                <div class="action-row"><el-button v-if="!live" type="primary" size="large" :icon="VideoPlay" :disabled="!nativeAvailable || !bridgeReady || !rateOptions.length" @click="start">开始采集</el-button><el-button v-else type="danger" size="large" :icon="VideoPause" :loading="stopping" @click="stop">停止并保存</el-button></div>
              </section>
              <section class="panel channel-panel">
                <div class="panel-title"><h2>通道</h2><span class="panel-count">{{ selected.length }} / 4</span></div>
                <div v-for="channel of channels" :key="channel.id" class="channel-row"><el-switch v-model="channel.enabled" :disabled="live" :aria-label="`启用 CH${channel.id+1}`"/><span class="channel-num">CH{{ channel.id + 1 }}</span><el-input v-model="channel.name" :disabled="live" maxlength="60" size="small" :aria-label="`通道 ${channel.id+1} 名称`"/><span class="wave-indicator" :style="{ backgroundColor: channel.color }"></span></div>
              </section>
            </div>
          </div>
          <section class="panel log-panel"><div class="panel-title"><h2>事件</h2><span v-if="sessionPath" class="session-path" :title="sessionPath">{{ sessionPath }}</span></div><div class="event-list"><div v-if="!history.length" class="event-placeholder">暂无事件</div><div v-for="(item,index) in history.slice(0,8)" :key="index" class="event-row" :class="item.level"><time>{{ item.time }}</time><span>{{ item.message }}</span></div></div></section>
        </template>
        <template v-else>
          <div class="splitter-grid">
            <section class="panel">
              <div class="panel-title"><h2>文件</h2></div>
              <div class="control-group"><label>输入记录</label><div class="input-with-button"><el-input v-model="splitter.input" placeholder=".zwraw 文件或记录目录"/><el-button :icon="FolderOpened" aria-label="浏览输入记录" @click="pickSplitterInput">浏览</el-button></div></div>
              <div class="control-group"><label>输出目录</label><div class="input-with-button"><el-input v-model="splitter.output" placeholder="选择文件夹"/><el-button :icon="FolderOpened" aria-label="浏览输出目录" @click="pickSplitterOutput">浏览</el-button></div></div>
            </section>
            <section class="panel">
              <div class="panel-title"><h2>切分与导出</h2></div>
              <div class="control-group"><label>通道</label><el-checkbox-group v-model="exportSelections" class="export-checks"><el-checkbox v-for="c of channels" :key="c.id" :value="c.id">CH{{ c.id + 1 }} · {{ c.name }}</el-checkbox></el-checkbox-group></div>
              <div class="control-grid"><div class="control-group"><label>起始帧</label><el-input-number v-model="splitter.start" :min="0" :max="9007199254740991" controls-position="right"/></div><div class="control-group"><label>帧数 (0 = 全部)</label><el-input-number v-model="splitter.count" :min="0" :max="9007199254740991" controls-position="right"/><span class="sr-only">0 表示到记录结束</span></div><div class="control-group"><label>单文件帧数</label><el-input-number v-model="splitter.pointsPerFile" :min="256" :max="200000" :step="1000" controls-position="right"/></div><div class="control-group"><label>单位</label><el-segmented v-model="splitter.unit" :options="['V','mV']" block/></div></div>
              <div class="action-row"><el-button type="primary" size="large" :icon="Download" :loading="exporting" @click="exportData">导出 CSV</el-button></div>
            </section>
          </div>
        </template>
      </main>
    </div>
  </div>
</template>