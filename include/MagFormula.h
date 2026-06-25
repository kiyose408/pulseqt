//==============================================================================
// MagFormula — 磁场值计算公式（编译期常量）
//
// TCP (六通道磁场集采器):
//   raw ADC (int24, 范围 ±8,388,607) → Mag = raw × 100000 / 8388607
//   物理背景: 2.5V 参考电压, 23-bit 分辨率, ×1000 ×40 标定系数
//
// 串口 ($格式磁传感器):
//   parsed value → Mag = value / 3.0
//==============================================================================

#ifndef MAGFORMULA_H
#define MAGFORMULA_H

namespace MagFormula {

    // ADC 满量程: 2^23 - 1 = 8,388,607
    constexpr double ADC_MAX = 8388607.0;

    // 合成系数: 2.5 × 1000 × 40 = 100,000
    constexpr double SCALE_FACTOR = 100000.0;

    // TCP: raw ADC (int24 as int32) → 磁场值
    inline double fromAdc(int32_t raw) {
        return static_cast<double>(raw) * SCALE_FACTOR / ADC_MAX;
    }

    // 串口: 解析值 → 磁场值
    inline double fromSerial(double value) {
        return value / 3.5;
    }

} // namespace MagFormula

#endif // MAGFORMULA_H
