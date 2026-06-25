//==============================================================================
// FftProcessor 实现
//==============================================================================

#include "FftProcessor.h"
#include "kiss_fftr.h"
#include <QDateTime>
#include <QtMath>
#include <QDebug>

FftProcessor::FftProcessor(const Config &cfg, MagDataBuffer *buffer,
                           Extractor extractor, QObject *parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_buffer(buffer)
    , m_extractor(extractor)
{
    // 计算目标 bin
    m_targetBin = static_cast<int>(m_cfg.targetFreq * m_cfg.fftSize / m_cfg.sampleRate);

    // 分配缓冲区
    m_window.resize(m_cfg.fftSize);
    m_input.resize(m_cfg.fftSize);
    m_outputReal.resize(m_cfg.fftSize / 2 + 1);
    m_outputImag.resize(m_cfg.fftSize / 2 + 1);

    // 预计算 Hanning 窗
    initWindow();

    // 分配 KissFFT 实数 FFT 配置
    m_fftCfg = kiss_fftr_alloc(m_cfg.fftSize, 0, nullptr, nullptr);

    // 创建定时器
    m_timer = new QTimer(this);
    m_timer->setInterval(m_cfg.updateIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &FftProcessor::onTimer);

    qInfo() << "FftProcessor created: fs=" << m_cfg.sampleRate
            << "N=" << m_cfg.fftSize
            << "f_target=" << m_cfg.targetFreq << "Hz"
            << "bin=" << m_targetBin
            << "update=" << m_cfg.updateIntervalMs << "ms";
}

FftProcessor::~FftProcessor()
{
    stop();
    if (m_fftCfg) {
        kiss_fftr_free(m_fftCfg);
        m_fftCfg = nullptr;
    }
}

void FftProcessor::initWindow()
{
    int N = m_cfg.fftSize;
    for (int i = 0; i < N; ++i) {
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (N - 1)));
    }
}

void FftProcessor::start()
{
    if (m_running) return;
    m_running = true;
    m_timer->start();
    qInfo() << "FftProcessor started";
}

void FftProcessor::stop()
{
    if (!m_running) return;
    m_running = false;
    m_timer->stop();
}

bool FftProcessor::isRunning() const
{
    return m_running;
}

void FftProcessor::onTimer()
{
    // ── 1. 从 DataBuffer 取最新 N 个样本 ──────────────
    QVector<MagDataPoint> snapshot = m_buffer->snapshot();
    int count = snapshot.size();
    int N = m_cfg.fftSize;

    if (count < N) {
        // 数据不足，用零填充或跳过
        if (count < N / 2) return;  // 至少要有半窗数据
        // 零填充不足部分
        for (int i = 0; i < N; ++i) {
            if (i < N - count) {
                m_input[i] = 0.0f;
            } else {
                m_input[i] = static_cast<float>(
                    m_extractor(snapshot[count - N + i]) * m_window[i]);
            }
        }
    } else {
        // 取最新 N 个，加 Hanning 窗
        for (int i = 0; i < N; ++i) {
            m_input[i] = static_cast<float>(
                m_extractor(snapshot[count - N + i]) * m_window[i]);
        }
    }

    // ── 2. 实数 FFT ──────────────────────────────────
    // kiss_fftr 输出: N/2+1 个复数，存入 kiss_fft_cpx 数组
    // 手动管理临时数组
    QVector<kiss_fft_cpx> freqData(N / 2 + 1);
    kiss_fftr(m_fftCfg, m_input.data(), freqData.data());

    // ── 3. 提取目标 bin 的相位 ────────────────────────
    float real = freqData[m_targetBin].r;
    float imag = freqData[m_targetBin].i;

    double phaseRad = std::atan2(static_cast<double>(imag), static_cast<double>(real));
    double phaseDeg = phaseRad * 180.0 / M_PI;

    // 归一化到 [-180, +180]
    while (phaseDeg > 180.0) phaseDeg -= 360.0;
    while (phaseDeg < -180.0) phaseDeg += 360.0;

    m_currentPhase = phaseDeg;

    // ── 4. 发送信号 ──────────────────────────────────
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    emit phaseReady(phaseDeg, ts);
}
