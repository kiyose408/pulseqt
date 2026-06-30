#!/usr/bin/env python3
"""T028 — 协议帧 Fuzzing 测试工具

对自定义协议和 Modbus RTU 协议生成变异帧，验证解码器不崩溃不泄漏。

变异策略:
  1. 随机字节 — 完全随机的任意长度数据
  2. 位翻转  — 合法帧的随机 1~3 位取反
  3. 长度篡改 — payload 长度字段与实际数据不一致
  4. CRC 破坏 — 合法帧随机修改 1~2 字节 CRC
  5. 边界值   — 空帧、超长帧(>256)、全零帧、全0xFF帧
  6. 粘包乱序 — 合法帧的碎片拼接 + 随机乱序

用法:
    python tools/protocol_fuzzer.py --target tcp:127.0.0.1:9999 --protocol custom --count 1000
    python tools/protocol_fuzzer.py --target serial:COM7 --baud 115200 --protocol modbus --count 500
"""

import sys
import struct
import random
import time
import argparse
from typing import List, Tuple, Optional

try:
    import serial
except ImportError:
    serial = None
try:
    import socket
except ImportError:
    socket = None

# ── CRC16 工具 (CCITT + Modbus 共用) ───────────────────

def crc16_ccitt(data: bytes) -> int:
    """自定义协议 CRC（多项式 0x1021，初值 0xFFFF）"""
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


CRC16_MODBUS_TABLE = [
    0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,
    0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
    0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,
    0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
    0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,
    0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
    0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,
    0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
    0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,
    0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
    0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,
    0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
    0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,
    0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
    0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,
    0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
    0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,
    0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
    0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,
    0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
    0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,
    0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
    0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,
    0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
    0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,
    0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
    0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,
    0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
    0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,
    0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
    0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,
    0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040,
]

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc = (crc >> 8) ^ CRC16_MODBUS_TABLE[(crc ^ b) & 0xFF]
    return crc & 0xFFFF


# ── 合法帧构建 ────────────────────────────────────────

def build_custom_frame(payload: bytes, frame_type: int = 0x01) -> bytes:
    """构建自定义协议帧: [A55A][len][type][payload][CRC16-CCITT]"""
    raw = bytearray([0xA5, 0x5A])
    raw.append(len(payload) & 0xFF)
    raw.append(frame_type & 0xFF)
    raw.extend(payload)
    crc = crc16_ccitt(bytes(raw))
    raw.append(crc & 0xFF)
    raw.append((crc >> 8) & 0xFF)
    return bytes(raw)


def build_modbus_query(slave: int = 1) -> bytes:
    """构建 Modbus 查询帧: [slave][03][0000][0003][CRC]"""
    raw = bytearray([slave, 0x03, 0x00, 0x00, 0x00, 0x03])
    crc = crc16_modbus(bytes(raw))
    raw.append(crc & 0xFF)
    raw.append((crc >> 8) & 0xFF)
    return bytes(raw)


def build_modbus_response(slave: int = 1, regs: List[int] = None) -> bytes:
    """构建 Modbus 响应帧: [slave][03][byteCount][regs BE][CRC]"""
    if regs is None:
        regs = [512, 512, 512]
    raw = bytearray([slave, 0x03, len(regs) * 2])
    for r in regs:
        raw.append((r >> 8) & 0xFF)
        raw.append(r & 0xFF)
    crc = crc16_modbus(bytes(raw))
    raw.append(crc & 0xFF)
    raw.append((crc >> 8) & 0xFF)
    return bytes(raw)


# ── 变异策略 ──────────────────────────────────────────

def mutate_random_bytes(size: int = None) -> bytes:
    """完全随机的任意长度数据"""
    size = size or random.randint(1, 512)
    return bytes(random.randint(0, 255) for _ in range(size))


