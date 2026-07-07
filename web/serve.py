#!/usr/bin/env python3
"""Dev server with caching disabled (ES modules cache aggressively otherwise).
Usage: python3 web/serve.py [port]"""
import http.server
import sys


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        super().end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
http.server.ThreadingHTTPServer(('', port), NoCacheHandler).serve_forever()
