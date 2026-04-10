"""
Patch the LVGL 9.x library after PlatformIO installs it.

The upstream LVGL distribution ships ARM SIMD assembly files (Helium, Neon)
that the Xtensa assembler cannot parse – the system stdint.h pulled in by
the helper headers contains C `typedef` declarations that `gas` chokes on.

We neutralise every `.S` file under `lvgl/src/draw/sw/blend/` to an empty
translation unit so the linker just sees zero-byte object files.
"""

import os

Import("env")  # noqa: F821 (provided by PlatformIO)

LIB_ROOT = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps",  # noqa
                        env["PIOENV"], "lvgl", "src", "draw", "sw",
                        "blend")


def neutralise(path):
    try:
        with open(path, "rb") as f:
            head = f.read(16)
    except OSError:
        return
    if head.startswith(b"/* patched */"):
        return  # already stubbed
    with open(path, "wb") as f:
        f.write(b"/* patched */\n")
    print("[patch_lvgl] neutralised", path)


def walk():
    if not os.path.isdir(LIB_ROOT):
        return
    for root, _dirs, files in os.walk(LIB_ROOT):
        for fn in files:
            if fn.endswith(".S"):
                neutralise(os.path.join(root, fn))


walk()
