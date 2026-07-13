#!/usr/bin/env python3
"""
Modbus RTU 从站模拟器 (标准主从模式)

功能：
    模拟一个 Modbus RTU 从站设备。
    只有当接收到主机的读取请求时，才会回复数据。
    回复 3 个保持寄存器的模拟波形数据。

协议细节：
    - 波特率: 115200 (可配置)
    - 数据位: 8
    - 停止位: 1
    - 校验位: 无 (None)
    - 功能码: 0x03 (读保持寄存器)
"""

import sys
import time
import math
import argparse
import serial
from typing import List, Tuple

# CRC16-Modbus 查表法 (多项式 0x8005, 初值 0xFFFF)
CRC16_TABLE = [
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
]

def crc16_modbus(data: bytes) -> int:
    """计算 Modbus RTU CRC16 校验值"""
    crc = 0xFFFF
    for byte in data:
        crc = (crc >> 8) ^ CRC16_TABLE[(crc ^ byte) & 0xFF]
    return crc & 0xFFFF

def validate_crc(frame: bytes) -> bool:
    """验证帧的 CRC 校验"""
    if len(frame) < 2:
        return False
    received_crc = frame[-2] | (frame[-1] << 8)
    calc_crc = crc16_modbus(frame[:-2])
    return received_crc == calc_crc

def build_response(slave_id: int, func_code: int, register_values: List[int]) -> bytes:
    """
    构建 Modbus RTU 响应帧
    """
    # 协议头: 从站地址 + 功能码 + 字节计数
    byte_count = len(register_values) * 2
    header = bytes([slave_id, func_code, byte_count])
    
    # 寄存器数据 (大端序)
    data_body = bytearray()
    for value in register_values:
        # 确保值在 UInt16 范围内
        clamped = max(0, min(0xFFFF, value))
        data_body.append((clamped >> 8) & 0xFF)
        data_body.append(clamped & 0xFF)
    
    # 组装并计算 CRC
    frame_without_crc = header + data_body
    crc = crc16_modbus(frame_without_crc)
    
    # 完整帧: 头 + 数据 + CRC(低字节在前)
    full_frame = frame_without_crc + bytes([crc & 0xFF, (crc >> 8) & 0xFF])
    return full_frame

def parse_request(received_data: bytes) -> Tuple[bool, str]:
    """
    解析主机发来的请求帧。
    返回 (是否合法, 错误原因)
    这里简化处理，只检查基本格式和 CRC。
    """
    if len(received_data) < 8:
        return False, "帧长不足"
    
    # 1. 提取基本信息
    unit_id = received_data[0]       # 从站地址
    func_code = received_data[1]     # 功能码
    
    # 2. 检查 CRC
    if not validate_crc(received_data):
        return False, "CRC校验失败"
    
    # 3. 检查功能码 (仅支持 0x03 读保持寄存器)
    if func_code != 0x03:
        return False, f"不支持的功能码: {func_code}"
    
    # 4. 检查寄存器地址和数量 (简化：只允许读取前 3 个寄存器)
    # 地址: received_data[2:4], 数量: received_data[4:6]
    start_addr = (received_data[2] << 8) | received_data[3]
    reg_count = (received_data[4] << 8) | received_data[5]
    
    if start_addr != 0:
        return False, f"寄存器地址错误，期望 0，收到 {start_addr}"
    if reg_count != 3:
        return False, f"寄存器数量错误，期望 3，收到 {reg_count}"
    
    return True, "OK"

def generate_mock_data() -> List[int]:
    """生成模拟的寄存器数据 (3个通道的波形)"""
    t = time.time()
    ch0 = int(512 + 511 * math.sin(t * math.pi))
    ch1 = int(1023 * (t % 3.0) / 3.0)
    ch2 = int(500 + 500 * math.sin(t * math.pi * 0.4 + 1.0))
    return [ch0, ch1, ch2]

def main():
    parser = argparse.ArgumentParser(description="Modbus RTU 从站模拟器")
    parser.add_argument("port", help="串口端口 (例如 COM3 或 /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="波特率 (默认: 115200)")
    parser.add_argument("--slave", type=int, default=1, help="从站地址 (默认: 1)")
    args = parser.parse_args()

    # 初始化串口
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1, bytesize=8, parity='N', stopbits=1)
        print(f"[OK] {args.port} @ {args.baud}")
        print(f"Listening slave={args.slave} ... (Ctrl+C to stop)")
        print("-" * 50)
    except Exception as e:
        print(f"[ERROR] cannot open port: {e}")
        sys.exit(1)

    buffer = bytearray() # 接收缓冲区

    try:
        while True:
            # 1. 读取串口数据
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                buffer.extend(data)
                
                # ── 处理缓冲区中的完整帧 ──────────────────────────
                # Modbus RTU 查询帧（功能码 0x03 读保持寄存器）长度固定为 8 字节：
                #   [Slave(1)] [Func(1)=0x03] [StartAddr(2)] [RegCount(2)] [CRC(2)]
                # 长度不依赖 payload 内容，不能套用响应帧的 byte-count 公式。
                while len(buffer) >= 8:
                    # 取 8 字节作为候选查询帧
                    frame = bytes(buffer[:8])
                    del buffer[:8]

                    # 解析请求
                    is_valid, msg = parse_request(frame)
                    if is_valid:
                        # 生成数据并回复
                        regs = generate_mock_data()
                        response = build_response(args.slave, 0x03, regs)
                        ser.write(response)

                        # 打印日志
                        print(f"[RX] query | reply: CH0={regs[0]:4d} CH1={regs[1]:4d} CH2={regs[2]:4d}")
                    else:
                        print(f"[WARN] invalid request: {msg}")
                            
            # 避免 CPU 占用过高
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\nBye")
    except Exception as e:
        print(f"\n[ERROR] runtime: {e}")
    finally:
        ser.close()

if __name__ == "__main__":
    main()