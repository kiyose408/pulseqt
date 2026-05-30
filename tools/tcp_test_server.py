"""TCP 测试服务端 — 供 T003 SerialChannel/TcpChannel 验收使用

用法：
    python tools/tcp_test_server.py
    然后启动 PulseQt，TcpChannel 连接 127.0.0.1:9999
"""
import socket
import time

HOST = "127.0.0.1"
PORT = 9999

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(1)
print(f"TCP test server listening on {HOST}:{PORT} ...")
print("Now start PulseQt to connect.")

conn, addr = s.accept()
print(f"Client connected: {addr}")

# 发第一条数据
time.sleep(0.5)
conn.send(b"Hello from server!")
print("Sent: Hello from server!")

# 发第二条数据
time.sleep(2)
conn.send(b"Second message")
print("Sent: Second message")

# 等 3 秒后关闭
time.sleep(3)
conn.close()
s.close()
print("Server done — check app.log for [TEST] messages.")