def mutate_bit_flip(frame: bytes, bits: int = None) -> bytes:
    """合法帧的随机位翻转"""
    if not frame:
        return frame
    bits = bits or random.randint(1, 3)
    frame = bytearray(frame)
    for _ in range(bits):
        idx = random.randint(0, len(frame) - 1)
        bit = 1 << random.randint(0, 7)
        frame[idx] ^= bit
    return bytes(frame)


def mutate_length_tamper(frame: bytes, protocol: str) -> bytes:
    """payload 长度字段与实际数据不一致"""
    if protocol == "custom" and len(frame) >= 4:
        frame = bytearray(frame)
        real_len = len(frame) - 5  # header(2)+len(1)+type(1)+crc(2) = 5 overhead
        frame[2] = random.choice([0, max(0, real_len - 1), real_len + 1, 255])
        return bytes(frame)
    return frame


def mutate_crc_corrupt(frame: bytes, protocol: str) -> bytes:
    """CRC 字段随机破坏"""
    if not frame or len(frame) < 2:
        return frame
    frame = bytearray(frame)
    frame[-2] ^= random.randint(1, 255)
    frame[-1] ^= random.randint(1, 255)
    return bytes(frame)


def mutate_edge_cases() -> bytes:
    """边界值：空帧、超长帧、全零、全0xFF"""
    choice = random.randint(0, 3)
    if choice == 0:
        return b""  # 空帧
    elif choice == 1:
        return bytes(random.randint(0, 255) for _ in range(1024))  # 超长
    elif choice == 2:
        return b"\x00" * random.randint(1, 256)  # 全零
    else:
        return b"\xFF" * random.randint(1, 256)  # 全0xFF


def mutate_fragment_shuffle(frames: List[bytes]) -> bytes:
    """多帧碎片拼接 + 乱序"""
    if not frames:
        return b""
    data = bytearray()
    for f in frames:
        if f:
            start = random.randint(0, max(0, len(f) - 1))
            end = random.randint(start, len(f))
            data.extend(f[start:end])
    random.shuffle(data)  # 原地乱序
    return bytes(data)


# ── 传输后端 ──────────────────────────────────────────

class TransportBackend:
    """运输层抽象"""
    def send(self, data: bytes):
        raise NotImplementedError

    def close(self):
        pass


class TcpBackend(TransportBackend):
    def __init__(self, host: str, port: int):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(3)
        self.sock.connect((host, port))

    def send(self, data: bytes):
        self.sock.sendall(data)

    def close(self):
        self.sock.close()


class SerialBackend(TransportBackend):
    def __init__(self, port: str, baud: int):
        if serial is None:
            raise RuntimeError("pyserial required: pip install pyserial")
        self.ser = serial.Serial(port, baud, timeout=1)

    def send(self, data: bytes):
        self.ser.write(data)

    def close(self):
        self.ser.close()


# ── Fuzzer 引擎 ───────────────────────────────────────

