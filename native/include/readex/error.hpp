// error.hpp — readex 统一错误类型（ErrorCode + Error + Result<T>）
//
// 设计约定（PR-1 起全局遵守）：
//   1. 核心逻辑（native/src/…）不抛出作为控制流的异常，可恢复错误一律走 Result<T>。
//   2. 真正的异常（bad_alloc、契约违背等）允许在核心内部抛出，但绝不允许穿过
//      QuickJS 的 C ABI 边界——binding 层统一在 catch(...) 里用
//      currentExceptionToError() 转成 Error，再转成 JS Error。
//   3. 头文件优先、C++14、零第三方依赖；host 单元测试可独立编译运行。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_ERROR_HPP
#define READEX_ERROR_HPP

#include <exception>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace readex {

// 统一错误码。后续 PR 新增子系统（解码/解析/分页/搜索）时在此扩展，
// 不要在各自模块里另起错误码体系。
enum class ErrorCode {
    Ok = 0,
    InvalidArgument, // 参数非法（含 JS 层类型错误下沉）
    InvalidHandle,   // 句柄/对象已关闭或无效（如 close 后继续访问 Book）
    NotFound,        // 文件/章节/书签不存在
    PermissionDenied,
    IoError,         // 读写失败
    DecodeError,     // 编码识别/转换失败（PR-2）
    ParseError,      // 解析失败（PR-3）
    OutOfMemory,
    Unsupported,     // 平台/格式不支持
    Internal,        // 内部错误（含 C++ 异常转换）
};

// 错误码英文名（诊断用，不面向最终用户文案）
inline const char* errorCodeName(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::Ok: return "Ok";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidHandle: return "InvalidHandle";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::PermissionDenied: return "PermissionDenied";
    case ErrorCode::IoError: return "IoError";
    case ErrorCode::DecodeError: return "DecodeError";
    case ErrorCode::ParseError: return "ParseError";
    case ErrorCode::OutOfMemory: return "OutOfMemory";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::Internal: return "Internal";
    }
    return "Unknown";
}

// 错误值对象：错误码 + 人类可读信息。
// Ok 状态用 code()==Ok 表示（Error 默认构造即 Ok）。
class Error {
public:
    Error() noexcept : code_(ErrorCode::Ok) {}
    Error(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message))
    {
        // Ok 不应携带错误信息；调用方误用时归一到 Internal 语义之外的裸 Ok。
        if (code_ == ErrorCode::Ok)
            message_.clear();
    }

    ErrorCode code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }
    bool ok() const noexcept { return code_ == ErrorCode::Ok; }

    // "InvalidArgument: 参数…"
    std::string toString() const
    {
        if (ok())
            return "Ok";
        std::string s(errorCodeName(code_));
        if (!message_.empty()) {
            s += ": ";
            s += message_;
        }
        return s;
    }

private:
    ErrorCode code_;
    std::string message_;
};

// Result<T> — 成功值或错误，二者必居其一。
//   - 成功: Result<T>::success(value) / Result<T>(ValueTag, ...)
//   - 失败: Result<T>::failure(error) / failure(code, msg)
// 移动语义：移动构造/赋值后，源对象仍 ok()，但其中的值处于「已移动」状态
// （合法但内容未指定），不得再使用其值。析构/再赋值安全。
template <typename T>
class Result {
public:
    using value_type = T;

    static Result success(T value)
    {
        Result r;
        r.ok_ = true;
        new (r.storage()) T(std::move(value));
        return r;
    }

    static Result failure(Error error)
    {
        Result r;
        r.ok_ = false;
        r.error_ = std::move(error);
        return r;
    }

    static Result failure(ErrorCode code, std::string message)
    {
        return failure(Error(code, std::move(message)));
    }

    Result(const Result& other) : ok_(other.ok_), error_(other.error_)
    {
        if (ok_)
            new (storage()) T(*other.ptr());
    }

    Result(Result&& other) noexcept(
        std::is_nothrow_move_constructible<T>::value)
        : ok_(other.ok_), error_(std::move(other.error_))
    {
        if (ok_)
            new (storage()) T(std::move(*other.ptr()));
    }

    Result& operator=(const Result& other)
    {
        if (this == &other)
            return *this;
        if (ok_)
            ptr()->~T();
        ok_ = other.ok_;
        error_ = other.error_;
        if (ok_)
            new (storage()) T(*other.ptr());
        return *this;
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible<T>::value)
    {
        if (this == &other)
            return *this;
        if (ok_)
            ptr()->~T();
        ok_ = other.ok_;
        error_ = std::move(other.error_);
        if (ok_)
            new (storage()) T(std::move(*other.ptr()));
        return *this;
    }

    ~Result()
    {
        if (ok_)
            ptr()->~T();
    }

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 前置条件：ok()。失败时调用是编程错误（契约违背）。
    T& value() { return *ptr(); }
    const T& value() const { return *ptr(); }

    // 前置条件：!ok()。成功时返回 Ok 空错误。
    const Error& error() const noexcept { return error_; }

private:
    Result() noexcept : ok_(false) {}

    T* ptr() noexcept
    {
        return reinterpret_cast<T*>(&storage_);
    }
    const T* ptr() const noexcept
    {
        return reinterpret_cast<const T*>(&storage_);
    }
    void* storage() noexcept { return &storage_; }

    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
    bool ok_;
    Error error_;
};

// Result<void>：只关心成败的操作。
template <>
class Result<void> {
public:
    using value_type = void;

    static Result success() { return Result(Error()); }
    static Result failure(Error error) { return Result(std::move(error)); }
    static Result failure(ErrorCode code, std::string message)
    {
        return Result(Error(code, std::move(message)));
    }

    bool ok() const noexcept { return error_.ok(); }
    explicit operator bool() const noexcept { return ok(); }

    const Error& error() const noexcept { return error_; }

private:
    explicit Result(Error error) : error_(std::move(error)) {}

    Error error_;
};

// —— 异常不跨 ABI ——
// 在 catch(...) 块内调用：把当前正在处理的 C++ 异常转换成 Error。
// 典型用法（binding 层每个 C 入口）：
//   try { ... } catch (...) { return js_throw(ctx, currentExceptionToError()); }
// 保证任何 C++ 异常都不会穿过 QuickJS 的 C ABI。
inline Error currentExceptionToError() noexcept
{
    try {
        throw; // 重新抛出「当前正在处理」的异常
    } catch (const std::exception& e) {
        return Error(ErrorCode::Internal,
                     std::string("C++ 异常: ") + e.what());
    } catch (...) {
        return Error(ErrorCode::Internal, "C++ 异常: 未知类型");
    }
}

} // namespace readex

#endif // READEX_ERROR_HPP
