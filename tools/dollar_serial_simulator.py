#!/usr/bin/env python3
"""
T002 — 串口 $ 格式磁传感器模拟器

生成 $xxxxxxx.xxxxxx 格式 ASCII 数据，250Hz 通过 TCP 发送。
注入 50Hz 正弦波信号，用于验证 FFT 相位提取。

数据格式:
  $ + 7位整数 + 小数点 + 6位小数 + 换行
  例: $1234567.123456\\n

磁场值: Mag = value / 3.0

使用方式:
  python tools/dollar_serial_simulator.py --host 127.0.0.1 --port 9998
  python tools/dollar_serial_simulator.py --host 127.0.0.1 --port 9998 --baseline 3000000 --amplitude 500000 --phase 45
"""

import socket
import time
import argparse
import math


def format_dollar(value: float) -> str:
    """
    将浮点数格式化为 $xxxxxxx.xxxxxx\\n

    Args:
        value: 浮点数 (如 1234567.123456)
    Returns:
        "$01234567.123456\\n" 格式字符串
    """
    # 分离整数和小数部分
    int_part = int(value)
    frac_part = int(round((value - int_part) * 1_000_000))

    # 处理进位
    if frac_part >= 1_000_000:
        int_part += 1
        frac_part -= 1_000_000
    elif frac_part < 0:
        int_part -= 1
        frac_part += 1_000_000

    # 限制范围
    int_part = max(0, min(9_999_999, int_part))
    frac_part = max(0, min(999_999, frac_part))

    return f"${int_part:07d}.{frac_part:06d}\n"


def main():
    parser = argparse.ArgumentParser(
        description="串口 $ 格式磁传感器模拟器 (250Hz)"
    )
    parser.add_argument("--host", default="127.0.0.1", help="TCP 监听地址 (默认: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9998, help="TCP 监听端口 (默认: 9998)")
    parser.add_argument("--rate", type=int, default=250, help="发送频率 Hz (默认: 250)")
    parser.add_argument("--baseline", type=float, default=3_000_000.0,
                        help="基线值 (默认: 3000000.0)")
    parser.add_argument("--amplitude", type=float, default=500_000.0,
                        help="50Hz 正弦波幅值 (默认: 500000.0)")
    parser.add_argument("--phase", type=float, default=45.0,
                        help="50Hz 初相角(度) (默认: 45)")
    parser.add_argument("--duration", type=float, default=0,
                        help="发送时长(秒), 0=无限 (默认: 0)")
    parser.add_argument("--verbose", type=int, default=10,
                        help="每 N 秒打印一次统计 (默认: 10)")

    args = parser.parse_args()

    interval = 1.0 / args.rate
    line_count = 0
    start_time = time.time()
    last_report = start_time

    baseline = args.baseline
    amplitude = args.amplitude
    phase_rad = math.radians(args.phase)

    # Mag = value / 3.0, 所以 value = Mag * 3.0
    # 展示的 Mag 幅值
    mag_amplitude = amplitude / 3.0

    # ── TCP Server (接受一个客户端) ──────────────────────
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(1)
    print(f"[串口模拟器] TCP 服务端启动: {args.host}:{args.port}")
    print(f"[串口模拟器] 频率: {args.rate} Hz, 间隔: {interval*1000:.2f} ms")
    print(f"[串口模拟器] 基线: {baseline}, 幅值: {amplitude}")
    print(f"[串口模拟器] Mag = value/3, Mag基线≈{baseline/3:.2f}, Mag幅值≈{mag_amplitude:.3f}")
    print(f"[串口模拟器] 50Hz 初相: {args.phase}°")
    print(f"[串口模拟器] 等待客户端连接...")

    try:
        conn, addr = server.accept()
        print(f"[串口模拟器] 客户端已连接: {addr[0]}:{addr[1]}")
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        next_tick = time.perf_counter()
        sequence = 0

        while True:
            # 精确定时
            now = time.perf_counter()
            if now < next_tick:
                sleep_time = next_tick - now
                if sleep_time > 0:
                    time.sleep(sleep_time)
            next_tick += interval

            if next_tick < time.perf_counter() - 1.0:
                next_tick = time.perf_counter() + interval

            # 生成 50Hz 正弦波
            t = sequence / args.rate
            value = baseline + amplitude * math.sin(2 * math.pi * 50.0 * t + phase_rad)
            sequence += 1

            # 格式化
            line = format_dollar(value)

            # 发送
            try:
                conn.sendall(line.encode("ascii"))
                line_count += 1
            except (BrokenPipeError, ConnectionResetError) as e:
                print(f"[串口模拟器] 发送失败: {e}")
                break

            # 统计
            elapsed = time.time() - last_report
            if elapsed >= args.verbose:
                actual_rate = line_count / (time.time() - start_time)
                mag_now = value / 3.0
                print(f"[串口模拟器] 已发送 {line_count} 行 | "
                      f"当前值: {value:.3f} | Mag: {mag_now:.3f} | "
                      f"实际速率: {actual_rate:.1f} Hz")
                last_report = time.time()

            if args.duration > 0 and (time.time() - start_time) >= args.duration:
                print(f"[串口模拟器] 达到设定时长 {args.duration}s, 停止")
                break

    except KeyboardInterrupt:
        print("\n[串口模拟器] 用户中断")
    finally:
        conn.close()
        server.close()
        elapsed = time.time() - start_time
        avg_rate = line_count / elapsed if elapsed > 0 else 0
        print(f"[串口模拟器] 已停止. 共发送 {line_count} 行, "
              f"耗时 {elapsed:.1f}s, 平均速率: {avg_rate:.1f} Hz")


if __name__ == "__main__":
    main()
