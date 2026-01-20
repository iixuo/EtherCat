#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QScrollBar>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , updateTimer(new QTimer(this))
    , reliabilityTestTimer(new QTimer(this))
#if !USE_REAL_ETHERCAT
    , rng(std::random_device{}())
#endif
{
    ui->setupUi(this);
    
    // 设置信号槽连接
    setupConnections();
    
    // 设置初始状态
    ui->lblSystemStatus->setText("● 正在初始化...");
    ui->lblSystemStatus->setStyleSheet("color: #f59e0b; font-weight: bold; font-size: 16px;");
    setControlsEnabled(false);
    
    // 延迟初始化系统，让窗口先显示出来
    QTimer::singleShot(100, this, &MainWindow::initializeSystem);
}

MainWindow::~MainWindow()
{
    if (updateTimer->isActive()) {
        updateTimer->stop();
    }
    if (reliabilityTestTimer->isActive()) {
        reliabilityTestTimer->stop();
    }
    
#if USE_REAL_ETHERCAT
    if (master && masterRunning) {
        master->stop();
    }
#endif
    
    delete ui;
}

void MainWindow::initializeSystem()
{
    systemUptime.start();
    
#if USE_REAL_ETHERCAT
    // ========== 真实EtherCAT模式 ==========
    appendLog("正在初始化 EtherCAT 主站...", "INFO");
    
    master = std::make_unique<EtherCATMaster>();
    
    // 设置日志回调
    master->setLogCallback([this](const LogEntry& log) {
        QMetaObject::invokeMethod(this, [this, log]() {
            onLogReceived(log);
        }, Qt::QueuedConnection);
    });
    
    // 初始化主站
    if (master->initialize()) {
        masterInitialized = true;
        appendLog("EtherCAT 主站初始化成功", "INFO");
        
        // 启动主站
        if (master->start()) {
            masterRunning = true;
            ui->lblSystemStatus->setText("● 运行中");
            ui->lblSystemStatus->setStyleSheet("color: #2563eb; font-weight: bold; font-size: 16px;");
            appendLog("EtherCAT 主站已启动", "INFO");
            setControlsEnabled(true);
        } else {
            appendLog("EtherCAT 主站启动失败", "ERROR");
            ui->lblSystemStatus->setText("● 启动失败");
            ui->lblSystemStatus->setStyleSheet("color: #333333; font-weight: bold; font-size: 16px;");
        }
    } else {
        appendLog("EtherCAT 主站初始化失败", "ERROR");
        ui->lblSystemStatus->setText("● 初始化失败");
        ui->lblSystemStatus->setStyleSheet("color: #333333; font-weight: bold; font-size: 16px;");
    }
#else
    // ========== 模拟模式 ==========
    appendLog("系统已启动（模拟模式）", "INFO");
    appendLog("注意: 当前为模拟模式，未连接真实硬件", "WARNING");
    
    // 初始化模拟压力值
    for (int i = 0; i < 4; i++) {
        simulatedPressures[i] = 0.5f + (rng() % 100) / 100.0f;
    }
    
    ui->lblSystemStatus->setText("● 运行中（模拟）");
    ui->lblSystemStatus->setStyleSheet("color: #2563eb; font-weight: bold; font-size: 16px;");
    setControlsEnabled(true);
#endif
    
    ui->btnStopReliability->setEnabled(false);
    
    // 启动界面更新定时器 (100ms)
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onUpdateTimer);
    updateTimer->start(100);
    
    // 设置可靠性测试定时器
    connect(reliabilityTestTimer, &QTimer::timeout, this, &MainWindow::onReliabilityTestTimer);
}

