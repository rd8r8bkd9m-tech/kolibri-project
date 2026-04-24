import http.server
import socketserver
import sys
import urllib.error
import urllib.parse
import urllib.request

PORT = 8081
BACKEND_PORT = 8000
BACKEND_URL = f"http://localhost:{BACKEND_PORT}"


class ProxyHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/api/"):
            # Proxy to the C backend
            target_url = f"{BACKEND_URL}{self.path}"
            print(f"Proxying {self.path} to {target_url}")
            try:
                with urllib.request.urlopen(target_url) as response:
                    self.send_response(response.status)
                    # Copy headers
                    for key, value in response.headers.items():
                        self.send_header(key, value)
                    self.end_headers()
                    # Copy body
                    self.wfile.write(response.read())
            except urllib.error.HTTPError as e:
                self.send_response(e.code)
                self.end_headers()
                self.wfile.write(e.read())
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Proxy Error: {str(e)}".encode("utf-8"))
        else:
            # Serve static files
            super().do_GET()


if __name__ == "__main__":
    # Allow reuse address to avoid "Address already in use"
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), ProxyHandler) as httpd:
        print(f"Serving at port {PORT}")
        print(f"Proxying API requests to {BACKEND_URL}")
        httpd.serve_forever()
