/**
 * 页面基类：统一管理页面级定时器，页面销毁时自动清理。
 * 事件监听等能力待计划书定稿后补充。
 */
export class BasePage extends $falcon.Page {
  constructor() {
    super()
    this._timers = new Set()
  }

  setTimeout(fn, ms) {
    const token = setTimeout(() => {
      this._timers.delete(token)
      fn()
    }, ms)
    this._timers.add(token)
    return token
  }

  setInterval(fn, ms) {
    const token = setInterval(fn, ms)
    this._timers.add(token)
    return token
  }

  clearTimer(token) {
    clearTimeout(token)
    clearInterval(token)
    this._timers.delete(token)
  }

  onDestroy() {
    for (const token of this._timers) {
      clearTimeout(token)
      clearInterval(token)
    }
    this._timers.clear()
    super.onDestroy()
  }
}