void MainWindow::setupConnections()
{
    // 继电器控制按钮
    connect(ui->btnRelay1, &QPushButton::toggled, this, &MainWindow::onRelay1Toggled);
    connect(ui->btnRelay2, &QPushButton::toggled, this, &MainWindow::onRelay2Toggled);
    connect(ui->btnRelay3, &QPushButton::toggled, this, &MainWindow::onRelay3Toggled);
    connect(ui->btnRelay4, &QPushButton::toggled, this, &MainWindow::onRelay4Toggled);
    connect(ui->btnAllRelaysOff, &QPushButton::clicked, this, &MainWindow::onAllRelaysOff);
    
    // 测试控制按钮
    connect(ui->btnSupportTest, &QPushButton::clicked, this, &MainWindow::onSupportTest);
    connect(ui->btnRetractTest, &QPushButton::clicked, this, &MainWindow::onRetractTest);
    connect(ui->btnStartReliability, &QPushButton::clicked, this, &MainWindow::onStartReliabilityTest);
    connect(ui->btnStopReliability, &QPushButton::clicked, this, &MainWindow::onStopReliabilityTest);
    
    // 日志按钮
    connect(ui->btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLog);
    connect(ui->btnExportLog, &QPushButton::clicked, this, &MainWindow::onExportLog);
    connect(ui->btnExportReport, &QPushButton::clicked, this, &MainWindow::onExportReport);
    
    // 菜单动作
    connect(ui->actionSupportTest, &QAction::triggered, this, &MainWindow::onSupportTest);
    connect(ui->actionRetractTest, &QAction::triggered, this, &MainWindow::onRetractTest);
    connect(ui->actionReliabilityTest, &QAction::triggered, this, &MainWindow::onStartReliabilityTest);
    connect(ui->actionExportReport, &QAction::triggered, this, &MainWindow::onExportReport);
    connect(ui->actionExportLog, &QAction::triggered, this, &MainWindow::onExportLog);
    connect(ui->actionExit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

// ==================== 继电器控制 ====================
void MainWindow::onRelay1Toggled(bool checked)
{
#if USE_REAL_ETHERCAT
    if (master && masterRunning) {
        master->setRelayChannel(1, checked);
    }
#else
    relayStates[0] = checked;
#endif
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道1 (支撑) %1").arg(state), "INFO");
}

void MainWindow::onRelay2Toggled(bool checked)
{
#if USE_REAL_ETHERCAT
    if (master && masterRunning) {
        master->setRelayChannel(2, checked);
    }
#else
    relayStates[1] = checked;
#endif
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道2 (收回) %1").arg(state), "INFO");
}

void MainWindow::onRelay3Toggled(bool checked)
{
#if USE_REAL_ETHERCAT
    if (master && masterRunning) {
        master->setRelayChannel(3, checked);
    }
#else
    relayStates[2] = checked;
#endif
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道3 %1").arg(state), "INFO");
}

void MainWindow::onRelay4Toggled(bool checked)
{
#if USE_REAL_ETHERCAT
    if (master && masterRunning) {
        master->setRelayChannel(4, checked);
    }
#else
    relayStates[3] = checked;
#endif
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道4 %1").arg(state), "INFO");
}

void MainWindow::onAllRelaysOff()
{
    ui->btnRelay1->setChecked(false);
    ui->btnRelay2->setChecked(false);
    ui->btnRelay3->setChecked(false);
    ui->btnRelay4->setChecked(false);
    
#if USE_REAL_ETHERCAT
    if (master && masterRunning) {
        master->setAllRelays(false);
    }
#else
    for (int i = 0; i < 4; i++) {
        relayStates[i] = false;
    }
#endif
    appendLog("所有继电器已关闭", "WARNING");
}

// ==================== 测试控制 ====================
void MainWindow::onSupportTest()
{
#if USE_REAL_ETHERCAT
    if (!master || !masterRunning) {
        QMessageBox::warning(this, "警告", "EtherCAT主站未运行");
        return;
    }
    if (master->isReliabilityTestRunning()) {
        QMessageBox::warning(this, "警告", "可靠性测试正在运行，请先停止");
        return;
    }
#else
    if (reliabilityTestRunning) {
        QMessageBox::warning(this, "警告", "可靠性测试正在运行，请先停止");
        return;
    }
#endif
    
    supportTargetPressure = ui->spinSupportTarget->value();
    supportTimeoutMs = ui->spinSupportTimeout->value() * 1000;
    
    appendLog(QString("开始支撑测试 - 目标: %1 bar, 超时: %2 秒")
              .arg(supportTargetPressure).arg(supportTimeoutMs/1000), "INFO");
    
    ui->lblTestStatus->setText("测试状态: 支撑测试进行中...");
    ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #2563eb; border: 1px solid #e5e7eb;");
    
#if USE_REAL_ETHERCAT
    master->startSupportTestAsync(supportTargetPressure, supportTimeoutMs, nullptr,
        [this](const TestResult& result) {
            QMetaObject::invokeMethod(this, [this, result]() {
                if (result.success) {
                    appendLog("支撑测试成功", "INFO");
                    ui->lblTestStatus->setText("测试状态: 支撑测试成功");
                } else {
                    appendLog("支撑测试失败", "ERROR");
                    ui->lblTestStatus->setText("测试状态: 支撑测试失败");
                }
                ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #333333; border: 1px solid #e5e7eb;");
            }, Qt::QueuedConnection);
        });
#else
    // 模拟模式 - 开启支撑继电器
    ui->btnRelay1->setChecked(true);
    ui->btnRelay2->setChecked(false);
    
    currentPhase = TestPhase::SUPPORT;
    phaseTimer.start();
    
    QTimer::singleShot(supportTimeoutMs, this, [this]() {
        if (currentPhase == TestPhase::SUPPORT) {
            bool success = checkPressureTarget(supportTargetPressure, true);
            ui->btnRelay1->setChecked(false);
            currentPhase = TestPhase::IDLE;
            
            if (success) {
                appendLog("支撑测试成功", "INFO");
                ui->lblTestStatus->setText("测试状态: 支撑测试成功");
            } else {
                appendLog("支撑测试失败", "ERROR");
                ui->lblTestStatus->setText("测试状态: 支撑测试失败");
            }
            ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #333333; border: 1px solid #e5e7eb;");
        }
    });
#endif
}

void MainWindow::onRetractTest()
{
#if USE_REAL_ETHERCAT
    if (!master || !masterRunning) {
        QMessageBox::warning(this, "警告", "EtherCAT主站未运行");
        return;
    }
    if (master->isReliabilityTestRunning()) {
        QMessageBox::warning(this, "警告", "可靠性测试正在运行，请先停止");
        return;
    }
#else
    if (reliabilityTestRunning) {
        QMessageBox::warning(this, "警告", "可靠性测试正在运行，请先停止");
        return;
    }
#endif
    
    retractTargetPressure = ui->spinRetractTarget->value();
    retractTimeoutMs = ui->spinRetractTimeout->value() * 1000;
    
    appendLog(QString("开始收回测试 - 目标: < %1 bar, 超时: %2 秒")
              .arg(retractTargetPressure).arg(retractTimeoutMs/1000), "INFO");
    
    ui->lblTestStatus->setText("测试状态: 收回测试进行中...");
    ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #2563eb; border: 1px solid #e5e7eb;");
    
#if USE_REAL_ETHERCAT
    master->startRetractTestAsync(retractTargetPressure, retractTimeoutMs, nullptr,
        [this](const TestResult& result) {
            QMetaObject::invokeMethod(this, [this, result]() {
                if (result.success) {
                    appendLog("收回测试成功", "INFO");
                    ui->lblTestStatus->setText("测试状态: 收回测试成功");
                } else {
                    appendLog("收回测试失败", "ERROR");
                    ui->lblTestStatus->setText("测试状态: 收回测试失败");
                }
                ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #333333; border: 1px solid #e5e7eb;");
            }, Qt::QueuedConnection);
        });
#else
    // 模拟模式
    ui->btnRelay1->setChecked(false);
    ui->btnRelay2->setChecked(true);
    
    currentPhase = TestPhase::RETRACT;
    phaseTimer.start();
    
    QTimer::singleShot(retractTimeoutMs, this, [this]() {
        if (currentPhase == TestPhase::RETRACT) {
            bool success = checkPressureTarget(retractTargetPressure, false);
            ui->btnRelay2->setChecked(false);
            currentPhase = TestPhase::IDLE;
            
            if (success) {
                appendLog("收回测试成功", "INFO");
                ui->lblTestStatus->setText("测试状态: 收回测试成功");
            } else {
                appendLog("收回测试失败", "ERROR");
                ui->lblTestStatus->setText("测试状态: 收回测试失败");
            }
            ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #333333; border: 1px solid #e5e7eb;");
        }
    });
#endif
}

void MainWindow::onStartReliabilityTest()
{
#if USE_REAL_ETHERCAT
    if (!master || !masterRunning) {
        QMessageBox::warning(this, "警告", "EtherCAT主站未运行");
        return;
    }
    if (master->isReliabilityTestRunning()) {
        return;
    }
#else
    if (reliabilityTestRunning) {
        return;
    }
#endif
    
    supportTargetPressure = ui->spinSupportTarget->value();
    retractTargetPressure = ui->spinRetractTarget->value();
    supportTimeoutMs = ui->spinSupportTimeout->value() * 1000;
    retractTimeoutMs = ui->spinRetractTimeout->value() * 1000;
    
    appendLog("========================================", "INFO");
    appendLog("开始可靠性测试", "INFO");
    appendLog(QString("支撑目标: %1 bar, 超时: %2 秒").arg(supportTargetPressure).arg(supportTimeoutMs/1000), "INFO");
    appendLog(QString("收回目标: < %1 bar, 超时: %2 秒").arg(retractTargetPressure).arg(retractTimeoutMs/1000), "INFO");
    appendLog("========================================", "INFO");
    
    testUptime.start();
    
    ui->lblTestStatus->setText("测试状态: 可靠性测试运行中");
    ui->lblTestStatus->setStyleSheet("background-color: #2563eb; padding: 10px; border-radius: 4px; font-size: 14px; color: white; border: none;");
    
    ui->btnStartReliability->setEnabled(false);
    ui->btnStopReliability->setEnabled(true);
    ui->btnSupportTest->setEnabled(false);
    ui->btnRetractTest->setEnabled(false);
    
#if USE_REAL_ETHERCAT
    // 真实EtherCAT模式 - 调用EtherCATMaster的可靠性测试
    master->startInfiniteReliabilityTestAsync(
        supportTargetPressure,
        retractTargetPressure,
        supportTimeoutMs,
        retractTimeoutMs,
        [this](const ReliabilityTestStats& stats) {
            QMetaObject::invokeMethod(this, [this, stats]() {
                onReliabilityProgress(stats);
            }, Qt::QueuedConnection);
        },
        [this](const ReliabilityTestStats& stats) {
            QMetaObject::invokeMethod(this, [this, stats]() {
                appendLog("可靠性测试已完成", "INFO");
                onReliabilityProgress(stats);
            }, Qt::QueuedConnection);
        }
    );
#else
    // 模拟模式
    reliabilityTestRunning = true;
    stats = TestStats();
    startSupportPhase();
    reliabilityTestTimer->start(100);
#endif
}

void MainWindow::onStopReliabilityTest()
{
#if USE_REAL_ETHERCAT
    if (master && master->isReliabilityTestRunning()) {
        master->stopReliabilityTest(true);  // 生成报告
    }
#else
    if (!reliabilityTestRunning) {
        return;
    }
    reliabilityTestRunning = false;
    reliabilityTestTimer->stop();
    currentPhase = TestPhase::IDLE;
    
    ui->btnRelay1->setChecked(false);
    ui->btnRelay2->setChecked(false);
#endif
    
    appendLog("========================================", "WARNING");
    appendLog("可靠性测试已停止", "WARNING");
    appendLog("========================================", "WARNING");
    
    ui->lblTestStatus->setText("测试状态: 已停止");
    ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #666666; border: 1px solid #e5e7eb;");
    
    ui->btnStartReliability->setEnabled(true);
    ui->btnStopReliability->setEnabled(false);
    ui->btnSupportTest->setEnabled(true);
    ui->btnRetractTest->setEnabled(true);
}

#if USE_REAL_ETHERCAT
void MainWindow::onReliabilityProgress(const ReliabilityTestStats& ethercatStats)
{
    // 更新统计显示
    ui->lblCyclesValue->setText(QString::number(ethercatStats.total_cycles));
    ui->lblSupportSuccessValue->setText(QString("%1%").arg(ethercatStats.getSupportSuccessRate(), 0, 'f', 2));
    ui->lblRetractSuccessValue->setText(QString("%1%").arg(ethercatStats.getRetractSuccessRate(), 0, 'f', 2));
    ui->lblAvgSupportValue->setText(QString("%1 ms").arg(static_cast<int>(ethercatStats.avg_support_time_ms)));
    ui->lblAvgRetractValue->setText(QString("%1 ms").arg(static_cast<int>(ethercatStats.avg_retract_time_ms)));
}

void MainWindow::onLogReceived(const LogEntry& log)
{
    QString level;
    switch (log.level) {
        case LogLevel::LOG_DEBUG: level = "DEBUG"; break;
        case LogLevel::LOG_INFO: level = "INFO"; break;
        case LogLevel::LOG_WARNING: level = "WARNING"; break;
        case LogLevel::LOG_ERROR: level = "ERROR"; break;
        case LogLevel::LOG_CRITICAL: level = "ERROR"; break;
        default: level = "INFO"; break;
    }
    appendLog(QString::fromStdString(log.message), level);
}
#else
// ==================== 模拟模式函数 ====================
void MainWindow::startSupportPhase()
{
    currentPhase = TestPhase::SUPPORT;
    phaseTimer.start();
    ui->btnRelay1->setChecked(true);
    ui->btnRelay2->setChecked(false);
    appendLog(QString("周期 %1: 开始支撑阶段").arg(stats.totalCycles + 1), "INFO");
}

void MainWindow::startRetractPhase()
{
    currentPhase = TestPhase::RETRACT;
    phaseTimer.start();
    ui->btnRelay1->setChecked(false);
    ui->btnRelay2->setChecked(true);
    appendLog(QString("周期 %1: 开始收回阶段").arg(stats.totalCycles + 1), "INFO");
}

void MainWindow::completeCycle(bool supportSuccess, bool retractSuccess, int supportTime, int retractTime)
{
    stats.totalCycles++;
    
    if (supportSuccess) {
        stats.supportSuccess++;
        stats.totalSupportTime += supportTime;
    } else {
        stats.supportFail++;
    }
    
    if (retractSuccess) {
        stats.retractSuccess++;
        stats.totalRetractTime += retractTime;
    } else {
        stats.retractFail++;
    }
    
    if (stats.supportSuccess > 0) {
        stats.avgSupportTime = static_cast<float>(stats.totalSupportTime) / stats.supportSuccess;
    }
    if (stats.retractSuccess > 0) {
        stats.avgRetractTime = static_cast<float>(stats.totalRetractTime) / stats.retractSuccess;
    }
    
    QString result = (supportSuccess && retractSuccess) ? "成功" : "失败";
    QString level = (supportSuccess && retractSuccess) ? "INFO" : "WARNING";
    appendLog(QString("周期 %1 完成: %2").arg(stats.totalCycles).arg(result), level);
    
    updateTestStats();
}

bool MainWindow::checkPressureTarget(float target, bool above)
{
    for (int i = 0; i < 4; i++) {
        if (above) {
            if (simulatedPressures[i] < target) return false;
        } else {
            if (simulatedPressures[i] >= target) return false;
        }
    }
    return true;
}

void MainWindow::executeTestPhase()
{
    static bool supportSuccess = false;
    static int supportTime = 0;
    
    int elapsed = phaseTimer.elapsed();
    
    switch (currentPhase) {
        case TestPhase::SUPPORT:
            if (checkPressureTarget(supportTargetPressure, true)) {
                supportSuccess = true;
                supportTime = elapsed;
                currentPhase = TestPhase::SUPPORT_WAIT;
                phaseTimer.start();
            } else if (elapsed >= supportTimeoutMs) {
                supportSuccess = false;
                supportTime = elapsed;
                currentPhase = TestPhase::SUPPORT_WAIT;
                phaseTimer.start();
            }
            break;
            
        case TestPhase::SUPPORT_WAIT:
            if (elapsed >= 500) {
                ui->btnRelay1->setChecked(false);
                startRetractPhase();
            }
            break;
            
        case TestPhase::RETRACT:
            if (checkPressureTarget(retractTargetPressure, false)) {
                completeCycle(supportSuccess, true, supportTime, elapsed);
                currentPhase = TestPhase::RETRACT_WAIT;
                phaseTimer.start();
            } else if (elapsed >= retractTimeoutMs) {
                completeCycle(supportSuccess, false, supportTime, elapsed);
                currentPhase = TestPhase::RETRACT_WAIT;
                phaseTimer.start();
            }
            break;
            
        case TestPhase::RETRACT_WAIT:
            if (elapsed >= 1000) {
                ui->btnRelay2->setChecked(false);
                startSupportPhase();
            }
            break;
            
        default:
            break;
    }
}

void MainWindow::simulatePressureChanges()
{
    float targetPressure = 0.5f;
    float changeRate = 0.5f;
    
    if (relayStates[0]) {
        targetPressure = supportTargetPressure + 2.0f + (rng() % 30) / 10.0f;
        changeRate = 2.0f;
    } else if (relayStates[1]) {
        targetPressure = 0.3f + (rng() % 5) / 10.0f;
        changeRate = 1.5f;
    }
    
    for (int i = 0; i < 4; i++) {
        float diff = targetPressure - simulatedPressures[i];
        float noise = (rng() % 100 - 50) / 500.0f;
        simulatedPressures[i] += diff * 0.1f * changeRate + noise;
        if (simulatedPressures[i] < 0) simulatedPressures[i] = 0;
        if (simulatedPressures[i] > 100) simulatedPressures[i] = 100;
    }
}
#endif

void MainWindow::onReliabilityTestTimer()
{
#if !USE_REAL_ETHERCAT
    if (!reliabilityTestRunning) return;
    executeTestPhase();
#endif
}

// ==================== 日志操作 ====================
void MainWindow::onClearLog()
{
    ui->txtLog->clear();
    appendLog("日志已清空", "INFO");
}

void MainWindow::onExportLog()
{
    QString fileName = QFileDialog::getSaveFileName(this, 
        "导出日志", 
        QString("log_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "文本文件 (*.txt);;所有文件 (*)");
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << ui->txtLog->toPlainText();
            file.close();
            appendLog(QString("日志已导出到: %1").arg(fileName), "INFO");
        }
    }
}

void MainWindow::onExportReport()
{
    QString fileName = QFileDialog::getSaveFileName(this, 
        "导出测试报告", 
        QString("report_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "文本文件 (*.txt);;所有文件 (*)");
    
    if (!fileName.isEmpty()) {
#if USE_REAL_ETHERCAT
        if (master) {
            auto ethercatStats = master->getReliabilityTestStats();
            master->saveTestResultsToFile(fileName.toStdString(), ethercatStats);
            appendLog(QString("测试报告已导出到: %1").arg(fileName), "INFO");
        }
#else
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "========================================\n";
            out << "液压脚撑可靠性测试报告\n";
            out << "========================================\n\n";
            out << "生成时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n\n";
            out << "测试参数:\n";
            out << QString("  支撑目标压力: %1 bar\n").arg(supportTargetPressure);
            out << QString("  收回目标压力: < %1 bar\n").arg(retractTargetPressure);
            out << QString("  支撑超时: %1 秒\n").arg(supportTimeoutMs / 1000);
            out << QString("  收回超时: %1 秒\n\n").arg(retractTimeoutMs / 1000);
            out << "测试结果:\n";
            out << QString("  总测试周期: %1\n").arg(stats.totalCycles);
            out << QString("  支撑成功率: %1%\n").arg(stats.getSupportSuccessRate(), 0, 'f', 2);
            out << QString("  收回成功率: %1%\n").arg(stats.getRetractSuccessRate(), 0, 'f', 2);
            out << QString("  平均支撑时间: %1 ms\n").arg(static_cast<int>(stats.avgSupportTime));
            out << QString("  平均收回时间: %1 ms\n").arg(static_cast<int>(stats.avgRetractTime));
            out << "========================================\n";
            file.close();
            appendLog(QString("测试报告已导出到: %1").arg(fileName), "INFO");
        }
#endif
    }
}

void MainWindow::onAbout()
{
    QString mode;
#if USE_REAL_ETHERCAT
    mode = "EtherCAT硬件模式";
#else
    mode = "模拟模式";
#endif
    
    QMessageBox::about(this, "关于",
        QString("<h2>液压脚撑可靠性测试系统</h2>"
        "<p>版本: 1.0.0</p>"
        "<p>当前模式: %1</p>"
        "<hr>"
        "<p><b>功能:</b></p>"
        "<ul>"
        "<li>压力传感器监控 (4通道)</li>"
        "<li>继电器控制 (4通道)</li>"
        "<li>自动化可靠性测试</li>"
        "</ul>").arg(mode));
}

// ==================== 定时器更新 ====================
void MainWindow::onUpdateTimer()
{
#if USE_REAL_ETHERCAT
    // 从真实硬件读取压力
    if (master && masterRunning) {
        auto pressures = master->readAllAnalogInputsAsPressure();
        for (size_t i = 0; i < pressures.size() && i < 4; i++) {
            auto status = master->checkPressureStatus(i + 1);
            QString statusStr = QString::fromStdString(master->getPressureStatusString(status));
            updatePressureDisplay(i + 1, pressures[i], statusStr);
        }
        
        auto digitalInputs = master->readAllDigitalInputs();
        for (size_t i = 0; i < digitalInputs.size() && i < 8; i++) {
            updateDigitalInputDisplay(i + 1, digitalInputs[i]);
        }
    }
#else
    // 模拟压力变化
    simulatePressureChanges();
    
    for (int i = 0; i < 4; i++) {
        updatePressureDisplay(i + 1, simulatedPressures[i], "正常");
    }
    
    static int counter = 0;
    counter++;
    for (int i = 1; i <= 8; i++) {
        bool state = ((counter + i * 17) % 7) < 2;
        updateDigitalInputDisplay(i, state);
    }
#endif
    
    updateSystemUptime();
    
#if USE_REAL_ETHERCAT
    if (master && master->isReliabilityTestRunning()) {
        updateTestStats();
    }
#else
    if (reliabilityTestRunning) {
        updateTestStats();
    }
#endif
}

// ==================== 辅助函数 ====================
void MainWindow::updatePressureDisplay(int channel, float pressure, const QString& status)
{
    QProgressBar* progress = nullptr;
    QLabel* valueLabel = nullptr;
    QLabel* statusLabel = nullptr;
    
    switch (channel) {
        case 1: progress = ui->progressP1; valueLabel = ui->lblP1Value; statusLabel = ui->lblP1Status; break;
        case 2: progress = ui->progressP2; valueLabel = ui->lblP2Value; statusLabel = ui->lblP2Status; break;
        case 3: progress = ui->progressP3; valueLabel = ui->lblP3Value; statusLabel = ui->lblP3Status; break;
        case 4: progress = ui->progressP4; valueLabel = ui->lblP4Value; statusLabel = ui->lblP4Status; break;
        default: return;
    }
    
    if (progress && valueLabel && statusLabel) {
        progress->setValue(static_cast<int>(pressure));
        valueLabel->setText(QString("%1 bar").arg(pressure, 0, 'f', 2));
        statusLabel->setText(status);
        valueLabel->setStyleSheet("color: #333333; font-size: 16px; font-weight: bold;");
        statusLabel->setStyleSheet("color: #666666;");
    }
}

void MainWindow::updateDigitalInputDisplay(int channel, bool state)
{
    QLabel* label = nullptr;
    switch (channel) {
        case 1: label = ui->lblDI1; break;
        case 2: label = ui->lblDI2; break;
        case 3: label = ui->lblDI3; break;
        case 4: label = ui->lblDI4; break;
        case 5: label = ui->lblDI5; break;
        case 6: label = ui->lblDI6; break;
        case 7: label = ui->lblDI7; break;
        case 8: label = ui->lblDI8; break;
        default: return;
    }
    
    if (label) {
        label->setText(QString("DI%1: %2").arg(channel).arg(state ? "ON" : "OFF"));
        QString bgColor = state ? "#2563eb" : "#f3f4f6";
        QString textColor = state ? "white" : "#666666";
        label->setStyleSheet(QString("background-color: %1; padding: 8px; border-radius: 4px; color: %2;").arg(bgColor).arg(textColor));
    }
}

void MainWindow::updateTestStats()
{
#if USE_REAL_ETHERCAT
    if (master) {
        auto ethercatStats = master->getReliabilityTestStats();
        ui->lblCyclesValue->setText(QString::number(ethercatStats.total_cycles));
        ui->lblSupportSuccessValue->setText(QString("%1%").arg(ethercatStats.getSupportSuccessRate(), 0, 'f', 2));
        ui->lblRetractSuccessValue->setText(QString("%1%").arg(ethercatStats.getRetractSuccessRate(), 0, 'f', 2));
        ui->lblAvgSupportValue->setText(QString("%1 ms").arg(static_cast<int>(ethercatStats.avg_support_time_ms)));
        ui->lblAvgRetractValue->setText(QString("%1 ms").arg(static_cast<int>(ethercatStats.avg_retract_time_ms)));
    }
#else
    ui->lblCyclesValue->setText(QString::number(stats.totalCycles));
    ui->lblSupportSuccessValue->setText(QString("%1%").arg(stats.getSupportSuccessRate(), 0, 'f', 2));
    ui->lblRetractSuccessValue->setText(QString("%1%").arg(stats.getRetractSuccessRate(), 0, 'f', 2));
    ui->lblAvgSupportValue->setText(QString("%1 ms").arg(static_cast<int>(stats.avgSupportTime)));
    ui->lblAvgRetractValue->setText(QString("%1 ms").arg(static_cast<int>(stats.avgRetractTime)));
#endif
    
    if (testUptime.isValid()) {
        qint64 elapsed = testUptime.elapsed();
        int hours = elapsed / 3600000;
        int minutes = (elapsed % 3600000) / 60000;
        int seconds = (elapsed % 60000) / 1000;
        ui->lblRuntimeValue->setText(QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0')));
    }
}

void MainWindow::updateSystemUptime()
{
    if (systemUptime.isValid()) {
        qint64 elapsed = systemUptime.elapsed();
        int hours = elapsed / 3600000;
        int minutes = (elapsed % 3600000) / 60000;
        int seconds = (elapsed % 60000) / 1000;
        ui->lblUptime->setText(QString("运行时长: %1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0')));
    }
}

void MainWindow::appendLog(const QString& message, const QString& level)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString color = "#333333";
    
    if (level == "WARNING" || level == "ERROR") {
        color = "#333333";
    } else if (level == "INFO") {
        color = "#2563eb";
    }
    
    QString html = QString("<span style='color: #999999;'>%1</span> "
                          "<span style='color: %2;'>[%3]</span> "
                          "<span style='color: #333333;'>%4</span>")
                   .arg(timestamp).arg(color).arg(level).arg(message);
    
    ui->txtLog->append(html);
    ui->txtLog->verticalScrollBar()->setValue(ui->txtLog->verticalScrollBar()->maximum());
}

void MainWindow::setControlsEnabled(bool enabled)
{
    ui->btnRelay1->setEnabled(enabled);
    ui->btnRelay2->setEnabled(enabled);
    ui->btnRelay3->setEnabled(enabled);
    ui->btnRelay4->setEnabled(enabled);
    ui->btnAllRelaysOff->setEnabled(enabled);
    ui->btnSupportTest->setEnabled(enabled);
    ui->btnRetractTest->setEnabled(enabled);
    ui->btnStartReliability->setEnabled(enabled);
}