class ProtocolFuzzer:
    def __init__(self, transport: TransportBackend, protocol: str,
                 count: int, interval_ms: int):
        self.transport = transport
        self.protocol = protocol  # "custom" or "modbus"
        self.count = count
        self.interval = interval_ms / 1000.0
        self.stats = {"sent": 0, "errors": 0, "by_strategy": {}}
        self.recent_frames: List[bytes] = []  # 保存最近合法帧供变异

    def _record(self, strategy: str):
        self.stats["sent"] += 1
        self.stats["by_strategy"][strategy] = \
            self.stats["by_strategy"].get(strategy, 0) + 1

    def _gen_valid(self) -> bytes:
        """生成合法帧作为基线"""
        if self.protocol == "custom":
            payload = bytes(random.randint(0, 255) for _ in range(6))
            return build_custom_frame(payload)
        else:
            if random.random() < 0.5:
                return build_modbus_query()
            else:
                regs = [random.randint(0, 1023) for _ in range(3)]
                return build_modbus_response(regs=regs)

    def _gen_mutant(self, strategy: str) -> Optional[bytes]:
        if strategy == "valid":
            return self._gen_valid()
        elif strategy == "random":
            return mutate_random_bytes()
        elif strategy == "bit_flip":
            base = random.choice(self.recent_frames) if self.recent_frames else self._gen_valid()
            return mutate_bit_flip(base)
        elif strategy == "length_tamper":
            base = random.choice(self.recent_frames) if self.recent_frames else self._gen_valid()
            return mutate_length_tamper(base, self.protocol)
        elif strategy == "crc_corrupt":
            base = random.choice(self.recent_frames) if self.recent_frames else self._gen_valid()
            return mutate_crc_corrupt(base, self.protocol)
        elif strategy == "edge":
            return mutate_edge_cases()
        elif strategy == "fragment":
            return mutate_fragment_shuffle(self.recent_frames[-5:] if self.recent_frames else [])
        return None

    def run(self):
        strategies = ["valid", "random", "bit_flip", "length_tamper",
                       "crc_corrupt", "edge", "fragment"]
        weights   = [10,      30,      15,        10,             15,         5,      15]
        # 策略权重: 合法帧 10%, 随机字节 30%(最暴力), 位翻转 15%, 长度篡改 10%, CRC破坏 15%, 边界 5%, 碎片 15%

        print(f"[Fuzzer] {self.protocol} protocol, {self.count} mutations, "
              f"interval={self.interval*1000:.0f}ms")
        print(f"[Fuzzer] target: {type(self.transport).__name__}")
        print("-" * 60)

        t0 = time.time()
        for i in range(self.count):
            strategy = random.choices(strategies, weights=weights, k=1)[0]
            try:
                data = self._gen_mutant(strategy)
                if data is None:
                    continue

                self.transport.send(data)
                self._record(strategy)

                # 保存合法帧供后续变异
                if strategy == "valid" and len(data) >= 4:
                    self.recent_frames.append(data)
                    if len(self.recent_frames) > 20:
                        self.recent_frames.pop(0)

            except (socket.timeout, ConnectionError, serial.SerialException) as e:
                self.stats["errors"] += 1
                if self.stats["errors"] <= 3:
                    print(f"[WARN] transport error (#{self.stats['errors']}): {e}")

            # 进度
            if (i + 1) % max(1, self.count // 10) == 0:
                elapsed = time.time() - t0
                print(f"  [{i+1:5d}/{self.count}] "
                      f"{(i+1)/max(elapsed,0.001):.0f} fuzz/sec")

            time.sleep(self.interval)

        elapsed = time.time() - t0
        print("-" * 60)
        print(f"[Fuzzer] DONE: {self.count} test vectors in {elapsed:.1f}s")
        for s, n in sorted(self.stats["by_strategy"].items()):
            print(f"  {s:16s}: {n:5d}")
        print(f"  errors         : {self.stats['errors']:5d}")


# ── CLI ───────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description="Protocol Fuzzer (T028)")
    p.add_argument("--target", required=True,
                   help="tcp:HOST:PORT or serial:PORT")
    p.add_argument("--baud", type=int, default=115200,
                   help="Serial baud rate (default: 115200)")
    p.add_argument("--protocol", choices=["custom", "modbus"], default="custom")
    p.add_argument("--count", type=int, default=1000,
                   help="Number of fuzz mutations (default: 1000)")
    p.add_argument("--interval", type=int, default=1,
                   help="Interval between sends in ms (default: 1)")
    args = p.parse_args()

    # 创建传输
    transport = None
    try:
        if args.target.startswith("tcp:"):
            _, host, port = args.target.split(":")
            transport = TcpBackend(host, int(port))
        elif args.target.startswith("serial:"):
            port = args.target.split(":", 1)[1]
            transport = SerialBackend(port, args.baud)
        else:
            print(f"Unknown target format: {args.target}")
            sys.exit(1)

        fuzzer = ProtocolFuzzer(transport, args.protocol, args.count, args.interval)
        fuzzer.run()

    except KeyboardInterrupt:
        print("\n[Fuzzer] interrupted by user")
    finally:
        if transport:
            transport.close()


if __name__ == "__main__":
    main()
