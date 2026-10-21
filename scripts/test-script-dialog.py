"""Exercise the actual dialog bridge against mocked system UI and runtime APIs.

Uses Linux GCC/ASan/UBSan; on Windows invokes the same tools through WSL.
The stubs test lifecycle and protocol behavior, not the SDK ABI or GPU output.
"""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
build = root / "build/script-dialog-test"
build.mkdir(parents=True, exist_ok=True)
prefix = ["wsl", "-e"] if os.name == "nt" else []


def path(value):
    value = str(value.resolve()).replace("\\", "/")
    return "/mnt/" + value[0].lower() + value[2:] if os.name == "nt" else value


flags = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
obj = build / "cJSON.o"
exe = build / "test"
subprocess.run(prefix + ["gcc", *flags, "-c", path(root / "vendor/cJSON/cJSON.c"), "-o", path(obj)], check=True)
includes = ["-I" + path(root / name) for name in ["tests/script_dialog/stubs", "host-direct/src", "vendor/cJSON"]]
subprocess.run(prefix + ["g++", "-std=c++17", *flags, *includes,
                        path(root / "tests/script_dialog/test.cpp"), path(root / "host-direct/src/script_dialog.cpp"),
                        path(obj), "-o", path(exe)], check=True)
subprocess.run(prefix + [path(exe)], check=True)
