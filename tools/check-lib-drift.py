#!/usr/bin/env python3
"""libs/ 原生库漂移检查：仓库已提交的 .so vs 源码重编产物。

用途：CI 中先跑 tools/build-core.sh / tools/build-fs-plugin.sh 重编，再跑本脚本。
不一致说明「源码改了但没重编提交」，必须本地重编并提交 libs/ 下产物。

比对做路径归一化：绝对路径只保留 basename，抹掉目录部分——
工具链（zig）安装路径因机器而异，会把编译路径烙进 glibc 桩字符串，
归一化后不影响判定；源码/产物脱节仍必然检出。
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIBS = ["libs/libjsapi_readex.so", "libs/libjsapi_fs.so"]

# 绝对路径样式：/xx/yy/zz 形式的字节串
PATH_RE = re.compile(rb"/(?:[A-Za-z0-9_.\-]+/){1,}[A-Za-z0-9_.\-]+")


def normalize(data: bytes) -> bytes:
    def repl(m):
        p = m.group(0)
        base = p.rsplit(b"/", 1)[-1]
        return b"\x00" * (len(p) - len(base)) + base

    return PATH_RE.sub(repl, data)


def main() -> int:
    failed = 0
    for rel in LIBS:
        committed = subprocess.run(
            ["git", "show", f"HEAD:{rel}"], capture_output=True, cwd=ROOT
        ).stdout
        built_path = ROOT / rel
        if not committed or not built_path.exists():
            print(f"[跳过] {rel}（无已提交版本或未重编）")
            continue
        built = built_path.read_bytes()
        if normalize(committed) == normalize(built):
            print(f"[一致] {rel}（{len(built)} 字节）")
        else:
            print(
                f"[漂移] {rel}: 已提交 {len(committed)} 字节 vs 重编 {len(built)} 字节。\n"
                f"       源码与二进制脱节——请本地跑 tools/build-core.sh / "
                f"tools/build-fs-plugin.sh 重编后提交 libs/。"
            )
            failed = 1
    return failed


if __name__ == "__main__":
    sys.exit(main())
