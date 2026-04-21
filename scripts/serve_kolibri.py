import http.server
import socketserver
import os

PORT = 8000
DIRECTORY = "web/dist"

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def end_headers(self):
        # Добавляем заголовки для работы WASM и SharedArrayBuffer
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

# Регистрируем MIME-тип для WASM
Handler.extensions_map.update({
    '.wasm': 'application/wasm',
    '.js': 'application/javascript',
})

print(f"[KOLIBRI] Сервер запускается на http://localhost:{PORT}")
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    httpd.serve_forever()
