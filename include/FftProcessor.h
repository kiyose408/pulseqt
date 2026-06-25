//==============================================================================
// FftProcessor — 滑动窗口 FFT 相位提取器
//
// 从 MagDataBuffer 消费磁场值，定时执行 FFT，提取指定频率的相位角。
//
// 设计:
//   - 构造时配置采样率、FFT 点数、目标频率、更新间隔
//   - QTimer 定时触发 (默认 20ms = 50Hz)
//   - 每次从 MagDataBuffer snapshot 取最新 N 个样本
//   - Hanning 窗 → KissFFT (实数) → 取目标 bin → atan2 → 角度
//   - 通过 extractor lambda 从 MagDataPoint 提取所需磁场值列
//
// 使用:
//   FftProcessor::Config cfg;
//   cfg.sampleRate = 1000; cfg.fftSize = 1000; cfg.targetFreq = 50.0;
//   auto *fft = new FftProcessor(cfg, buffer,
//       [](const MagDataPoint &p) { return p.data[0]; });  // 提取 X1
//   connect(fft, &FftProcessor::phaseReady, ...);
//   fft->start();
//==============================================================================

#ifndef FFTPROCESSOR_H
#define FFTPROCESSOR_H

#include <QObject>
#include <QTimer>
#include <QVector>
#include <functional>
#include "MagDataPoint.h"
#include "MagDataBuffer.h"

// 前向声明 KissFFT 类型
typedef struct kiss_fftr_state* kiss_fftr_cfg;

class FftProcessor : public QObject
{
    Q_OBJECT

public:
    struct Config {
        int    sampleRate       = 1000;   // 采样率 Hz (TCP=1000, Serial=250)
        int    fftSize          = 1000;   // FFT 点数 (TCP=1000, Serial=250)
        double targetFreq       = 50.0;   // 目标频率 Hz
        int    updateIntervalMs = 20;     // FFT 更新间隔 ms (= 50Hz)
    };

    // extractor: 从 MagDataPoint 提取磁场值 (如 p.data[0] 取 X1)
    using Extractor = std::function<double(const MagDataPoint &)>;

    FftProcessor(const Config &cfg, MagDataBuffer *buffer,
                 Extractor extractor, QObject *parent = nullptr);
    ~FftProcessor() override;

public slots:
    void start();
    void stop();

public:
    bool isRunning() const;

    // 当前相位角 (度)
    double currentPhase() const { return m_currentPhase; }

signals:
    void phaseReady(double phaseDeg, qint64 timestamp);

public slots:
    void onTimer();   // 供测试直接调用

private:
    void initWindow();   // 预计算 Hanning 窗

    Config        m_cfg;
    MagDataBuffer *m_buffer;
    Extractor     m_extractor;
    QTimer        *m_timer = nullptr;

    kiss_fftr_cfg m_fftCfg = nullptr;      // KissFFT 实数 FFT 配置
    QVector<float> m_window;                // Hanning 窗系数
    QVector<float> m_input;                 // 实数输入 (N)
    QVector<float> m_outputReal;            // 复数输出实部 (N/2+1)
    QVector<float> m_outputImag;            // 复数输出虚部 (N/2+1)

    int    m_targetBin = 0;                 // 目标频率对应的 bin 索引
    double m_currentPhase = 0.0;            // 最近一次相位角
    bool   m_running = false;
};

#endif // FFTPROCESSOR_H
