#!/usr/bin/env python3
"""
T001 — TCP 六通道磁场集采器模拟器

按物理帧格式封装 48 字节数据帧，1000Hz 通过 TCP 发送。
每个通道注入独立 50Hz 正弦波信号，用于验证 FFT 相位提取。

帧格式 (48 bytes, little-endian):
  Offset  Size  Field
  ──────────────────────────────────────
   0      2     帧头: 0x46 0x4D ("FM")
   2      2     帧总长: uint16 LE (=48)
   4      4     帧序列: uint32 LE
   8      1     周 (WW)
   9      1     月 (MM)
  10      1     日 (DD)
  11      1     年 (YY, 0~99)
  12      1     时 (HH)
  13      1     分 (MM)
  14      1     秒 (SS)
  15      1     时间格式 (FF)
  16      4     毫秒: uint32 LE
  20      3     X1 数据: int24 LE
  23      1     X1 标志: 0x58 ('X')
  24      3     Y1 数据: int24 LE
  27      1     Y1 标志: 0x59 ('Y')
  28      3     Z1 数据: int24 LE
  31      1     Z1 标志: 0x5A ('Z')
  32      3     X2 数据: int24 LE
  35      1     X2 标志: 0x78 ('x')
  36      3     Y2 数据: int24 LE
  39      1     Y2 标志: 0x79 ('y')
  40      3     Z2 数据: int24 LE
  43      1     Z2 标志: 0x7A ('z')
  44      2     校验和: uint16 LE (offset 4~43 共 40 字节的字节和)
  46      2     帧尾: 0x0D 0x0A (CR LF)

使用方式:
  python tools/mag_collector_simulator.py --host 127.0.0.1 --port 9999
  python tools/mag_collector_simulator.py --host 127.0.0.1 --port 9999 --amplitude 1000000 --phases 45,90,135,180,225,270
"""

import socket
import struct
import time
import argparse
import math
from datetime import datetime


# ── 帧常量 ──────────────────────────────────────────────
HEADER       = b'\x46\x4D'          # "FM"
FOOTER       = b'\x0D\x0A'          # CR LF
FRAME_SIZE   = 48
CHANNEL_FLAGS = [0x58, 0x59, 0x5A, 0x78, 0x79, 0x7A]  # X Y Z x y z

# 磁场公式: Mag = raw * 100000 / 8388607
# raw = Mag * 8388607 / 100000
RAW_SCALE = 8388607.0 / 100000.0


def encode_int24(value: int) -> bytes:
    """将 24-bit 有符号整数编码为 3 字节小端序"""
    # 限制在 int24 范围
    value = max(-8388608, min(8388607, value))
    # 转为无符号 24-bit
    if value < 0:
        value += 0x1000000
    return struct.pack('<I', value)[:3]


def compute_checksum(data: bytes) -> int:
    """计算 40 字节(offset 4~43)的简单字节和, uint16"""
    return sum(data) & 0xFFFF


def build_frame(sequence: int, channel_data: list, timestamp: tuple = None) -> bytes:
    """
    构造一个完整的 48 字节数据帧。

    Args:
        sequence:     帧序列号 (uint32)
        channel_data: 6 通道 int32 原始数据列表
        timestamp:    (week, month, day, year, hour, minute, second, format, ms)
                      若为 None 则自动取当前时间
    Returns:
        48 字节帧
    """
    if timestamp is None:
        now = datetime.now()
        timestamp = (
            now.isocalendar()[1] & 0xFF,  # week
            now.month,                     # month
            now.day,                       # day
            now.year % 100,               # year (0-99)
            now.hour,                      # hour
            now.minute,                    # minute
            now.second,                    # second
            0x00,                          # format
            int(now.microsecond / 1000),  # ms (0-999)
        )

    buf = bytearray()

    # Header (2B)
    buf += HEADER

    # Frame length (2B, uint16 LE)
    buf += struct.pack('<H', FRAME_SIZE)

    # Sequence (4B, uint32 LE)
    buf += struct.pack('<I', sequence & 0xFFFFFFFF)

    # Timestamp (12B)
    buf += struct.pack('<B', timestamp[0])   # week
    buf += struct.pack('<B', timestamp[1])   # month
    buf += struct.pack('<B', timestamp[2])   # day
    buf += struct.pack('<B', timestamp[3])   # year
    buf += struct.pack('<B', timestamp[4])   # hour
    buf += struct.pack('<B', timestamp[5])   # minute
    buf += struct.pack('<B', timestamp[6])   # second
    buf += struct.pack('<B', timestamp[7])   # format
    buf += struct.pack('<I', timestamp[8])   # ms

    # 6 channels (3B data + 1B flag each) = 24B
    for i in range(6):
        buf += encode_int24(channel_data[i])
        buf += struct.pack('<B', CHANNEL_FLAGS[i])

    # Checksum (2B, uint16 LE) — covers offset 4~43
    checksum = compute_checksum(buf[4:44])
    buf += struct.pack('<H', checksum)

    # Footer (2B)
    buf += FOOTER

    assert len(buf) == FRAME_SIZE, f"Frame size mismatch: {len(buf)} != {FRAME_SIZE}"
    return bytes(buf)


