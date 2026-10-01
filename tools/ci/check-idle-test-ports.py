"""Fail rather than skip daemon contract tests or terminate an existing application."""
import socket
for port in (19897, 19898):
    with socket.socket() as sock:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            raise SystemExit(f"Software integration port {port} is occupied. Exit Aura normally and rerun; CI will not kill it.")
print("Integration ports available; no process was stopped.")
