//==============================================================================
// FFT — Radix-2 Cooley-Tukey, 零依赖
//
// 用法:
//   auto spectrum = FFT::computeMagnitude(samples);  // 返回 [0, N/2] 幅度
//
// 输入必须是 2 的幂 (256, 512, ...)，输出长度 = N/2 + 1
//==============================================================================

#ifndef FFT_H
#define FFT_H

#include <vector>
#include <complex>
#include <cmath>


namespace FFT {

using Complex = std::complex<double>;

// ── 位反转排列 ────────────────────────────────────
inline size_t bitReverse(size_t x, int bits)
{
    size_t r = 0;
    for (int i = 0; i < bits; ++i) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}

// ── 原地 Radix-2 FFT ─────────────────────────────
inline void transform(std::vector<Complex> &a, bool inverse = false)
{
    size_t n = a.size();
    int bits = 0;
    while ((1 << bits) < int(n)) ++bits;

    // 位反转排列
    for (size_t i = 0; i < n; ++i) {
        size_t j = bitReverse(i, bits);
        if (i < j) std::swap(a[i], a[j]);
    }

    // Cooley-Tukey
    double angleDir = inverse ? 2.0 * 3.14159265358979323846 : -2.0 * 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = angleDir / len;
        Complex wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            Complex w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                Complex u = a[i + j];
                Complex v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto &x : a) x /= static_cast<double>(n);
    }
}

// ── 实信号 → 幅度谱 ──────────────────────────────
// 返回 [0, N/2] 的线性幅度（不含相位）
inline std::vector<double> computeMagnitude(const std::vector<double> &samples)
{
    size_t n = samples.size();
    if (n < 2) return {0.0};

    std::vector<Complex> a(n);
    for (size_t i = 0; i < n; ++i)
        a[i] = Complex(samples[i], 0.0);

    transform(a, false);

    size_t bins = n / 2 + 1;
    std::vector<double> mag(bins);
    for (size_t i = 0; i < bins; ++i)
        mag[i] = std::abs(a[i]);

    return mag;
}

// ── 幅度 → 分贝 ──────────────────────────────────
inline std::vector<double> toDecibel(const std::vector<double> &mag, double ref = 1.0)
{
    std::vector<double> db(mag.size());
    for (size_t i = 0; i < mag.size(); ++i) {
        double v = mag[i] / ref;
        db[i] = v > 1e-12 ? 20.0 * std::log10(v) : -120.0;
    }
    return db;
}

} // namespace FFT

#endif // FFT_H
