#!/usr/bin/env python3
"""IMU 数据集重放 — 读 CSV → 组 PulseQt 帧 → 串口发送

CSV 格式（自动检测列数）:
  6 轴: timestamp, ax, ay, az, gx, gy, gz
  9 轴: timestamp, ax, ay, az, gx, gy, gz, mx, my, mz

用法:
    python tools/replay_imu.py IMU.csv COM7 --baud 115200 --rate 200
    python tools/replay_imu.py IMU.csv COM7 --loop           # 循环回放
    python tools/replay_imu.py IMU.csv --list               # 只打印前5行预览
"""

import sys
import struct
import time
import argparse
import csv
import math

try:
    import serial
except ImportError:
    print("需要 pyserial: pip install pyserial")
    sys.exit(1)

# ══════════════════════════════════════════════════════════
# CRC16-CCITT (与 PulseQt 一致)
# ══════════════════════════════════════════════════════════

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

def crc16_ccitt(data):
    crc = 0xFFFF
    for b in data:
        crc = (crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ b) & 0xFF]
    return crc & 0xFFFF

def build_frame(channels):
    """channels: list of float → 组 PulseQt 二进制帧 (uint16 LE, 含负数映射)"""
    payload = b''
    for v in channels:
        # 负数 → 偏移到 32768 中心 (int16 → uint16)
        raw = int(v) + 32768
        raw = max(0, min(65535, raw))
        payload += struct.pack('<H', raw)
    raw = b'\xA5\x5A'
    raw += struct.pack('B', len(payload))
    raw += b'\x01'  # TYPE_DATA
    raw += payload
    crc = crc16_ccitt(raw)
    raw += struct.pack('<H', crc)
    return raw

# ══════════════════════════════════════════════════════════
# 主程序
# ══════════════════════════════════════════════════════════

def parse_line(row, scale=None):
    """解析一行 CSV, 返回 (channels, timestamp_ms)"""
    # 跳过 timestamp 列 (第 0 列)
    values = [float(v) for v in row[1:]]
    if scale:
        values = [v * scale for v in values]
    return values, float(row[0]) if row[0].replace('.','').replace('-','').isdigit() else 0


def main():
    parser = argparse.ArgumentParser(description="IMU CSV → PulseQt 串口重放")
    parser.add_argument("csv", help="CSV 文件路径")
    parser.add_argument("port", nargs="?", help="串口 (如 COM7)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--rate", type=int, default=200, help="目标帧率 Hz")
    parser.add_argument("--loop", action="store_true", help="循环回放")
    parser.add_argument("--list", action="store_true", help="打印前 5 行预览")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="值缩放 (EuRoC 加速度用 1000 转为 mm/s²)")
    parser.add_argument("--skip", type=int, default=1, help="跳过行号 (含标题)")
    args = parser.parse_args()

    # 读取 CSV
    with open(args.csv) as f:
        reader = csv.reader(f)
        lines = [row for row in reader if len(row) > 2]

    if args.skip:
        lines = lines[args.skip:]

    if not lines:
        print("CSV 为空或格式错误")
        sys.exit(1)

    first_vals, _ = parse_line(lines[0])
    ch_count = len(first_vals)
    print(f"检测到 {ch_count} 个通道, {len(lines)} 行数据")
    print(f"前 5 行预览:")
    for row in lines[:5]:
        vals, ts = parse_line(row, args.scale)
        print(f"  ts={ts:.0f}ms  vals={[f'{v:.0f}' for v in vals]}")

    if args.list:
        return

    if not args.port:
        print("请指定串口 (如 COM7)")
        sys.exit(1)

    # 打开串口
    ser = serial.Serial(args.port, args.baud, timeout=1)
    interval = 1.0 / args.rate
    total = 0
    t0 = time.time()

    print(f"\n开始重放 → {args.port} @ {args.baud}, 目标 {args.rate} Hz")

    try:
        idx = 0
        while True:
            row = lines[idx % len(lines)]
            vals, ts = parse_line(row, args.scale)

            frame = build_frame(vals)
            ser.write(frame)
            total += 1

            idx += 1
            if idx >= len(lines):
                if args.loop:
                    idx = 0
                    print(f"\n[循环 #{total//len(lines)}]")
                else:
                    break

            elapsed = time.time() - t0
            expected = total / args.rate
            if elapsed < expected:
                time.sleep(expected - elapsed)

    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print(f"\n完成: 发送 {total} 帧, 实际速率 {total / (time.time() - t0):.0f} Hz")


if __name__ == "__main__":
    main()
