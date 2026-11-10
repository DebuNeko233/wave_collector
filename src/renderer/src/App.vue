<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Connection, FolderOpened, VideoPlay, VideoPause, Download, Setting, DataLine, Scissor, InfoFilled } from '@element-plus/icons-vue'
import type { AcqEvent, RecordingConfig, ExportConfig } from '../../preload/index'

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
const rateLoading = ref(false)
const rateNote = ref('等待查询设备支持的采样率')
const exportSelections = ref<number[]>([0, 1])
let rateRequestId = 0
const rateLabel = (r: number) => r >= 1000000
  ? `${Number((r / 1000000).toFixed(3))} MSa/s`
  : r >= 1000 ? `${Number((r / 1000).toFixed(3))} kSa/s` : `${r} Sa/s`
async function refreshRates() {
  const seq = ++rateRequestId
  const mask = selectedMask.value
  rateOptions.value = []
  if (!mask) { rateNote.value = '请至少启用一个通道'; return }
  rateLoading.value = true
  try {
    const cap = await window.acquisition.capabilities(form.source, mask)
    if (seq !== rateRequestId) return
    rateOptions.value = cap.rates.map(value => ({ label: rateLabel(value), value }))
    if (!cap.rates.includes(form.rate)) form.rate = cap.recommended
    rateNote.value = cap.note
    log(`采样档位已更新：${cap.rates.length} 档（${cap.note}）`)
  } catch (e) {
    if (seq !== rateRequestId) return
    rateNote.value = String(e)
    log('读取采样档位失败：'+String(e), 'warn')
  } finally {
    if (seq === rateRequestId) rateLoading.value = false
  }
}
watch([() => form.source, selectedMask], () => { if (!live.value) void refreshRates() })
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
    ctx.fillText('REC · 最近一个采集块的 Min/Max 包络（不是整个文件）',gridLeft+7,height-8)
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
      if (continuity.value.includes('UNVERIFIED')) log('旧版 DLL 不保证采样窗口连续，请勿视为无丢点原始记录', 'warn')
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
let unsubscribe: (() => void) | undefined
onMounted(async () => {
  unsubscribe = window.acquisition.onEvent(handleEvent)
  const saved = localStorage.getItem('zhuru-acq-ui-settings')
  if (saved) {
    try {
      const obj = JSON.parse(saved)
      if (obj.names?.length === 4) for (let i = 0;i<4;i++) channels[i].name = String(obj.names[i]).slice(0,60)
      if (obj.output) form.output = obj.output
    } catch { /* ignore untrusted local settings */ }
  }
  try {
    const status = await window.acquisition.status()
    nativeAvailable.value = status.available
    if (!form.output) form.output = status.defaultOutput
    splitter.output = form.output
    await refreshRates()
  } catch (e) { log(String(e),'error') }
  window.addEventListener('resize', scheduleDraw)
})
onBeforeUnmount(() => { unsubscribe?.();window.removeEventListener('resize',scheduleDraw) })
async function pickOutput() { const p=await window.acquisition.chooseDirectory();if(p)form.output=p }
async function pickSplitterInput() { const p=await window.acquisition.chooseInput();if(p)splitter.input=p }
async function pickSplitterOutput() { const p=await window.acquisition.chooseDirectory();if(p)splitter.output=p }
async function start() {
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
  if (!live.value || stopping.value) return
  stopping.value = true
  try { await window.acquisition.stop();log('已请求停止，原生进程正在冲刷缓冲数据…') }
  catch (e) { stopping.value=false;ElMessage.error(String(e)) }
}
async function exportData() {
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
async function warnLegacy() {
  if (form.source !== 'legacy') return
  await ElMessageBox.alert('当前旧 libwave.dll 提供的是重复读取接口。它尚未通过连续采样、不重不漏、采样时钟校准的现场验证，不能视为生产级无缝连续采集。请先用模拟设备验证导出流程，再联调原厂 SDK。','实验性旧 DLL 采集', { type: 'warning' })
}
</script>

<template>
  <div class="app-shell">
    <aside class="side-bar">
      <div class="brand-mark"><DataLine :size="24" /></div>
      <div class="brand-text"><b>ZHURU <span>WAVE</span></b><small>ACQUISITION STUDIO</small></div>
      <div class="side-navigation">
        <button class="nav-link" :class="{ active: tab === 'capture' }" @click="tab='capture'"><Connection :size="19"/> 连续采集</button>
        <button class="nav-link" :class="{ active: tab === 'splitter' }" @click="tab='splitter'"><Scissor :size="19"/> 记录分割器</button>
      </div>
      <div class="sidebar-spacer"></div>
      <div class="side-note"><span class="side-note-dot"></span> 运行环境 <strong>Windows x64</strong><p>高吞吐写盘交给 C++，波形预览独立降采样。</p></div>
      <div class="side-footer">Zhuru Wave Acquisition <span>v0.1.0</span></div>
    </aside>
    <div class="main-content">
      <header class="top-bar"><div class="top-title"><span>{{ tab === 'capture' ? '连续波形采集' : '离线记录分割器' }}</span><small>{{ tab === 'capture' ? 'Continuous waveform recorder' : 'Recording export & segmentation' }}</small></div>
        <div class="top-right"><span class="status-dot" :class="{ working: live }"></span>{{ live ? (stopping ? '正在停止…' : '采集中') : '空闲' }}<span class="divider-v"></span><span class="environment">MSVC C++ / Electron</span></div>
      </header>
      <main class="page-scroll">
        <template v-if="tab === 'capture'">
          <section class="intro"><div><p class="eyebrow">ACQUISITION WORKSPACE</p><h1>让每一次扫描，都有迹可循。</h1><p>同步记录多通道波形，原生连续写盘，按需导出到 zhuru_wave 分析。</p></div><div class="rate-head"><small>当前采样率</small><strong>{{ displayedRate }}</strong><span>{{ actualRate ? '设备报告值' : '请求值 / 待设备确认' }}</span></div></section>
          <div v-if="!nativeAvailable" class="warning-banner">原生程序 zw_acq 尚未找到，请先运行 <code>pnpm build:native</code>。</div>
          <div class="stats-grid">
            <div class="stat"><span>采集帧数</span><strong>{{ formatInt(captured) }}</strong><small>每帧包含所有选中通道</small></div>
            <div class="stat"><span>已安全写入</span><strong>{{ formatInt(written) }}</strong><small>原生二进制记录</small></div>
            <div class="stat"><span>缓冲队列</span><strong>{{ backlog }} <em>/ 128</em></strong><small>超时将报错并停止</small></div>
            <div class="stat"><span>采集通道</span><strong>{{ selected.length }} <em>/ 4</em></strong><small>同一采样批次对齐</small></div>
          </div>
          <div class="workspace-grid">
            <section class="panel chart-panel">
              <div class="panel-title"><div><h2>实时波形</h2><p>每 100 ms 更新一次显示数据，原始数据直接写盘</p></div><el-tag :type="live ? 'success' : 'info'" effect="plain">{{ live ? 'LIVE' : 'STANDBY' }}</el-tag></div>
              <div class="wave-list">
                <div v-for="channel of selected" :key="channel.id" class="wave-card">
                  <div class="wave-label"><span class="wave-indicator" :style="{ backgroundColor:channel.color }"></span><b>{{channel.name}}</b><small>CH{{channel.id+1}}</small><span>{{form.unit}}</span></div>
                  <canvas :ref="el => updateCanvasRef(channel.id, el)" class="wave-canvas"></canvas>
                </div>
                <div v-if="selected.length === 0" class="empty-chart">请至少启用一个通道</div>
              </div>
              <p class="footnote">Min/Max 包络保留瞬态峰值；该视图不用于证明连续采集无丢点。{{ continuity.includes('UNVERIFIED') ? '当前旧版硬件读取的连续性未校验。' : '' }}</p>
            </section>
            <div class="controls-column">
              <section class="panel configuration"><div class="panel-title"><div><h2><Setting class="title-icon"/> 采集配置</h2><p>支持设备允许的采样速率</p></div></div>
                <div class="control-group"><label>采集设备</label><el-select v-model="form.source" :disabled="live" @change="warnLegacy" style="width:100%"><el-option label="模拟设备（安全演示）" value="mock"/><el-option label="HT6004BX · 旧 DLL（实验性）" value="legacy"/></el-select></div>
                <div class="control-group"><label>采样率档位（设备可调范围）</label><el-select v-model="form.rate" :disabled="live || rateLoading || !rateOptions.length" :loading="rateLoading" style="width:100%" placeholder="请先查询设备采样能力"><el-option v-for="option in rateOptions" :key="option.value" :label="option.label" :value="option.value"/></el-select><small>{{ rateNote }}。实际速率在启动后回读</small></div>
                <div class="control-group"><label>电压显示 / CSV 单位</label><el-segmented v-model="form.unit" :options="['V','mV']" :disabled="live" block/></div>
                <div class="control-group"><label>存储根目录</label><div class="input-with-button"><el-input v-model="form.output" :disabled="live" placeholder="选择采集输出目录"/><el-button :disabled="live" :icon="FolderOpened" @click="pickOutput"/></div></div>
                <div class="control-grid"><div class="control-group"><label>最大分段时长（秒）</label><el-input-number v-model="form.segmentSeconds" :disabled="live" :min="1" :max="3600" controls-position="right"/></div><div class="control-group"><label>最大分段大小（MB）</label><el-input-number v-model="form.segmentMb" :disabled="live" :min="1" :max="4096" controls-position="right"/></div></div>
                <div class="action-row"><el-button v-if="!live" type="primary" size="large" :icon="VideoPlay" :disabled="!nativeAvailable || rateLoading || !rateOptions.length" @click="start">开始连续采集</el-button><el-button v-else type="danger" size="large" :icon="VideoPause" :loading="stopping" @click="stop">停止并保存</el-button></div>
                <p v-if="form.source === 'legacy'" class="device-warning"><InfoFilled/> 旧 DLL 只提供轮询抓取；跨窗口连续性未验证。正式连续采集仍需原厂 SDK 联调。</p>
              </section>
              <section class="panel channel-panel"><div class="panel-title"><div><h2>采集通道</h2><p>支持自由启用和自定义名称</p></div></div>
                <div v-for="channel of channels" :key="channel.id" class="channel-row"><el-switch v-model="channel.enabled" :disabled="live"/><span class="channel-num">CH{{channel.id+1}}</span><el-input v-model="channel.name" :disabled="live" maxlength="60" size="small"/><span class="wave-indicator" :style="{ backgroundColor:channel.color }"></span></div>
              </section>
            </div>
          </div>
          <section class="panel log-panel"><div class="panel-title"><div><h2>采集事件</h2><p>设备、文件和缓冲状态记录</p></div><small>{{ sessionPath || '尚未创建记录' }}</small></div><div class="event-list"><div v-if="!history.length" class="event-placeholder">等待采集事件…</div><div v-for="(item,index) in history.slice(0,8)" :key="index" class="event-row" :class="item.level"><time>{{item.time}}</time><span>{{item.message}}</span></div></div></section>
        </template>
        <template v-else>
          <section class="intro"><div><p class="eyebrow">RECORDING UTILITIES</p><h1>大文件，按需切分。</h1><p>从原生 .zwraw 记录中截取时间段、选择通道，并输出可导入 zhuru_wave 的同步 CSV。</p></div><div class="splitter-symbol"><Scissor :size="44"/></div></section>
          <div class="splitter-grid">
            <section class="panel split-config"><div class="panel-title"><div><h2>输入与输出</h2><p>支持整个采集目录或单个 .zwraw 文件</p></div></div>
              <div class="control-group"><label>输入记录</label><div class="input-with-button"><el-input v-model="splitter.input" placeholder="选择原生记录目录或文件"/><el-button :icon="FolderOpened" @click="pickSplitterInput">浏览</el-button></div></div>
              <div class="control-group"><label>输出目录</label><div class="input-with-button"><el-input v-model="splitter.output" placeholder="选择 CSV 输出目录"/><el-button :icon="FolderOpened" @click="pickSplitterOutput">浏览</el-button></div></div>
              <div class="control-group"><label>导出通道</label><el-checkbox-group v-model="exportSelections" class="export-checks"><el-checkbox v-for="c of channels" :key="c.id" :value="c.id">CH{{c.id+1}} · {{c.name}}</el-checkbox></el-checkbox-group></div>
              <div class="control-grid"><div class="control-group"><label>起始采样帧索引</label><el-input-number v-model="splitter.start" :min="0" :max="9007199254740991" controls-position="right"/></div><div class="control-group"><label>导出帧数（0=全部）</label><el-input-number v-model="splitter.count" :min="0" :max="9007199254740991" controls-position="right"/></div><div class="control-group"><label>每份 CSV 最大帧数</label><el-input-number v-model="splitter.pointsPerFile" :min="256" :max="200000" :step="1000" controls-position="right"/></div><div class="control-group"><label>CSV 电压单位</label><el-segmented v-model="splitter.unit" :options="['V','mV']" block/></div></div>
              <div class="action-row"><el-button type="primary" size="large" :icon="Download" :loading="exporting" @click="exportData">切分并导出 CSV</el-button></div>
            </section>
            <section class="panel split-guide"><div class="panel-title"><div><h2>兼容性说明</h2><p>面向 zhuru_wave 的离线检测与 XY 李萨如</p></div></div>
              <div class="guide-step"><span>01</span><div><strong>保持同步</strong><p>从同一记录读取多路通道，不把不同时间的两个文件拼接成 XY。</p></div></div>
              <div class="guide-step"><span>02</span><div><strong>自动切块</strong><p>CSV 采样索引从 0 重新编号，并保留原始起始索引元数据。单文件最多 200,000 点。</p></div></div>
              <div class="guide-step"><span>03</span><div><strong>明确单位</strong><p>.zwraw 内部固定以 V 为单位保存 float32；导出时转换到 V 或 mV。旧 DLL 电压标定需实测确认。</p></div></div>
              <div class="guide-step"><span>04</span><div><strong>导入现有软件</strong><p>CSV 首行采用 meta_key,meta_value,sample_index,X1,Y1…，可进入现有 XY 读取器。检测阈值如按 raw 定义需重新换算。</p></div></div>
            </section>
          </div>
        </template>
      </main>
    </div>
  </div>
</template>
<script lang="ts">
export default {}
</script>
