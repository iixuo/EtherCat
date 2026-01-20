#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QScrollBar>
#include <QCoreApplication>
#include <QDebug>
#include <iostream>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , updateTimer(new QTimer(this))
    , reliabilityTestTimer(new QTimer(this))
{
    ui->setupUi(this);
    
    // 设置信号槽连接
    setupConnections();
    
    // 确保窗口显示
    show();
    
    // 延迟初始化，让窗口先完全显示（500ms）
    QTimer::singleShot(500, this, &MainWindow::initializeSystem);
}

MainWindow::~MainWindow()
{
    if (updateTimer->isActive()) {
        updateTimer->stop();
    }
    if (reliabilityTestTimer->isActive()) {
        reliabilityTestTimer->stop();
    }
    
    if (master && masterRunning) {
        master->stop();
    }
    
    delete ui;
}

void MainWindow::initializeSystem()
{
    // 强制处理所有待处理的事件，确保 UI 完全显示
    QCoreApplication::processEvents();
    
    systemUptime.start();
    
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
    if (master && masterRunning) {
        master->setRelayChannel(1, checked);
    }
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道1 (支撑) %1").arg(state), "INFO");
}

void MainWindow::onRelay2Toggled(bool checked)
{
    if (master && masterRunning) {
        master->setRelayChannel(2, checked);
    }
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道2 (收回) %1").arg(state), "INFO");
}

void MainWindow::onRelay3Toggled(bool checked)
{
    if (master && masterRunning) {
        master->setRelayChannel(3, checked);
    }
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道3 %1").arg(state), "INFO");
}

void MainWindow::onRelay4Toggled(bool checked)
{
    if (master && masterRunning) {
        master->setRelayChannel(4, checked);
    }
    QString state = checked ? "开启" : "关闭";
    appendLog(QString("继电器通道4 %1").arg(state), "INFO");
}

void MainWindow::onAllRelaysOff()
{
    ui->btnRelay1->setChecked(false);
    ui->btnRelay2->setChecked(false);
    ui->btnRelay3->setChecked(false);
    ui->btnRelay4->setChecked(false);
    
    if (master && masterRunning) {
        master->setAllRelays(false);
    }
    appendLog("所有继电器已关闭", "WARNING");
}

// ==================== 测试控制 ====================
void MainWindow::onSupportTest()
{
    if (!master || !masterRunning) {
        QMessageBox::warning(this, "警告", "EtherCAT主站未运行");
        return;
    }
    if (master->isReliabilityTestRunning()) {
        QMessageBox::warning(this, "警告", "可靠性测试正在运行，请先停止");
        return;
    }
    
    supportTargetPressure = ui->spinSupportTarget->value();
    supportTimeoutMs = ui->spinSupportTimeout->value() * 1000;
    
    appendLog(QString("开始支撑测试 - 目标: %1 bar, 超时: %2 秒")
              .arg(supportTargetPressure).arg(supportTimeoutMs/1000), "INFO");
    
    ui->lblTestStatus->setText("测试状态: 支撑测试进行中...");
    ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #2563eb; border: 1px solid #e5e7eb;");
    
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
}

void MainWindow::onRetractTest()
{
    if (!master || !masterRunning) {
        QMessageBox::warning(this, "警告", "EtherCAT主站未运行");
        return;
    }
    if (master->isReliabilityTestRunning()) {
        QMessageBox::warning(this, "警告", "可靠性测试正在运行，请先停止");
        return;
    }
    
    retractTargetPressure = ui->spinRetractTarget->value();
    retractTimeoutMs = ui->spinRetractTimeout->value() * 1000;
    
    appendLog(QString("开始收回测试 - 目标: < %1 bar, 超时: %2 秒")
              .arg(retractTargetPressure).arg(retractTimeoutMs/1000), "INFO");
    
    ui->lblTestStatus->setText("测试状态: 收回测试进行中...");
    ui->lblTestStatus->setStyleSheet("background-color: #f3f4f6; padding: 10px; border-radius: 4px; font-size: 14px; color: #2563eb; border: 1px solid #e5e7eb;");
    
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
}

