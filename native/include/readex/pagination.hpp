// pagination.hpp — 分页引擎接口（PR-4 实现，本次重构重点）
//
// 当前仅接口声明，无实现。任务范围见计划书 §3.5：
//   - 可用阅读区域由 Vue 层按真实布局量出后传入，不再用固定屏幕比例估算
//   - 中英混排、标点、行高/字距/段距/边距都要参与计算
//   - 字符宽度测量与分页算法解耦：第一阶段用可配置估算模型，
//     设备若能提供字体测量接口则优先接真实测量数据
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_PAGINATION_HPP
#define READEX_PAGINATION_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace readex {

struct PaginationConfig {
    float width = 0;  // 可用阅读区域宽（px，由 UI 实测传入）
    float height = 0; // 可用阅读区域高
    float fontSize = 20;
    float lineHeight = 1.5f; // 倍数
    float letterSpacing = 0;
    float paragraphSpacing = 0;

    float paddingTop = 0;
    float paddingBottom = 0;
    float paddingLeft = 0;
    float paddingRight = 0;
};

struct PageInfo {
    uint64_t startOffset = 0; // 在传入 content 中的字节偏移（含）
    uint64_t endOffset = 0;   // 不含
    size_t pageIndex = 0;
};

class PaginationEngine {
public:
    // 同一 content + 同一 config 分页结果必须稳定；不丢字、不重复。
    static std::vector<PageInfo> paginate(
        const std::string& content,
        const PaginationConfig& config);
};

} // namespace readex

#endif // READEX_PAGINATION_HPP
