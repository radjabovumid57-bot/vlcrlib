"""A small tour of the Python binding.

Run it from a directory that contains the compiled ``shaders/`` folder (the
CMake build tree copies them next to the library), or let this script find the
build tree by itself:

    python bindings/python/examples/hello.py

Set ``VLCR_FRAMES=120`` to exit after 120 frames.
"""

from __future__ import annotations

import math
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import vlcrlib as vl


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
    ensure_shaders()

    frame_limit = int(os.environ.get("VLCR_FRAMES", "0"))

    with vl.Window(960, 540, "vlcrlib — Python binding", target_fps=60) as win:
        font = vl.Font.default()
        camera = vl.Camera3D(
            position=vl.Vector3(6.0, 4.0, 8.0),
            target=vl.Vector3(0.0, 1.0, 0.0),
            fovy=55.0,
            far_plane=200.0,
        )

        frame = 0
        print(f"vlcrlib Python binding, ABI {vl.version()}")

        while win.running:
            t = vl.elapsed_time()

            with win.frame():
                vl.clear(vl.RAYWHITE)

                vl.begin_mode_3d(camera)
                vl.draw_grid(20, 1.0)
                vl.draw_cube(-2.0, 0.5, 0.0, 1.0, 1.0, 1.0, vl.RED)
                vl.draw_sphere(1.5, 1.0 + 0.25 * math.sin(t * 2.0), 0.0, 1.0, vl.SKYBLUE)
                vl.draw_line_3d((0.0, 0.05, -6.0), (0.0, 0.05, 6.0), vl.DARKGRAY)
                vl.end_mode_3d()

                vl.draw_rectangle_pro(
                    vl.Rectangle(80.0, 80.0, 120.0, 60.0),
                    origin=(60.0, 30.0),
                    rotation=math.degrees(t) % 360.0,
                    color=vl.ORANGE,
                )
                vl.draw_rectangle(20, 20, 260, 54, vl.Color(20, 20, 30, 190))
                font.draw("vlcrlib + Python", 34.0, 34.0, 24.0, 1.0, vl.WHITE)

                vl.draw_text(
                    f"frame {frame}  {vl.fps()} fps  {vl.frame_time() * 1000.0:.1f} ms",
                    34, 500, 18, vl.DARKGRAY,
                )

                mouse = vl.mouse_position()
                vl.draw_circle(mouse.x, mouse.y, 8.0, vl.GREEN)

            if vl.key_down(vl.Key.Escape):
                break

            frame += 1
            if frame_limit and frame >= frame_limit:
                break

        font.unload()

    print(f"vlcrlib Python binding finished after {frame} frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
