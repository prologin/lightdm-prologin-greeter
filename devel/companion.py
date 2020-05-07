#!/usr/bin/env python

"""
A fake greeter companion.

On setup, sends two progress message with 1s delay, then succeeds.
On cleanup, succeeds after 1s.
"""

import time
from socketserver import StreamRequestHandler, UnixStreamServer


class Handler(StreamRequestHandler):
    def handle(self):
        while True:
            line = self.rfile.readline().strip().decode()
            if not line:
                return

            if line.startswith("ping"):
                self.wfile.write("pong\n".encode())
                self.wfile.flush()

            if line.startswith("setup"):
                time.sleep(1)
                self.wfile.write("message Dummy status 1...\n".encode())
                self.wfile.flush()
                time.sleep(1)
                self.wfile.write("message Dummy status 2...\n".encode())
                self.wfile.flush()
                time.sleep(1)
                self.wfile.write("success\n".encode())
                self.wfile.flush()
                return

            elif line.startswith("cleanup"):
                time.sleep(1)
                self.wfile.write("success\n".encode())
                self.wfile.flush()
                return


if __name__ == "__main__":
    import pathlib

    addr = pathlib.Path("/tmp/greeter-companion.socket")
    try:
        addr.unlink()
    except FileNotFoundError:
        pass
    server = UnixStreamServer(str(addr), Handler)
    server.serve_forever()
