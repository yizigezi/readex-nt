import { BasePage } from './base-page.js'
import { logEvent, logError } from './utils/Logger.js'

class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)
    logEvent('app.launch', 'UI logger initialized')
    $falcon.useDefaultBasePageClass(BasePage)
  }

  onShow() {
    super.onShow()
    logEvent('app.show')
  }

  onHide() {
    super.onHide()
    logEvent('app.hide')
  }

  onDestroy() {
    super.onDestroy()
    logEvent('app.destroy')
  }
}

// 与 doge-reader 保持一致：为 Falcon/QuickJS 页面运行环境补齐基础全局对象。
// 某些设备固件的运行环境不会预先提供 window / process。
try {
  globalThis['window'] = {
    requestAnimationFrame,
    cancelAnimationFrame
  }
} catch (err) {
  logError('global.window.init', err)
}

try {
  globalThis['process'] = {
    env: {
      NODE_ENV: 'production'
    }
  }
} catch (err) {
  logError('global.process.init', err)
}

export default App
