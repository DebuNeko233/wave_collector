/// <reference types="vite/client" />
import type { AcquisitionApi } from '../../preload/index'
declare global {
  interface Window { acquisition: AcquisitionApi }
}
export {}
