// pagination.hpp — 分页引擎（PR-4，计划书 §3.5，本次重构重点模块）
//
// —— 偏移语义（PR-5 依赖的关键约定，勿改）——
//   PageInfo.startOffset / endOffset 是「传入 content 字符串内的解码文本偏移」：
//   即 UTF-8 字节偏移，指向 paginate() 收到的那串**已解码章文本**（不是原始
//   文件字节偏移——文件偏移是 ChapterInfo 的语义，两者不可混用）。恒满足：
//     1. 半开区间 [startOffset, endOffset)，恒落在码点边界（不会切开字符）；
//     2. 各页区间首尾相接、无缝无重，拼接 substr 结果 == 原 content；
//     3. pageIndex 从 0 递增。
//   PR-5 的 ReadingState 将存 chapterIndex + textOffset，其中 textOffset 沿用
//   本约定（= 解码章文本内的 UTF-8 字节偏移）；全文搜索（PR-5）返回的匹配
//   偏移同样复用该语义，三者共用一把尺子。
//
// —— 可用阅读区域是显式输入（修「内容挤在屏幕左侧」bug 的关键）——
//   PaginationConfig.width / height 就是 UI 布局量出的**可用阅读区域**像素
//   宽高（不含工具栏等），引擎内部绝不假设屏幕比例、绝不自行推算工具栏宽度。
//   尺寸由 Vue 层按真实布局量出后传入（毛坯阶段 falcon 无测量 API，先用
//   vw/vh 换算占位并在 UI 注释写明「真机尺寸数据到位后校准」）。
//
// —— 字符宽度测量与分页算法解耦 ——
//   分页算法只依赖 TextMeasurer 抽象（码点 -> 字形前进宽度），第一阶段提供
//   EstimatingMeasurer：可配置字符宽度估算模型（CJK 全宽 / ASCII 半宽 /
//   中文标点按全宽或半宽可配，参数在 EstimatorConfig）。
//   已知局限（估算模型，非真机渲染）：
//     - 估算宽度与设备真机字体渲染存在误差（比例系数是近似值，且真机字体
//       的实际 advance 可能随字体文件不同而偏离 0.5em/1em 经验值）；
//     - 不建模 kerning（字距微调）/ 连字（ligature）/ 基线调整；letterSpacing
//       按「每字后加固定像素」参与行宽（与 CSS letter-spacing 行为一致，
//       行末字也计）；
//     - 若设备能提供字体测量接口（如按字体文件查 advance），实现 TextMeasurer
//       注入 paginate() 的重载即可替换，分页算法无需改动。
//
// —— 换行规则（基础版）与明确局限 ——
//   只在「合法断点」处软换行：断点两侧须满足
//     - 行首禁则：句号逗号等收尾标点（，。！？；：、）」』…——）不落行首；
//     - 行尾禁则：引号括号等起始标点（「『（【《"）不落行尾；
//     - 英文词内不断（相邻 ASCII 字母/数字之间无断点）；
//     - 成对连字（—— …… ～～）中间不断；
//     - 空白之后才断（空白跟前行行尾，不落下一行行首）。
//   行超宽时向后回溯到最后一个合法断点（因此避头尾靠「提前换行」实现）。
//   明确局限（不崩、稳定，但不完美）：
//     - 行内完全没有合法断点时退化为强制断行（如超长英文单词被机械切开、
//       极端宽度下禁则被突破）；或把超宽的行首禁则字悬挂在行尾（最多 1 字）；
//     - 连续禁则字符（如「…………」）在极窄行宽下仍可能使禁则字落到行首；
//     - 不做悬挂标点（hanging punctuation）伸出边距、不做避头尾点调整、
//       不做英文连字符（hyphen）断词、不做两端对齐；
//     - \t 按 4 个半角空格宽计（估算法）；非法 UTF-8 字节按 1 字节 1 字符
//       （半角宽）处理，不崩不丢。
//
// —— 与计划书 §3.5 建议接口的偏差及理由 ——
//   1. paginate() 返回 Result<std::vector<PageInfo>>（计划书为裸 vector）：
//      配置非法（宽高/字号非正、边距吃光可用区、NaN/inf 等）走统一 Error
//      体系（InvalidArgument），不靠空列表/崩溃表达，与 book.hpp 同一约定。
//   2. 增加 paginate(content, config, const TextMeasurer&) 重载：测量解耦的
//      落点，未来真机测量数据从这里注入；两重载分页结果只差宽度数据。
//   3. 字段名/结构与计划书一致（width/height/fontSize/lineHeight/
//      letterSpacing/paragraphSpacing/四边边距），另加 estimator 子结构收纳
//      估算参数（计划书允许「进 PaginationConfig 或独立 EstimatorConfig」）。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_PAGINATION_HPP
#define READEX_PAGINATION_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "readex/error.hpp"

