#!/usr/bin/env python3
"""Serve Emscripten output where .js may be renamed to .js.txt by AV policy."""
import http.server
import os
import sys
from functools import partial


class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        p = super().translate_path(path)
        if not os.path.exists(p) and path.split("?", 1)[0].endswith(".js"):
            if os.path.exists(p + ".txt"):
                return p + ".txt"
        return p

    def guess_type(self, path):
        if path.endswith(".js.txt"):
            return "text/javascript"
        return super().guess_type(path)


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8100
    handler = partial(Handler, directory=directory)
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()


if __name__ == "__main__":
    main()
