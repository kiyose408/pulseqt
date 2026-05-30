"""串口测试服务端 — 供 T003 SerialChannel 验收使用

需要先安装 pyserial:  pip install pyserial
需要虚拟串口对工具（二选一）：
  - com0com (Windows)   https://com0com.sourceforge.net/
  - socat  (Linux/Mac)  socat -d -d pty,raw,echo=0 pty,raw,echo=0

用 com0com 创建一对虚拟串口，比如 COM2 <-> COM3
脚本打开 COM2 发数据，PulseQt 打开 COM3 收数据

用法：
    python tools/serial_test_server.py COM2
"""
import sys
import time
import serial

if len(sys.argv) < 2:
    print("Usage: python serial_test_server.py <COM_PORT>")
    print("Example: python serial_test_server.py COM2")
    sys.exit(1)

port = sys.argv[1]
baud = 115200

ser = serial.Serial(port, baud, timeout=1)
print(f"Serial test server opened {port} @ {baud}")
print("Now start PulseQt with SerialChannel pointing to the OTHER COM port.")

time.sleep(1)

ser.write(b"Hello from serial!")
print("Sent: Hello from serial!")

time.sleep(2)
ser.write(b"Serial message 2")
print("Sent: Serial message 2")

time.sleep(3)
ser.close()
print("Server done — check app.log for [TEST] messages.")