void MainWindow::onStartReliabilityTest()
{
    if (!master || !masterRunning) {
        QMessageBox::warning(this, "警告", "EtherCAT主站未运行");
        return;
    }
    if (master->isReliabilityTestRunning()) {
        return;
    }
    
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
}

void MainWindow::onStopReliabilityTest()
{
    if (master && master->isReliabilityTestRunning()) {
        master->stopReliabilityTest(true);  // 生成报告
    }
    
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

void MainWindow::onReliabilityTestTimer()
{
    // 空实现 - 真实模式使用 EtherCATMaster 内部线程
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
        if (master) {
            auto ethercatStats = master->getReliabilityTestStats();
            master->saveTestResultsToFile(fileName.toStdString(), ethercatStats);
            appendLog(QString("测试报告已导出到: %1").arg(fileName), "INFO");
        }
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "关于",
        QString("<h2>液压脚撑可靠性测试系统</h2>"
        "<p>版本: 1.0.0</p>"
        "<p>当前模式: EtherCAT硬件模式</p>"
        "<hr>"
        "<p><b>功能:</b></p>"
        "<ul>"
        "<li>压力传感器监控 (4通道)</li>"
        "<li>继电器控制 (4通道)</li>"
        "<li>自动化可靠性测试</li>"
        "</ul>"));
}

// ==================== 定时器更新 ====================
void MainWindow::onUpdateTimer()
{
    // 调试：每秒打印一次状态
    static int timerCounter = 0;
    timerCounter++;
    if (timerCounter % 10 == 0) {
        std::cout << "[UI] 定时器 #" << timerCounter 
                  << " master=" << (master ? "有效" : "空") 
                  << " masterRunning=" << masterRunning << std::endl;
    }
    
    // 从真实硬件读取压力
    if (master && masterRunning) {
        // 每5秒打印一次详细调试信息
        static int debugCounter = 0;
        debugCounter++;
        if (debugCounter % 50 == 0) {
            std::cout << "===== [UI] 读取传感器数据 =====" << std::endl;
            auto currents = master->readAllAnalogInputsAsCurrent();
            for (size_t i = 0; i < currents.size(); i++) {
                std::cout << "  通道 " << (i+1) << " 电流: " << currents[i] << " mA" << std::endl;
            }
        }
        
        auto pressures = master->readAllAnalogInputsAsPressure();
        
        // 每秒打印一次压力值
        if (timerCounter % 10 == 0) {
            std::cout << "[UI] 压力值: ";
            for (size_t i = 0; i < pressures.size(); i++) {
                std::cout << "P" << (i+1) << "=" << pressures[i] << "bar ";
            }
            std::cout << std::endl;
        }
        
        for (size_t i = 0; i < pressures.size() && i < 4; i++) {
            auto status = master->checkPressureStatus(i + 1);
            QString statusStr = QString::fromStdString(master->getPressureStatusString(status));
            updatePressureDisplay(i + 1, pressures[i], statusStr);
        }
        
        auto digitalInputs = master->readAllDigitalInputs();
        for (size_t i = 0; i < digitalInputs.size() && i < 8; i++) {
            updateDigitalInputDisplay(i + 1, digitalInputs[i]);
        }
    } else {
        if (timerCounter % 10 == 0) {
            std::cout << "[UI] 跳过读取: master=" << (master ? "有效" : "空") 
                      << " masterRunning=" << masterRunning << std::endl;
        }
    }
    
    updateSystemUptime();
    
    if (master && master->isReliabilityTestRunning()) {
        updateTestStats();
    }
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
    if (master) {
        auto ethercatStats = master->getReliabilityTestStats();
        ui->lblCyclesValue->setText(QString::number(ethercatStats.total_cycles));
        ui->lblSupportSuccessValue->setText(QString("%1%").arg(ethercatStats.getSupportSuccessRate(), 0, 'f', 2));
        ui->lblRetractSuccessValue->setText(QString("%1%").arg(ethercatStats.getRetractSuccessRate(), 0, 'f', 2));
        ui->lblAvgSupportValue->setText(QString("%1 ms").arg(static_cast<int>(ethercatStats.avg_support_time_ms)));
        ui->lblAvgRetractValue->setText(QString("%1 ms").arg(static_cast<int>(ethercatStats.avg_retract_time_ms)));
    }
    
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