def main():
    parser = argparse.ArgumentParser(
        description="六通道磁场集采器 TCP 模拟器 (1000Hz, 48B帧)"
    )
    parser.add_argument("--host", default="127.0.0.1", help="TCP 监听地址 (默认: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9999, help="TCP 监听端口 (默认: 9999)")
    parser.add_argument("--rate", type=int, default=1000, help="发送频率 Hz (默认: 1000)")
    parser.add_argument("--amplitude", type=float, default=1000000,
                        help="50Hz 正弦波幅值 (raw ADC, 默认: 1000000, 最大 8388607)")
    parser.add_argument("--phases", type=str, default="0,30,60,90,120,150",
                        help="6 通道初相角(度), 逗号分隔 (默认: 0,30,60,90,120,150)")
    parser.add_argument("--duration", type=float, default=0,
                        help="发送时长(秒), 0=无限 (默认: 0)")
    parser.add_argument("--verbose", type=int, default=10,
                        help="每 N 秒打印一次统计 (默认: 10)")

    args = parser.parse_args()
    phases = [float(p) for p in args.phases.split(",")]
    if len(phases) != 6:
        print(f"错误: --phases 需要 6 个值, 实际收到 {len(phases)} 个")
        return

    amp = min(abs(args.amplitude), 8388607)
    interval = 1.0 / args.rate
    sequence = 0
    frame_count = 0
    start_time = time.time()
    last_report = start_time

    # ── TCP Server (接受一个客户端) ──────────────────────
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(1)
    print(f"[模拟器] TCP 服务端启动: {args.host}:{args.port}")
    print(f"[模拟器] 频率: {args.rate} Hz, 间隔: {interval*1000:.2f} ms")
    print(f"[模拟器] 50Hz 正弦幅值: {amp} raw ADC (Mag ≈ {amp*100000/8388607:.3f})")
    print(f"[模拟器] 6 通道初相: {phases} 度")
    print(f"[模拟器] 等待客户端连接...")

    try:
        conn, addr = server.accept()
        print(f"[模拟器] 客户端已连接: {addr[0]}:{addr[1]}")
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        next_tick = time.perf_counter()

        while True:
            # 精确 1000Hz 定时
            now = time.perf_counter()
            if now < next_tick:
                sleep_time = next_tick - now
                if sleep_time > 0:
                    time.sleep(sleep_time)
            next_tick += interval

            # 若积压过多 (如调试断点) 则追上当前时间
            if next_tick < time.perf_counter() - 1.0:
                next_tick = time.perf_counter() + interval

            # 生成 6 通道数据: 50Hz 正弦波
            t = sequence / args.rate  # 时间 (s)
            channel_data = [
                int(amp * math.sin(2 * math.pi * 50.0 * t + math.radians(phases[i])))
                for i in range(6)
            ]

            # 构建帧
            frame = build_frame(sequence, channel_data)
            sequence += 1

            # 发送
            try:
                conn.sendall(frame)
                frame_count += 1
            except (BrokenPipeError, ConnectionResetError) as e:
                print(f"[模拟器] 发送失败: {e}")
                break

            # 统计报告
            elapsed = time.time() - last_report
            if elapsed >= args.verbose:
                actual_rate = frame_count / (time.time() - start_time)
                print(f"[模拟器] 已发送 {frame_count} 帧 | "
                      f"序列: {sequence} | 实际速率: {actual_rate:.1f} Hz")
                last_report = time.time()

            # 时长限制
            if args.duration > 0 and (time.time() - start_time) >= args.duration:
                print(f"[模拟器] 达到设定时长 {args.duration}s, 停止")
                break

    except KeyboardInterrupt:
        print("\n[模拟器] 用户中断")
    finally:
        conn.close()
        server.close()
        elapsed = time.time() - start_time
        avg_rate = frame_count / elapsed if elapsed > 0 else 0
        print(f"[模拟器] 已停止. 共发送 {frame_count} 帧, "
              f"耗时 {elapsed:.1f}s, 平均速率: {avg_rate:.1f} Hz")


if __name__ == "__main__":
    main()
