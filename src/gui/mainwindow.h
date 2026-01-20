#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>

// 根据平台决定是否使用真实EtherCAT
#if defined(__linux__) && WITH_IGH_ETHERCAT
    #define USE_REAL_ETHERCAT 1
#include "ethercat/EtherCATMaster.h"
#else
    #define USE_REAL_ETHERCAT 0
    #include <random>
#endif

// 前向声明 UI 命名空间
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#if !USE_REAL_ETHERCAT
// 模拟模式下的测试阶段枚举
enum class TestPhase {
    IDLE,           // 空闲
    SUPPORT,        // 支撑阶段
    SUPPORT_WAIT,   // 支撑等待
    RETRACT,        // 收回阶段
    RETRACT_WAIT,   // 收回等待
    COMPLETED       // 完成
};

// 模拟模式下的测试统计结构
struct TestStats {
    int totalCycles = 0;
    int supportSuccess = 0;
    int supportFail = 0;
    int retractSuccess = 0;
    int retractFail = 0;
    float avgSupportTime = 0.0f;
    float avgRetractTime = 0.0f;
    qint64 totalSupportTime = 0;
    qint64 totalRetractTime = 0;
    
    float getSupportSuccessRate() const {
        if (totalCycles == 0) return 0.0f;
        return (supportSuccess * 100.0f) / totalCycles;
    }
    
    float getRetractSuccessRate() const {
        if (totalCycles == 0) return 0.0f;
        return (retractSuccess * 100.0f) / totalCycles;
    }
};
#endif

/**
 * @brief 液压脚撑可靠性测试系统主窗口
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // 继电器控制
    void onRelay1Toggled(bool checked);
    void onRelay2Toggled(bool checked);
    void onRelay3Toggled(bool checked);
    void onRelay4Toggled(bool checked);
    void onAllRelaysOff();
    
    // 测试控制
    void onSupportTest();
    void onRetractTest();
    void onStartReliabilityTest();
    void onStopReliabilityTest();
    
    // 日志操作
    void onClearLog();
    void onExportLog();
    void onExportReport();
    
    // 定时器
    void onUpdateTimer();
    void onReliabilityTestTimer();
    
    // 菜单操作
    void onAbout();

private:
    Ui::MainWindow *ui;
    
    // 定时器
    QTimer *updateTimer;           // 界面更新定时器
    QTimer *reliabilityTestTimer;  // 可靠性测试定时器
    QElapsedTimer systemUptime;    // 系统运行时间
    QElapsedTimer testUptime;      // 测试运行时间
    QElapsedTimer phaseTimer;      // 当前阶段计时

#if USE_REAL_ETHERCAT
    // 真实EtherCAT模式
    std::unique_ptr<EtherCATMaster> master;
    bool masterInitialized = false;
    bool masterRunning = false;
#else
    // 模拟模式
    bool reliabilityTestRunning = false;
    TestPhase currentPhase = TestPhase::IDLE;
    TestStats stats;
    float simulatedPressures[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool relayStates[4] = {false, false, false, false};
    std::mt19937 rng;
#endif
    
    // 测试参数
    float supportTargetPressure = 22.0f;
    float retractTargetPressure = 1.0f;
    int supportTimeoutMs = 15000;
    int retractTimeoutMs = 15000;
    
    // 辅助函数
    void setupConnections();
    void initializeSystem();
    void updatePressureDisplay(int channel, float pressure, const QString& status);
    void updateDigitalInputDisplay(int channel, bool state);
    void updateTestStats();
    void updateSystemUptime();
    void appendLog(const QString& message, const QString& level = "INFO");
    void setControlsEnabled(bool enabled);
    
#if USE_REAL_ETHERCAT
    // 真实EtherCAT模式的回调处理
    void onReliabilityProgress(const ReliabilityTestStats& stats);
    void onLogReceived(const LogEntry& log);
#else
    // 模拟模式函数
    void simulatePressureChanges();
    void executeTestPhase();
    bool checkPressureTarget(float target, bool above);
    void startSupportPhase();
    void startRetractPhase();
    void completeCycle(bool supportSuccess, bool retractSuccess, int supportTime, int retractTime);
#endif
};

#endif // MAINWINDOW_H
