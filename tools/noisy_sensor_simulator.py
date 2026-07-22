#!/usr/bin/env python3
"""噪声传感器模拟器 — 模拟工业传感器常见数据问题

叠加以下噪声类型（均可独立开关）:
  1. 基底信号 — 正弦/锯齿/三角/方波/直流
  2. 白噪声   — ±N 高斯分布
  3. 脉冲野值 — 随机 9999（模拟传感器断线/ADC溢出）
  4. 50Hz纹波 — 电源干扰
  5. 漂移     — 缓慢线性偏移（模拟温度漂移）
  6. 阶跃     — 周期性跳变（模拟阀门开关/负载突变）

用法:
    python3 tools/noisy_sensor_simulator.py --target tcp:9999
    python3 tools/noisy_sensor_simulator.py --target tcp:9999 --noise 10 --glitch-rate 0.02
"""

import sys
import math
import time
import struct
import random
import socket
import argparse
from typing import List, Tuple


class NoisyChannel:
    """单通道噪声发生器"""

    def __init__(self, base_type: str = "sin",
                 base_amp: float = 500, base_offset: float = 500,
                 noise_amp: float = 0, glitch_rate: float = 0,
                 ripple_amp: float = 0, drift_rate: float = 0,
                 step_amp: float = 0, step_interval: float = 10):
        self.base_type = base_type
        self.base_amp = base_amp
        self.base_offset = base_offset
        self.noise_amp = noise_amp
        self.glitch_rate = glitch_rate
        self.ripple_amp = ripple_amp
        self.drift_rate = drift_rate
        self.step_amp = step_amp
        self.step_interval = step_interval

        self._last_step = 0
        self._step_state = 0

    def sample(self, t: float) -> float:
        # 1. 基底信号
        phase = t * math.pi
        if self.base_type == "sin":
            val = self.base_amp * math.sin(phase) + self.base_offset
        elif self.base_type == "saw":
            val = (t % 2) * self.base_amp + self.base_offset
        elif self.base_type == "tri":
            val = self.base_amp * (2 * abs(2 * (t % 2) - 1) - 1) + self.base_offset
        elif self.base_type == "square":
            val = self.base_amp * (1 if (int(t * 2) % 2 == 0) else -1) + self.base_offset
        else:  # dc
            val = float(self.base_offset)

        # 2. 白噪声 (高斯)
        if self.noise_amp > 0:
            val += random.gauss(0, self.noise_amp)

        # 3. 脉冲野值 — 叠加尖峰脉冲（不替换基值，不破坏 Y 轴）
        if self.glitch_rate > 0 and random.random() < self.glitch_rate:
            val += self.base_amp * 1.5   # 对于 500 幅的信号 = +750，明显但可控

        # 4. 50Hz 工频干扰
        if self.ripple_amp > 0:
            val += self.ripple_amp * math.sin(2 * math.pi * 50 * t)

        # 5. 线性漂移 (长期)
        if self.drift_rate > 0:
            val += self.drift_rate * t

        # 6. 阶跃
        if self.step_amp > 0:
            if t - self._last_step > self.step_interval:
                self._step_state = 1 - self._step_state
                self._last_step = t
            if self._step_state:
                val += self.step_amp

        return val


# ── 自定义协议帧构建 (0xE1-0xE5, 与 PulseQt v1.2 兼容) ──

CRC16_CCITT_TABLE = [
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
    crc = 0xFFFF
    for b in data:
        crc = (crc << 8) ^ CRC16_CCITT_TABLE[((crc >> 8) ^ b) & 0xFF]
        crc &= 0xFFFF
    return crc

def build_frame(payload: bytes, frame_type: int = 0xE1) -> bytes:
    raw = bytearray([0xA5, 0x5A])
    raw.append(len(payload) & 0xFF)
    raw.append(frame_type & 0xFF)
    raw.extend(payload)
    crc = crc16_ccitt(bytes(raw))
    raw.append(crc & 0xFF)
    raw.append((crc >> 8) & 0xFF)
    return bytes(raw)


def build_handshake(num_channels: int, types: List[int]) -> bytes:
    p = bytearray([num_channels])
    p.extend(types)
    return build_frame(bytes(p), 0xE4)


# ── TCP 服务端 ────────────────────────────────────────

CHANNEL_TYPES = [0x02, 0x02, 0x02, 0x02]  # 4 通道 uint16 (0x02=CH_UINT16)

def main():
    p = argparse.ArgumentParser(description="噪声传感器模拟器")
    p.add_argument("--target", default="tcp:9999", help="tcp:PORT (默认 tcp:9999)")
    p.add_argument("--noise", type=float, default=5, help="白噪声幅度 ±N (默认5)")
    p.add_argument("--glitch-rate", type=float, default=0.01, help="脉冲野值概率 (默认0.01)")
    p.add_argument("--ripple", type=float, default=3, help="50Hz纹波幅度 (默认3)")
    p.add_argument("--drift", type=float, default=0.5, help="线性漂移速率/秒 (默认0.5)")
    p.add_argument("--step-amp", type=float, default=200, help="阶跃幅度 (默认200)")
    p.add_argument("--step-interval", type=float, default=8, help="阶跃间隔秒 (默认8)")
    p.add_argument("--rate", type=int, default=100, help="帧率 Hz (默认100)")
    args = p.parse_args()

    _, host, port = args.target.split(":")
    port = int(port)

    # 4 通道: 正弦/常值 × 干净/噪声
    channels = [
        NoisyChannel("sin", 200, 750, 0,          0,          0, 0, 0, 0),  # CH0 干净正弦 (550~950)
        NoisyChannel("dc",  0,   500, 0,          0,          0, 0, 0, 0),  # CH1 干净常值 (500)
        NoisyChannel("sin", 200, 750, args.noise, args.glitch_rate, 0, 0, 0, 0),  # CH2 噪声正弦 (550~950±)
        NoisyChannel("dc",  0,   500, args.noise, args.glitch_rate, 0, 0, 0, 0),  # CH3 噪声常值 (500±)
    ]

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(1)
    print(f"[Noisy] 4ch 正弦+常值 — {host}:{port} @ {args.rate}Hz")
    print(f"  CH0: 干净正弦  |  CH2: 正弦 ±{args.noise} +{args.glitch_rate*100:.0f}%尖刺")
    print(f"  CH1: 干净常值  |  CH3: 常值 ±{args.noise} +{args.glitch_rate*100:.0f}%尖刺")
    print("  等待连接...")

    while True:
        conn, addr = server.accept()
        print(f"  已连接: {addr}")

        # 握手
        conn.sendall(build_handshake(len(CHANNEL_TYPES), CHANNEL_TYPES))
        time.sleep(0.05)

        interval = 1.0 / args.rate
        t0 = time.time()
        count = 0

        try:
            while True:
                t = time.time() - t0
                samples = [int(max(0, min(65535, ch.sample(t)))) for ch in channels]
                payload = struct.pack("<HHHH", *samples)
                conn.sendall(build_frame(payload, 0xE1))
                count += 1

                if count % 100 == 0:
                    print(f"\r  [{count:5d}帧] CH0={samples[0]:5d} CH1={samples[1]:5d} CH2={samples[2]:5d} CH3={samples[3]:5d}", end="")

                elapsed = time.time() - t0 - count * interval
                if elapsed < interval:
                    time.sleep(interval - elapsed)

        except (ConnectionError, BrokenPipeError):
            print(f"\n  客户端断开 ({count} 帧)，等待重连...")
        except KeyboardInterrupt:
            print(f"\n  共发送 {count} 帧")
            break
        finally:
            conn.close()


if __name__ == "__main__":
    main()
