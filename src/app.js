import { BasePage } from './base-page.js'

class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)
    $falcon.useDefaultBasePageClass(BasePage)
  }

  onShow() {
    super.onShow()
  }

  onHide() {
    super.onHide()
  }

  onDestroy() {
    super.onDestroy()
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
  console.log(err)
}

try {
  globalThis['process'] = {
    env: {
      NODE_ENV: 'production'
    }
  }
} catch (err) {
  console.log(err)
}

export default App