namespace readex {

// ---- 字符宽度估算模型参数（EstimatingMeasurer 消费）----
// 宽度 = em 系数 × fontSize；系数必须 > 0 且有限。
struct EstimatorConfig {
    float asciiWidthEm = 0.5f;       // ASCII/半角（U+0020-007E 等）
    float cjkWidthEm = 1.0f;         // CJK 全角（汉字/假名/谚文/全角字母数字）
    float punctuationWidthEm = 1.0f; // 中文标点（，。！？；：「」『』——……）
                                     // 1.0 = 全宽，0.5 = 半宽，可配
};

// 字符宽度类别（估算模型用；公开以便测试与诊断）
enum class CharClass : uint8_t {
    Half,  // 半角 -> asciiWidthEm
    Full,  // 全角 -> cjkWidthEm
    Punct, // 中文标点 -> punctuationWidthEm
};

// 码点归类（纯函数，供 EstimatingMeasurer 与测试使用）。
// 粗粒度近似：增补平面（>= U+10000，含 emoji）统一按 Full 估；未列入的
// BMP 字符按 Half 估。估算误差是已声明的局限。
CharClass classifyCodepoint(uint32_t cp) noexcept;

// 换行规则所需的字符性质（纯函数，供引擎与测试使用）
struct CharTraits {
    bool canStartLine; // false = 行首禁则（，。！？；：、）」』……）
    bool canEndLine;   // false = 行尾禁则（「『（【《" 等起始标点）
    bool isWordChar;   // ASCII 字母/数字：相邻不断
    bool isSpace;      // 空白（含 \t / 全角空格）：空白后才允许软断点
    bool isHardBreak;  // \r / \n：硬换行（段落边界）
    bool isPairChar;   // 成对连字（— … ～ ー 々 等）：与相同字符相邻不断
};

CharTraits codepointTraits(uint32_t cp) noexcept;

// ---- 测量接口（分页算法只依赖它，可替换为真机字体测量实现）----
class TextMeasurer {
public:
    virtual ~TextMeasurer();

    // 单码点字形前进宽度（px，不含 letterSpacing；由引擎另行叠加）。
    // 实现须满足：宽度有限且 >= 0，同输入恒定（分页稳定性依赖它）。
    virtual float codepointWidth(uint32_t cp) const = 0;

    // 便捷：字符串总宽（逐码点求和，不含 letterSpacing）。
    // 不建模 kerning/连字（局限）；text 为 UTF-8。
    virtual float measure(const char* text, size_t bytes) const;
};

// 第一阶段的估算测量器：em 系数模型（见 EstimatorConfig 与头注释局限）。
class EstimatingMeasurer : public TextMeasurer {
public:
    explicit EstimatingMeasurer(float fontSize,
                                const EstimatorConfig& estimator = EstimatorConfig());

    float codepointWidth(uint32_t cp) const override;

    float fontSize() const noexcept { return fontSize_; }
    const EstimatorConfig& estimator() const noexcept { return estimator_; }

private:
    float fontSize_;
    EstimatorConfig estimator_;
};

// ---- 分页配置（计划书 §3.5 结构 + 估算参数）----
// width/height 是 UI 布局量出的可用阅读区域像素宽高（显式输入）。
struct PaginationConfig {
    float width = 0;  // 可用阅读区域宽（px，外部实测传入，必填 > 0）
    float height = 0; // 可用阅读区域高（px，外部实测传入，必填 > 0）
    float fontSize = 20;
    float lineHeight = 1.5f;      // 倍数（行高 = fontSize × lineHeight）
    float letterSpacing = 0;      // px，每字后附加（>= 0）
    float paragraphSpacing = 0;   // px，段落（硬换行）之间附加（>= 0）

    float paddingTop = 0;
    float paddingBottom = 0;
    float paddingLeft = 0;
    float paddingRight = 0;

    EstimatorConfig estimator; // 估算模型参数（默认全宽/半角/标点全宽）
};

// 配置校验：合法返回 Ok，否则 InvalidArgument（带原因）。校验规则：
// width/height/fontSize/lineHeight > 0 且有限；letterSpacing/
// paragraphSpacing/四边边距 >= 0 且有限；边距不得吃光可用区
// （width - 左右 > 0 且 height - 上下 > 0）；估算系数 > 0 且有限。
Error validatePaginationConfig(const PaginationConfig& config);

// ---- 页信息：见头注释「偏移语义」----
struct PageInfo {
    uint64_t startOffset = 0; // 在传入 content（UTF-8）中的字节偏移（含）
    uint64_t endOffset = 0;   // 字节偏移（不含）；各页无缝拼接 == content
    size_t pageIndex = 0;
};

// ---- 分页引擎 ----
// 同一 content + 同一 config 结果稳定（确定性贪心 + 回溯断点，无随机/无
// 平台相关状态；宽度测量须恒定）。不丢字、不重复（区间无缝拼接性质）。
// 空文本 -> 单个空页 [0, 0)（UI 总有页可显示）；纯空白/超长行/单字超宽/
// 极小或极大尺寸均不崩，且每页至少 1 行、页数有限（保证推进）。
class PaginationEngine {
public:
    // 默认用 EstimatingMeasurer（config.fontSize × config.estimator）测量。
    static Result<std::vector<PageInfo>> paginate(const std::string& content,
                                                  const PaginationConfig& config);

    // 注入自定义测量器（真机字体测量数据从这里替换）；config.estimator 被
    // 忽略（宽度由 measurer 提供），lineHeight/letterSpacing/边距照常生效。
    static Result<std::vector<PageInfo>> paginate(const std::string& content,
                                                  const PaginationConfig& config,
                                                  const TextMeasurer& measurer);
};

} // namespace readex

#endif // READEX_PAGINATION_HPP
