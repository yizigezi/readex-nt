// test_support.hpp — 测试小工具：临时目录 / 测试文件 / fd 计数（PR-2）
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_TEST_SUPPORT_HPP
#define READEX_TEST_SUPPORT_HPP

#include <cstdio>
#include <cstdlib>
#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace testsup {

// 测试临时目录：READEX_TEST_TMPDIR 环境变量优先，否则 ./readex_test_tmp。
// 每个用例自行清理自己的文件。
inline std::string tmpDir()
{
    const char* env = std::getenv("READEX_TEST_TMPDIR");
    std::string dir = (env && *env) ? env : "./readex_test_tmp";
    ::mkdir(dir.c_str(), 0755); // 已存在不报错
    return dir;
}

inline std::string writeTestFile(const std::string& name, const std::string& bytes)
{
    std::string path = tmpDir() + "/" + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f != nullptr) {
        if (!bytes.empty())
            std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
    return path;
}

inline void removeTestFile(const std::string& path)
{
    ::unlink(path.c_str());
}

// 当前进程打开的 fd 数（遍历 /proc/self/fd；计数目录自身占一个 fd，
// 成对调用比较差值即可）。/proc 不可用时返回 -1。
inline int openFdCount()
{
    DIR* d = ::opendir("/proc/self/fd");
    if (d == nullptr)
        return -1;
    int n = 0;
    while (::readdir(d) != nullptr)
        ++n;
    ::closedir(d);
    return n;
}

} // namespace testsup

#endif // READEX_TEST_SUPPORT_HPP
