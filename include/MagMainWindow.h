//==============================================================================
// MagMainWindow — 磁场相位分析主窗口（v2.0）
//
// 双通道独立管道 + FFT + 相位波形展示
//==============================================================================

#ifndef MAGMAINWINDOW_H
#define MAGMAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QLabel>
#include <QSplitter>
#include "ChannelManager.h"
#include "ChannelRegistry.h"
#include "ConnectionDialog.h"
#include "MagCollectorDecoder.h"
#include "DollarParser.h"
#include "MagFormula.h"
#include "MagDataPoint.h"
#include "MagDataBuffer.h"
#include "FftProcessor.h"
#include "DataBuffer.h"
#include "DataPoint.h"
#include "RealTimeChart.h"

class MagMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MagMainWindow(QWidget *parent = nullptr);
    ~MagMainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralArea();
    void setupStatusBar();

    void createTcpPipeline();
    void createSerialPipeline();
    void createFftProcessors();       // 在管道就绪后创建 FFT
    void teardownTcpPipeline();
    void teardownSerialPipeline();
    void teardownFft();
    void teardownAll();

    // 当任一 FFT 输出新相位时更新展示
    void onPhaseTcpX1(double deg, qint64 ts);
    void onPhaseTcpY1(double deg, qint64 ts);
    void onPhaseSerial(double deg, qint64 ts);
    void pushPhaseDisplay();

    // ── UI ────────────────────────────────────────────
    QLabel *m_statusTcp    = nullptr;
    QLabel *m_statusSerial = nullptr;
    QLabel *m_statusFft    = nullptr;

    bool m_tcpConnected    = false;
    bool m_serConnected    = false;
    bool m_fftRunning      = false;
    bool m_tearingDown     = false;   // 防双重 teardown

    // ═══════════════════════════════════════════════════
    // TCP 管道
    // ═══════════════════════════════════════════════════
    QThread             *m_tcpCommThread     = nullptr;
    QThread             *m_tcpParseThread    = nullptr;
    ChannelManager      *m_tcpChannelManager = nullptr;
    MagCollectorDecoder *m_tcpDecoder        = nullptr;
    MagDataBuffer       *m_tcpMagBuffer      = nullptr;

    // ═══════════════════════════════════════════════════
    // 串口管道
    // ═══════════════════════════════════════════════════
    QThread         *m_serCommThread     = nullptr;
    QThread         *m_serParseThread    = nullptr;
    ChannelManager  *m_serChannelManager = nullptr;
    DollarParser    *m_serParser         = nullptr;
    MagDataBuffer   *m_serMagBuffer      = nullptr;

    // ═══════════════════════════════════════════════════
    // FFT
    // ═══════════════════════════════════════════════════
    QThread       *m_fftThread = nullptr;
    FftProcessor  *m_fftX1     = nullptr;
    FftProcessor  *m_fftY1     = nullptr;
    FftProcessor  *m_fftSerial = nullptr;

    // ── 相位展示缓冲 ───────────────────────────────────
    DataBuffer    *m_phaseBuf = nullptr;   // [φ_X1, φ_Y1, φ_Serial]
    DataBuffer    *m_diffBuf  = nullptr;   // [Δφ_X1-S, Δφ_Y1-S]

    // ── 最新相位值缓存 ─────────────────────────────────
    double m_phiX1     = 0.0;
    double m_phiY1     = 0.0;
    double m_phiSerial = 0.0;
    bool   m_hasX1     = false;
    bool   m_hasY1     = false;
    bool   m_hasSerial = false;

    // ── phase unwrap 累积值 ─────────────────────────────
    double m_unwrapX1     = 0.0;
    double m_unwrapY1     = 0.0;
    double m_unwrapSerial = 0.0;
    double m_unwrapPrevX1     = 0.0;
    double m_unwrapPrevY1     = 0.0;
    double m_unwrapPrevSerial = 0.0;
    bool   m_unwrapInitX1     = false;
    bool   m_unwrapInitY1     = false;
    bool   m_unwrapInitSerial = false;

    // ── 展示 ───────────────────────────────────────────
    RealTimeChart *m_phaseChart = nullptr;
    RealTimeChart *m_diffChart  = nullptr;

private slots:
    void onConnectTcp();
    void onDisconnectTcp();
    void onConnectSerial();
    void onDisconnectSerial();
};

#endif // MAGMAINWINDOW_H
