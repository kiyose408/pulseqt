#!/usr/bin/env python3
"""串口波形模拟器 — 模拟 3 通道传感器通过 UART 发送二进制数据帧

3 通道波形：
  CH0: 正弦波    0-1023, 周期 ~2s
  CH1: 锯齿波    0-1023, 周期 ~3s
  CH2: 三角波    300-700, 周期 ~5s

帧格式（与 PulseQt 自定义协议一致）：
  Header(2B) + Length(1B) + Type(1B) + Payload(6B) + CRC16(2B)
  0xA5 0x5A  +   0x06    +   0x01   + 3×uint16LE + CCITT

需要 pyserial:  pip install pyserial

硬件接线（两个 USB-TTL 交叉对接）：
  USB-TTL-A          USB-TTL-B
    TXD ──────────→ RXD
    RXD ←────────── TXD
    GND ─────────── GND

用法：
    # 默认 115200, 8N1
    python tools/serial_wave_simulator.py COM3

    # 自定义波特率
    python tools/serial_wave_simulator.py COM3 --baud 9600

    # 50 Hz 速率
    python tools/serial_wave_simulator.py COM3 --rate 50

    # 列出可用串口
    python tools/serial_wave_simulator.py --list
"""

import sys
import struct
import time
import math
import argparse

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("需要 pyserial: pip install pyserial")
    sys.exit(1)

# ══════════════════════════════════════════════════════════════
# CRC16-CCITT 查表（与 PulseQt 的 ProtocolDecoder 完全一致）
# 多项式 0x1021, 初始值 0xFFFF, 标准向量: "123456789" → 0x29B1
# ══════════════════════════════════════════════════════════════

CRC16_TABLE = [
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
    0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
    0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
    0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
    0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
    0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
    0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
    0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
    0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
    0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
    0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
    0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
    0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
    0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
    0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
    0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
    0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
    0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
    0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
    0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
    0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
    0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
    0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
    0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
    0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
    0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
    0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
    0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
    0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
    0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
    0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0,
]


def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT 查表法（与 PulseQt ProtocolDecoder 完全一致）"""
    crc = 0xFFFF
    for b in data:
        crc = (crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ b) & 0xFF]
    return crc & 0xFFFF


def make_data_frame(ch0: int, ch1: int, ch2: int) -> bytes:
    """构建一帧完整数据帧（type=0xE1, 3 通道 uint16 小端序）"""
    payload = struct.pack('<HHH', ch0, ch1, ch2)
    raw = b'\xA5\x5A'                    # Header
    raw += struct.pack('B', len(payload)) # Length
    raw += b'\x01'                        # Type = DATA
    raw += payload                        # Payload (6 bytes)
    crc = crc16_ccitt(raw)
    raw += struct.pack('<H', crc)         # CRC16 小端序
    return raw


def make_heartbeat_resp() -> bytes:
    """构建心跳应答帧（type=0xE3, payload 空）"""
    raw = b'\xA5\x5A\x00\x03'
    crc = crc16_ccitt(raw)
    return raw + struct.pack('<H', crc)


# ══════════════════════════════════════════════════════════════
# 波形生成器
# ══════════════════════════════════════════════════════════════

class WaveGenerator:
    """三通道波形生成，每个采样返回 (ch0, ch1, ch2)"""

    def sample(self, elapsed: float) -> tuple:
        # CH0: 正弦 0-1023, 周期 2s
        ch0 = int(512 + 511 * math.sin(elapsed * math.pi))

        # CH1: 锯齿 0-1023, 周期 3s
        ch1 = int(1023 * (elapsed % 3.0) / 3.0)

        # CH2: 三角 300-700, 周期 5s
        phase = (elapsed % 5.0) / 5.0
        if phase < 0.5:
            ch2 = int(300 + 800 * phase)
        else:
            ch2 = int(700 - 800 * (phase - 0.5))

        return ch0, ch1, ch2


# ══════════════════════════════════════════════════════════════
# 主程序
# ══════════════════════════════════════════════════════════════

def list_ports():
    """列出系统中所有可用串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未找到任何串口")
        return
    print("可用串口:")
    for p in ports:
        print(f"  {p.device}  —  {p.description}")


def main():
    parser = argparse.ArgumentParser(
        description="串口波形模拟器 — 模拟 3 通道传感器 UART 数据源")
    parser.add_argument("port", nargs="?", help="串口名称 (如 COM3, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="波特率 (默认 115200)")
    parser.add_argument("--rate", type=int, default=100, help="发送频率 Hz (默认 100)")
    parser.add_argument("--list", action="store_true", help="列出可用串口并退出")
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    if not args.port:
        print("请指定串口名称，或 --list 查看可用串口")
        print(f"用法: python {sys.argv[0]} COM3")
        sys.exit(1)

    # ── 打开串口 ──────────────────────────────────────
    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0,          # 非阻塞读
        )
    except serial.SerialException as e:
        print(f"无法打开 {args.port}: {e}")
        print("提示: 检查端口号、权限（Linux 需 dialout 组）、是否被其他程序占用")
        sys.exit(1)

    interval = 1.0 / args.rate
    generator = WaveGenerator()
    t0 = time.time()
    frame_count = 0
    hb_recv = 0

    print(f"串口波形模拟器已启动")
    print(f"  端口: {args.port}  波特率: {args.baud}  速率: {args.rate} Hz")
    print(f"  波形: CH0=正弦(2s)  CH1=锯齿(3s)  CH2=三角(5s)")
    print(f"  按 Ctrl+C 停止")
    print()

    try:
        while True:
            loop_start = time.time()
            elapsed = loop_start - t0

            # ── 1. 发送数据帧 ──
            ch0, ch1, ch2 = generator.sample(elapsed)
            frame = make_data_frame(ch0, ch1, ch2)
            ser.write(frame)
            frame_count += 1

            # ── 2. 检查是否有心跳请求 ──
            #    非阻塞读：查收 PulseQt 发来的心跳帧(type=0x02)
            #    仿真器持续发数据帧时 PulseQt 不会发心跳，仅在闲置时触发
            rx = ser.read(1024)
            if rx:
                if b'\xA5\x5A' in rx:
                    # 有帧头 → 应答心跳
                    ser.write(make_heartbeat_resp())
                    hb_recv += 1

            # ── 3. 速率控制 ──
            elapsed_loop = time.time() - loop_start
            if elapsed_loop < interval:
                time.sleep(interval - elapsed_loop)

            # ── 4. 每秒打印统计 ──
            if frame_count % args.rate == 0:
                print(f"\r[串口] 已发送 {frame_count} 帧  "
                      f"| CH0={ch0:4d} CH1={ch1:4d} CH2={ch2:4d}"
                      f"  {'心跳×'+str(hb_recv) if hb_recv else ''}",
                      end="")

    except KeyboardInterrupt:
        print("\n\n模拟器已停止")

    finally:
        ser.close()
        print(f"统计: 共发送 {frame_count} 帧, 收到 {hb_recv} 个心跳请求")


if __name__ == "__main__":
    main()
