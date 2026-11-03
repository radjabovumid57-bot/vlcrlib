"""Smoke test for the Python binding: runs a few frames and exits.

    python bindings/python/tests/test_smoke.py

Set ``VLCR_SKIP_WINDOW=1`` on machines without a display to only exercise the
non-window part of the API.
"""

from __future__ import annotations

import os
import pathlib
import sys
import traceback


def check(condition, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ensure_shaders() -> None:
    """The engine loads SPIR-V shaders relative to the working directory."""
    if pathlib.Path("shaders/2d.vert.spv").is_file():
        return
    root = pathlib.Path(__file__).resolve().parents[3]
    for build in ("build-bindings", "build", "build-cpu"):
        candidate = root / build / "Release" / "shaders"
        if candidate.is_dir():
            os.chdir(candidate.parent)
            return


def main() -> int:
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    import vlcrlib as vl

    ensure_shaders()

    abi = vl.version()
    print(f"binding ABI     : {abi}")
    check(abi.count(".") == 2, f"unexpected ABI version {abi!r}")

    check(vl.file_exists(os.path.abspath(__file__)), "file_exists() on this script")
    check(not vl.file_exists("/definitely/not/here"), "file_exists() on a missing file")
    check(vl.file_size(os.path.abspath(__file__)) > 0, "file_size() on this script")

    # Pure value types need no window.
    check((vl.Vector3(1, 2, 3) + vl.Vector3(1, 0, 0)).x == 2, "Vector3 add")
    check(vl.RED.packed() == 0xFF3729E6, "Color packing")
    check(vl.Rectangle(1, 2, 3, 4).width == 3, "Rectangle")

    if os.environ.get("VLCR_SKIP_WINDOW") == "1":
        print("window          : skipped (VLCR_SKIP_WINDOW=1)")
        vl.shutdown()
        return 0

    with vl.Window(320, 240, "vlcrlib python smoke", target_fps=60) as win:
        check(vl.is_ready(), "window is ready")
        check(win.size[0] > 0 and win.size[1] > 0, "window size")
        vl.set_window_title("vlcrlib python smoke")

        font = vl.Font.default()
        size = font.measure("smoke", 20.0)
        check(size.x > 0 and size.y > 0, "font measure")

        for i in range(3):
            with win.frame():
                vl.clear(vl.Color(18, 18, 24))
                vl.draw_rectangle(10, 10, 120, 60, vl.ORANGE)
                vl.draw_rectangle_lines(10, 10, 120, 60, vl.WHITE)
                vl.draw_circle(200, 120, 40, vl.SKYBLUE)
                vl.draw_line_ex((10, 200), (300, 220), 3.0, vl.GREEN)
                vl.draw_triangle((150, 40), (200, 90), (100, 90), vl.PINK)
                font.draw("smoke", 20, 90, 20.0, 1.0, vl.WHITE)
                vl.draw_text("frame", 20, 130, 18, vl.GRAY)

            check(vl.frame_time() >= 0.0, "frame_time")
            check(vl.elapsed_time() >= 0.0, "elapsed_time")

        check(font.handle > 0, "font handle")
        font.unload()

    check(not vl.is_ready(), "window closed")
    print("window          : 3 frames drawn and closed")
    print("python binding  : OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception:  # noqa: BLE001 - a smoke test should print the traceback
        traceback.print_exc()
        raise SystemExit(1)
