/**
 * 将 UTF-8 字节偏移转换为 JS 字符串下标。
 * 返回的游标只向前移动，适合按顺序切分分页结果，避免每页从头扫描。
 */
export function createUtf8OffsetCursor(value) {
    const text = String(value || '')
    let byteIndex = 0
    let stringIndex = 0

    return function advanceTo(byteOffset) {
        const target = Math.max(0, Number(byteOffset) || 0)
        while (stringIndex < text.length && byteIndex < target) {
            const codePoint = text.codePointAt(stringIndex)
            if (codePoint <= 0x7F) byteIndex += 1
            else if (codePoint <= 0x7FF) byteIndex += 2
            else if (codePoint <= 0xFFFF) byteIndex += 3
            else byteIndex += 4
            stringIndex += codePoint > 0xFFFF ? 2 : 1
        }
        return stringIndex
    }
}
