#!/usr/bin/env python3
"""生成模拟 9 轴 IMU CSV 数据集（加速度+陀螺仪+磁力计）

输出: timestamp_ms, ax, ay, az, gx, gy, gz, mx, my, mz
  ax/ay/az : 加速度 (mm/s²), 含重力 + 运动 + 噪声
  gx/gy/gz : 陀螺仪 (mdps), 旋转 + 漂移 + 噪声
  mx/my/mz : 磁力计 (μT), 地磁场 + 软铁畸变 + 噪声

用法:
    python tools/generate_imu_csv.py --duration 60 --rate 200 > imu_data.csv
    python tools/generate_imu_csv.py -d 120 -r 100 -o test.csv
"""

import math
import random
import argparse

def generate(duration_sec=60, rate=200, seed=42):
    random.seed(seed)
    n = int(duration_sec * rate)
    dt = 1.0 / rate

    # 模拟手腕转动轨迹
    for i in range(n):
        t = i * dt
        ts = int(t * 1000)

        # 运动: 绕 Y 轴慢转 (模拟抬手) + 绕 Z 轴小幅度抖动
        pitch = 30.0 * math.sin(t * 0.5)          # °, ±30°
        roll  = 10.0 * math.sin(t * 1.3 + 1.0)    # °, ±10°
        yaw   = 45.0 * math.sin(t * 0.2)           # °, ±45°

        # 角速度 (mdeg/s → 转换)
        gx = 30000.0 * math.cos(t * 0.5) * 0.5  + random.gauss(0, 50)
        gy = 13000.0 * math.cos(t * 1.3 + 1.0) + random.gauss(0, 50)
        gz =  9000.0 * math.cos(t * 0.2)        + random.gauss(0, 50)

        # 加速度 (mm/s²): 重力分量 + 线运动
        pitch_r = math.radians(pitch)
        roll_r  = math.radians(roll)
        grav = 9800.0  # mm/s²
        ax = grav * math.sin(pitch_r) + 500.0 * math.sin(t * 2.0)  + random.gauss(0, 30)
        ay = grav * math.sin(roll_r)  + 300.0 * math.cos(t * 1.7)  + random.gauss(0, 30)
        az = grav * math.cos(pitch_r) * math.cos(roll_r) + 400.0 * math.sin(t * 3.1) + random.gauss(0, 30)

        # 磁力计 (μT): 地磁场 ~50μT + 软铁效应
        mx = 25.0 * math.cos(math.radians(yaw)) + 15.0 * math.sin(t * 0.3) + random.gauss(0, 0.5)
        my = 25.0 * math.sin(math.radians(yaw)) + 10.0 * math.cos(t * 0.7) + random.gauss(0, 0.5)
        mz = 35.0 + 5.0 * math.sin(t * 0.1) + random.gauss(0, 0.3)

        yield f"{ts},{ax:.0f},{ay:.0f},{az:.0f},{gx:.0f},{gy:.0f},{gz:.0f},{mx:.1f},{my:.1f},{mz:.1f}"


def main():
    p = argparse.ArgumentParser(description="生成模拟 9 轴 IMU CSV")
    p.add_argument("-d", "--duration", type=float, default=60, help="时长(秒)")
    p.add_argument("-r", "--rate", type=int, default=200, help="采样率 Hz")
    p.add_argument("-o", "--output", help="输出文件 (默认 stdout)")
    p.add_argument("-s", "--seed", type=int, default=42)
    args = p.parse_args()

    header = "timestamp,ax,ay,az,gx,gy,gz,mx,my,mz"
    lines = [header] + list(generate(args.duration, args.rate, args.seed))

    if args.output:
        with open(args.output, 'w') as f:
            f.write('\n'.join(lines))
        print(f"已生成 {args.output} ({len(lines)-1} 行, {args.duration}s @ {args.rate}Hz)")
    else:
        print('\n'.join(lines))


if __name__ == "__main__":
    main()
