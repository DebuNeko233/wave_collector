import { app, BrowserWindow, dialog, ipcMain, shell } from 'electron'
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process'
import { existsSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const dirname = path.dirname(fileURLToPath(import.meta.url))
let window: BrowserWindow | null = null
let recorder: ChildProcessWithoutNullStreams | null = null
let quitting = false
let recording = false

type RecordingConfig = {
  source: 'mock' | 'hantek'
  output: string
  mask: number
  rate: number
  segmentSeconds: number
  segmentMb: number
  unit: 'V' | 'mV'
  names: string[]
}
type ExportConfig = {
  input: string
  output: string
  mask: number
  start: number
  count: number
  pointsPerFile: number
  unit: 'V' | 'mV'
}
const nativePath = (): string => {
  const filename = process.platform === 'win32' ? 'zw_acq.exe' : 'zw_acq'
  return app.isPackaged
    ? path.join(process.resourcesPath, 'bin', filename)
    : path.join(app.getAppPath(), 'resources', 'bin', filename)
}
const numeric = (n: unknown, min: number, max: number, name: string): number => {
  if (typeof n !== 'number' || !Number.isInteger(n) || n < min || n > max) throw new Error(`${name} 必须在 ${min}～${max} 范围内`)
  return n
}
const requirePath = (s: unknown, key: string): string => {
  if (typeof s !== 'string' || !s.trim()) throw new Error(`${key} 不能为空`)
  return path.resolve(s)
}
const event = (payload: object): void => {
  if (window && !window.isDestroyed()) window.webContents.send('acq:event', payload)
}
function parseLines(child: ChildProcessWithoutNullStreams, cb: (x: any) => void): void {
  let stdout = ''
  child.stdout.setEncoding('utf8')
  child.stdout.on('data', (text: string) => {
    stdout += text
    if (stdout.length > 4_000_000) {
      cb({ type: 'error', message: '采集进程输出过大' })
      stdout = ''
      return
    }
    let pos: number
    while ((pos = stdout.indexOf('\n')) !== -1) {
      const line = stdout.slice(0, pos).trim()
      stdout = stdout.slice(pos + 1)
      if (!line) continue
      try { cb(JSON.parse(line)) } catch { cb({ type: 'error', message: `采集进程返回非法数据: ${line.slice(0, 160)}` }) }
    }
  })
  child.stderr.setEncoding('utf8')
  child.stderr.on('data', (text: string) => cb({ type: 'log', message: text.slice(0, 4000) }))
}
function runNative(args: string[], listener: (x: any) => void): Promise<number> {
  const exe = nativePath()
  if (!existsSync(exe)) return Promise.reject(new Error(`找不到原生采集程序 ${exe}，请先运行 pnpm build:native`))
  return new Promise((resolve, reject) => {
    const cp = spawn(exe, args, { cwd: path.dirname(exe), windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] })
    let resolved = false
    parseLines(cp, listener)
    cp.on('error', error => { if (!resolved) { resolved = true; reject(error) } })
    cp.on('close', code => {
      if (!resolved) { resolved = true; resolve(code ?? 1) }
    })
  })
}
const createWindow = (): void => {
  window = new BrowserWindow({
    width: 1450,
    height: 960,
    minWidth: 1100,
    minHeight: 700,
    show: false,
    title: 'Zhuru Wave Acquisition',
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  })
  window.once('ready-to-show', () => window?.show())
  const devUrl = process.env.ELECTRON_RENDERER_URL
  if (devUrl) void window.loadURL(devUrl)
  else void window.loadFile(path.join(dirname, '../renderer/index.html'))
  window.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('https://')) void shell.openExternal(url)
    return { action: 'deny' }
  })
}
app.whenReady().then(() => {
  ipcMain.handle('dialog:directory', async () => {
    const r = await dialog.showOpenDialog(window!, { properties: ['openDirectory', 'createDirectory'], title: '选择目录' })
    return r.canceled ? null : r.filePaths[0]
  })
  ipcMain.handle('dialog:input', async () => {
    const r = await dialog.showOpenDialog(window!, { properties: ['openFile', 'openDirectory'], filters: [{ name: '采集数据', extensions: ['zwraw'] }], title: '选择记录文件或整个采集目录' })
    return r.canceled ? null : r.filePaths[0]
  })
  ipcMain.handle('acq:status', () => ({ recording, available: existsSync(nativePath()), platform: process.platform, defaultOutput: path.join(app.getPath('documents'), 'ZhuruWaveRecordings') }))
  ipcMain.handle('acq:capabilities', async (_, source: 'mock' | 'hantek', mask: number) => {
    if (!['mock','hantek'].includes(source)) throw new Error('未知设备源')
    const args = ['--mode', 'capabilities', '--source', source, '--mask', String(numeric(mask, 1, 15, '通道掩码'))]
    const logs: any[] = []
    const code = await runNative(args, x => logs.push(x))
    if (code !== 0) throw new Error(logs.find(x => x.type === 'error')?.message || '读取设备采样档位失败')
    const cap = logs.find(x => x.type === 'capabilities')
    if (!cap || !Array.isArray(cap.rates_hz) || !cap.rates_hz.length) throw new Error('设备未报告有效采样率档位')
    return { rates: cap.rates_hz as number[], recommended: cap.recommended_hz as number,
      note: cap.note as string }
  })
  ipcMain.handle('acq:record', async (_, cfg: RecordingConfig) => {
    if (recorder || recording) throw new Error('已有采集进程运行中')
    const exe = nativePath()
    if (!existsSync(exe)) throw new Error('原生程序未编译，请运行 pnpm build:native')
    if (!['mock', 'hantek'].includes(cfg.source)) throw new Error('无效的数据源')
    if (!['V', 'mV'].includes(cfg.unit)) throw new Error('无效的电压单位')
    const names = cfg.names
    if (!Array.isArray(names) || names.length !== 4 || names.some(s => typeof s !== 'string' || !s.trim() || s.length > 60 || s.includes('|')))
      throw new Error('请设置四个有效的通道名称，不能使用 |')
    const args = ['--mode', 'record', '--source', cfg.source, '--output', requirePath(cfg.output,'保存目录'),
      '--mask', String(numeric(cfg.mask, 1, 15, '通道')), '--rate', String(numeric(cfg.rate, 100, 1000000000, '采样率')),
      '--segment-seconds', String(numeric(cfg.segmentSeconds, 1, 3600,'分段秒数')),
      '--segment-mb', String(numeric(cfg.segmentMb, 1, 4096,'分段大小')),
      '--unit', cfg.unit, '--names', names.join('|')]
    const child = spawn(exe, args, { cwd: path.dirname(exe), windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] })
    recorder = child
    recording = true
    event({ type: 'connecting' })
    parseLines(child, event)
    child.on('error', e => event({ type:'error', message: e.message }))
    child.on('close', code => {
      recording = false
      recorder = null
      event({ type: 'process_exit', code })
      if (quitting) app.quit()
    })
    return { started: true }
  })
  ipcMain.handle('acq:stop', async () => {
    if (!recorder) return { stopped: true }
    recorder.stdin.write('stop\n')
    return { stopped: false, message: '已请求停止，等待数据写盘完成' }
  })
  ipcMain.handle('acq:export', async (_, cfg: ExportConfig) => {
    if (!['V','mV'].includes(cfg.unit)) throw new Error('无效输出单位')
    const args = ['--mode','export','--input',requirePath(cfg.input,'输入文件'), '--output',requirePath(cfg.output,'输出目录'),
      '--mask',String(numeric(cfg.mask,1,15,'导出通道')),
      '--start',String(numeric(cfg.start,0,Number.MAX_SAFE_INTEGER,'起始点')),
      '--count',String(numeric(cfg.count,0,Number.MAX_SAFE_INTEGER,'导出数量')),
      '--points-per-file',String(numeric(cfg.pointsPerFile,256,200000,'每份点数')),
      '--unit',cfg.unit]
    const logs: any[] = []
    const code = await runNative(args, x => { logs.push(x); event(x) })
    if (code !== 0) throw new Error(logs.find(x => x.type === 'error')?.message || `分割器返回错误码 ${code}`)
    return logs.find(x => x.type === 'export_done') || { type: 'export_done' }
  })
  createWindow()
})
app.on('window-all-closed', () => app.quit())
app.on('before-quit', evt => {
  if (recorder && !quitting) {
    evt.preventDefault()
    quitting = true
    recorder.stdin.write('stop\n')
    event({ type: 'log', message: '正在结束录制并写入文件索引…' })
  }
})