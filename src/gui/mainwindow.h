#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include "ethercat/EtherCATMaster.h"

// 前向声明 UI 命名空间
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

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

    // EtherCAT 主站
    std::unique_ptr<EtherCATMaster> master;
    bool masterInitialized = false;
    bool masterRunning = false;
    
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
    
    // EtherCAT 回调处理
    void onReliabilityProgress(const ReliabilityTestStats& stats);
    void onLogReceived(const LogEntry& log);
};

#endif // MAINWINDOW_H
