import { contextBridge, ipcRenderer } from 'electron'

export type Source = 'mock' | 'hantek'
export type Unit = 'V' | 'mV'
export interface RecordingConfig {
  source: Source
  output: string
  mask: number
  rate: number
  segmentSeconds: number
  segmentMb: number
  unit: Unit
  names: string[]
}
export interface ExportConfig {
  input: string
  output: string
  mask: number
  start: number
  count: number
  pointsPerFile: number
  unit: Unit
}
export type AcqEvent = {
  type: string
  rate?: number
  folder?: string
  message?: string
  source?: string
  continuity?: string
  captured?: number
  written?: number
  pending_blocks?: number
  elapsed_s?: number
  data?: [number, number][][]
  frames?: number
  files?: number
  output?: string
  code?: number
}
const api = {
  chooseDirectory: (): Promise<string | null> => ipcRenderer.invoke('dialog:directory'),
  chooseInput: (): Promise<string | null> => ipcRenderer.invoke('dialog:input'),
  status: (): Promise<{ recording: boolean, available: boolean, platform: string, defaultOutput: string }> => ipcRenderer.invoke('acq:status'),
  capabilities: (source: Source, mask: number): Promise<{rates: number[], recommended: number, note: string}> => ipcRenderer.invoke('acq:capabilities', source, mask),
  record: (cfg: RecordingConfig): Promise<{ started: boolean }> => ipcRenderer.invoke('acq:record', cfg),
  stop: (): Promise<{ stopped: boolean }> => ipcRenderer.invoke('acq:stop'),
  exportCsv: (cfg: ExportConfig): Promise<{ type: string, files?: number, frames?: number, output?: string }> => ipcRenderer.invoke('acq:export', cfg),
  onEvent: (callback: (event: AcqEvent) => void): (() => void) => {
    const listener = (_: Electron.IpcRendererEvent, event: AcqEvent) => callback(event)
    ipcRenderer.on('acq:event', listener)
    return () => ipcRenderer.removeListener('acq:event', listener)
  }
}
contextBridge.exposeInMainWorld('acquisition', api)
export type AcquisitionApi = typeof api