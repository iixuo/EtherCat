#include "ethercat/EtherCATMaster.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstring>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

// 全局变量用于信号处理
static EtherCATMaster* g_master_instance = nullptr;
static bool g_hotkey_enabled = false;

// 原始终端设置备份
static struct termios g_original_termios;

// 恢复终端设置
void restoreTerminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    std::cout << "终端设置已恢复" << std::endl;
}

// 设置非阻塞终端输入
void setNonBlockingTerminal() {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &g_original_termios);
    new_termios = g_original_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO); // 非规范模式，不回显
    new_termios.c_cc[VMIN] = 0;  // 最小读取字符数
    new_termios.c_cc[VTIME] = 0; // 等待时间（0表示不等待）
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    // 设置程序退出时恢复终端
    std::atexit(restoreTerminal);
}

void signalHandler(int signum) {
    std::cout << std::endl << "接收到信号 " << signum << "，停止程序..." << std::endl;
    if (g_master_instance) {
        g_master_instance->stop();
    }
}

// 快捷键处理函数
void handleHotkey(int ch) {
    if (!g_master_instance) return;
    
    std::cout << std::endl << "=== 快捷键处理 ===" << std::endl;
    
    switch (ch) {
        case 's': // s - 显示统计
        case 'S':
            std::cout << "显示当前测试统计..." << std::endl;
            {
                auto stats = g_master_instance->getReliabilityTestStats();
                g_master_instance->printReliabilityTestReport(stats);
            }
            break;
            
        case 'p': // p - 暂停/继续测试
        case 'P':
            std::cout << "暂停/继续功能待实现..." << std::endl;
            // 这里可以添加暂停/继续逻辑
            break;
            
        case 'l': // l - 显示最近日志
        case 'L':
            std::cout << "显示最近日志..." << std::endl;
            {
                auto logs = g_master_instance->getRecentLogs(20);
                std::cout << "=== 最近20条日志 ===" << std::endl;
                for (const auto& log : logs) {
                    std::cout << log.toString() << std::endl;
                }
                std::cout << "==================" << std::endl;
            }
            break;
            
        case 'e': // e - 结束测试并生成报告
        case 'E':
            std::cout << "结束可靠性测试并生成报告..." << std::endl;
            g_master_instance->stopReliabilityTest(true);
            break;
            
        case 'c': // c - 只结束测试不生成报告
        case 'C':
            std::cout << "结束可靠性测试..." << std::endl;
            g_master_instance->stopReliabilityTest(false);
            break;
            
        case 'h': // h - 显示帮助
        case 'H':
        case '?':
            std::cout << "=== 快捷键帮助 ===" << std::endl;
            std::cout << "s/S - 显示测试统计" << std::endl;
            std::cout << "p/P - 暂停/继续测试" << std::endl;
            std::cout << "l/L - 显示最近日志" << std::endl;
            std::cout << "e/E - 结束测试并生成报告" << std::endl;
            std::cout << "c/C - 结束测试不生成报告" << std::endl;
            std::cout << "h/H/? - 显示帮助" << std::endl;
            std::cout << "q/Q - 退出程序" << std::endl;
            std::cout << "=================" << std::endl;
            break;
            
        case 'q': // q - 退出程序
        case 'Q':
            std::cout << "退出程序..." << std::endl;
            if (g_master_instance) {
                g_master_instance->stop();
            }
            exit(0);
            break;
            
        default:
            std::cout << "未知快捷键: '" << (char)ch << "' (ASCII: " << ch << ")" << std::endl;
            std::cout << "按 'h' 或 '?' 查看帮助" << std::endl;
            break;
    }
    
    std::cout << "按任意键继续..." << std::endl;
}

// 快捷键监听线程函数（独立于主程序）
void hotkeyListener() {
    setNonBlockingTerminal();
    
    std::cout << "快捷键监听已启动" << std::endl;
    std::cout << "按 'h' 或 '?' 查看快捷键帮助" << std::endl;
    
    while (g_hotkey_enabled) {
        int ch = getchar();
        if (ch != EOF && ch != '\n') {
            handleHotkey(ch);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

EtherCATMaster::EtherCATMaster() 
    : master(nullptr)
    , domain(nullptr)
    , domain_data(nullptr)
    , relay_states(0)
    , initialized(false)
    , running(false)
    , current_status(MasterStatus::STATUS_UNINITIALIZED)
    , test_running(false)
    , test_cancelled(false)
    , current_test_status(TestStatus::TEST_IDLE)
    , infinite_test_running(false)
    , stop_infinite_test(false)
    , log_to_file(false)
    , log_file_counter(0)
    , hotkey_listening(false) {
    
    // 初始化偏移量
    memset(off_dig_in, 0, sizeof(off_dig_in));
    memset(off_ai_val, 0, sizeof(off_ai_val));
    memset(off_relay_out, 0, sizeof(off_relay_out));
    
    // 初始化状态结构
    memset(&master_state, 0, sizeof(master_state));
    memset(&domain_state, 0, sizeof(domain_state));
    
    // 设置信号处理
    g_master_instance = this;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // 初始化日志记录
    last_log_file_check = std::chrono::system_clock::now();
    
    // 注意：不在GUI模式下启动快捷键监听，因为会干扰Qt事件循环
    // 快捷键监听仅在命令行模式下使用
    // if (!g_hotkey_enabled) {
    //     g_hotkey_enabled = true;
    //     std::thread(hotkeyListener).detach();
    // }
}

EtherCATMaster::~EtherCATMaster() {
    stop();
    g_master_instance = nullptr;
    g_hotkey_enabled = false;
}

bool EtherCATMaster::initialize() {
    std::cout << "初始化 EtherCAT 主站..." << std::endl;
    
    // 获取主站
    master = ecrt_request_master(0);
    if (!master) {
        std::cerr << "错误: 无法请求 EtherCAT 主站" << std::endl;
        return false;
    }
    std::cout << "EtherCAT 主站请求成功" << std::endl;

    // 创建域
    domain = ecrt_master_create_domain(master);
    if (!domain) {
        std::cerr << "错误: 无法创建域" << std::endl;
        ecrt_release_master(master);
        master = nullptr;
        return false;
    }
    std::cout << "域创建成功" << std::endl;

    // 配置从站和PDO映射
    if (!configureSlaves()) {
        std::cerr << "错误: 从站配置失败" << std::endl;
        ecrt_release_master(master);
        master = nullptr;
        return false;
    }

    initialized = true;
    std::cout << "EtherCAT 主站初始化成功" << std::endl;
    return true;
}

// 新增：检查主站健康状态
bool EtherCATMaster::checkMasterHealth() {
    if (!master) {
        return false;
    }
    
    ec_master_state_t ms;
    ecrt_master_state(master, &ms);
    
    // 检查链接状态
    if (!ms.link_up) {
        std::cerr << "警告: EtherCAT 链接断开" << std::endl;
        current_status = MasterStatus::STATUS_ERROR;
        return false;
    }
    
    // 检查从站响应数量 (实际配置: EK1100, EL1008, EL3074, EL2634, EL6001, EL6751 = 6个)
    if (ms.slaves_responding != 6) {
        std::cerr << "警告: 从站响应数量异常，期望6个，实际" << ms.slaves_responding << "个" << std::endl;
        current_status = MasterStatus::STATUS_WARNING;
    } else {
        std::cout << "从站响应正常: " << ms.slaves_responding << "个" << std::endl;
    }
    
    // 检查应用层状态
    if ((ms.al_states & 0x08) == 0) { // 检查是否在OP状态
        std::cout << "注意: 应用层状态: 0x" << std::hex << static_cast<int>(ms.al_states) << std::dec << std::endl;
        current_status = MasterStatus::STATUS_WARNING;
    } else {
        current_status = MasterStatus::STATUS_OPERATIONAL;
    }
    
    // 更新状态信息
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        master_state_info.slaves_responding = ms.slaves_responding;
        master_state_info.al_states = ms.al_states;
        master_state_info.link_up = ms.link_up != 0;
        master_state_info.last_update = std::chrono::system_clock::now();
    }
    
    return current_status == MasterStatus::STATUS_OPERATIONAL || 
           current_status == MasterStatus::STATUS_WARNING;
}

// 新增：验证操作可行性
bool EtherCATMaster::verifyOperation(const std::string& operation_name) {
    if (!running) {
        std::cerr << "错误: " << operation_name << " - 主站未运行" << std::endl;
        return false;
    }
    
    if (!checkMasterHealth()) {
        std::cerr << "错误: " << operation_name << " - 主站健康状态检查失败" << std::endl;
        
        // 显示详细状态信息
        printHealthStatus();
        
        // 对于警告状态，允许继续操作（在GUI模式下不等待用户输入）
        if (current_status == MasterStatus::STATUS_WARNING) {
            std::cout << "警告: 主站处于警告状态，但继续执行操作" << std::endl;
            return true;  // 警告状态下仍然允许操作
        }
        return false;
    }
    
    return true;
}

// 新增：等待主站进入运行状态
bool EtherCATMaster::waitForOperational(int timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - start_time).count();
        
        if (elapsed >= timeout_ms) {
            std::cerr << "错误: 等待主站就绪超时 (" << timeout_ms << "ms)" << std::endl;
            return false;
        }
        
        if (checkMasterHealth()) {
            if (current_status == MasterStatus::STATUS_OPERATIONAL) {
                std::cout << "主站已进入运行状态，耗时 " << elapsed << "ms" << std::endl;
                return true;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// 新增：获取主站状态
MasterStatus EtherCATMaster::getMasterStatus() const {
    return current_status.load();
}

// 新增：获取详细状态信息
MasterStateInfo EtherCATMaster::getMasterStateInfo() const {
    std::lock_guard<std::mutex> lock(state_mutex);
    return master_state_info;
}

// 新增：检查主站是否运行正常
bool EtherCATMaster::isOperational() const {
    auto status = current_status.load();
    return (status == MasterStatus::STATUS_OPERATIONAL || 
            status == MasterStatus::STATUS_WARNING);
}

// 新增：获取状态字符串
std::string EtherCATMaster::getMasterStatusString() const {
    switch (current_status.load()) {
        case MasterStatus::STATUS_UNINITIALIZED:
            return "未初始化";
        case MasterStatus::STATUS_INITIALIZING:
            return "初始化中";
        case MasterStatus::STATUS_OPERATIONAL:
            return "运行正常";
        case MasterStatus::STATUS_WARNING:
            return "警告状态";
        case MasterStatus::STATUS_ERROR:
            return "错误状态";
        case MasterStatus::STATUS_STOPPED:
            return "已停止";
        case MasterStatus::STATUS_FAULT:
            return "故障状态";
        default:
            return "未知状态";
    }
}

// 新增：打印健康状态
void EtherCATMaster::printHealthStatus() {
    std::cout << "=== EtherCAT 主站健康状态 ===" << std::endl;
    std::cout << "当前状态: " << getMasterStatusString() << std::endl;
    
    ec_master_state_t ms;
    ecrt_master_state(master, &ms);
    
    std::cout << "以太网链接: " << (ms.link_up ? "正常" : "断开") << std::endl;
    std::cout << "响应从站: " << ms.slaves_responding << " 个" << std::endl;
    
    // 解析应用层状态
    std::cout << "应用层状态: ";
    if (ms.al_states & 0x01) std::cout << "INIT ";
    if (ms.al_states & 0x02) std::cout << "PREOP ";
    if (ms.al_states & 0x04) std::cout << "SAFEOP ";
    if (ms.al_states & 0x08) std::cout << "OP ";
    std::cout << "(0x" << std::hex << static_cast<int>(ms.al_states) << std::dec << ")" << std::endl;
    
     // 检查时间戳
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - master_state_info.last_update).count();
        std::cout << "最后状态更新: " << elapsed << " 秒前" << std::endl;
    }
    
    std::cout << "============================" << std::endl;
}

// 新增：更新主站状态
void EtherCATMaster::updateMasterStatus() {
    if (!master) {
        current_status = MasterStatus::STATUS_UNINITIALIZED;
        return;
    }
    
    if (!running) {
        current_status = MasterStatus::STATUS_STOPPED;
        return;
    }
    
    // 获取主站状态
    ec_master_state_t ms;
    ecrt_master_state(master, &ms);
    
    // 更新状态信息
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        master_state_info.slaves_responding = ms.slaves_responding;
        master_state_info.al_states = ms.al_states;
        master_state_info.link_up = ms.link_up != 0;
        master_state_info.last_update = std::chrono::system_clock::now();
    }
    
    // 确定状态级别
    if (!ms.link_up) {
        current_status = MasterStatus::STATUS_ERROR;
    } else if (ms.slaves_responding != 3) {
        current_status = MasterStatus::STATUS_WARNING;
    } else if ((ms.al_states & 0x08) == 0) {
        current_status = MasterStatus::STATUS_WARNING;
    } else {
        current_status = MasterStatus::STATUS_OPERATIONAL;
    }
}

// ==================== 日志记录功能 ====================
void EtherCATMaster::log(LogLevel level, const std::string& module, const std::string& message, int cycle_number) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.module = module;
    entry.message = message;
    entry.cycle_number = cycle_number;
    
    // 添加到历史记录
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_history.push_front(entry);
        
        // 保持最多1000条历史记录
        if (log_history.size() > 1000) {
            log_history.pop_back();
        }
        
        // 如果是关键日志，添加到统计中
        if (level >= LogLevel::LOG_WARNING) {
            reliability_stats.addCriticalLog(entry);
        }
    }
    
    // 输出到控制台
    if (level >= LogLevel::LOG_INFO) {
        std::cout << entry.toString() << std::endl;
    }
    
    // 写入日志文件
    if (log_to_file && log_file.is_open()) {
        writeLogToFile(entry);
    }
    
    // 调用日志回调
    if (log_callback) {
        log_callback(entry);
    }
}

void EtherCATMaster::setLogCallback(LogCallback callback) {
    log_callback = callback;
}

void EtherCATMaster::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    if (log_file.is_open()) {
        log_file.close();
    }
    
    log_filename = filename;
    if (!filename.empty()) {
        log_file.open(filename, std::ios::app);
        if (log_file.is_open()) {
            log_to_file = true;
            log(LogLevel::LOG_INFO, "LogSystem", "日志文件已打开: " + filename);
        } else {
            log(LogLevel::LOG_ERROR, "LogSystem", "无法打开日志文件: " + filename);
            log_to_file = false;
        }
    } else {
        log_to_file = false;
    }
}

void EtherCATMaster::flushLog() {
    if (log_file.is_open()) {
        log_file.flush();
    }
}

std::vector<LogEntry> EtherCATMaster::getRecentLogs(int count) const {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    std::vector<LogEntry> result;
    count = std::min(count, static_cast<int>(log_history.size()));
    
    for (int i = 0; i < count; i++) {
        result.push_back(log_history[i]);
    }
    
    return result;
}

std::vector<LogEntry> EtherCATMaster::getCriticalLogs() const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    return reliability_stats.critical_logs;
}

void EtherCATMaster::writeLogToFile(const LogEntry& log) {
    if (log_file.is_open()) {
        log_file << log.toString() << std::endl;
        
        // 定期刷新文件
        static int write_count = 0;
        write_count++;
        if (write_count >= 10) {
            log_file.flush();
            write_count = 0;
            
            // 每小时检查一次日志文件大小
            auto now = std::chrono::system_clock::now();
            auto hours = std::chrono::duration_cast<std::chrono::hours>(now - last_log_file_check).count();
            if (hours >= 1) {
                checkLogFileSize();
                last_log_file_check = now;
            }
        }
    }
}

void EtherCATMaster::checkLogFileSize() {
    if (log_filename.empty()) return;
    
    try {
        std::filesystem::path p(log_filename);
        if (std::filesystem::exists(p)) {
            auto size = std::filesystem::file_size(p);
            const size_t MAX_LOG_SIZE = 100 * 1024 * 1024; // 100MB
            if (size > MAX_LOG_SIZE) {
                rotateLogFile();
            }
        }
    } catch (const std::exception& e) {
        log(LogLevel::LOG_ERROR, "LogSystem", std::string("检查日志文件大小失败: ") + e.what());
    }
}

void EtherCATMaster::rotateLogFile() {
    if (log_filename.empty()) return;
    
    try {
        // 关闭当前日志文件
        if (log_file.is_open()) {
            log_file.close();
        }
        
        // 重命名当前日志文件
        std::string backup_name = log_filename + "." + std::to_string(log_file_counter++) + ".bak";
        std::rename(log_filename.c_str(), backup_name.c_str());
        
        // 重新打开日志文件
        log_file.open(log_filename, std::ios::app);
        if (log_file.is_open()) {
            log(LogLevel::LOG_INFO, "LogSystem", "日志文件已轮转: " + backup_name);
        } else {
            log(LogLevel::LOG_ERROR, "LogSystem", "无法重新打开日志文件: " + log_filename);
            log_to_file = false;
        }
    } catch (const std::exception& e) {
        log(LogLevel::LOG_ERROR, "LogSystem", std::string("轮转日志文件失败: ") + e.what());
    }
}

// ==================== 无限连续可靠性测试 ====================
void EtherCATMaster::startInfiniteReliabilityTestAsync(float support_target,
                                                      float retract_target,
                                                      int support_timeout,
                                                      int retract_timeout,
                                                      ReliabilityProgressCallback progress_callback,
                                                      std::function<void(const ReliabilityTestStats&)> completion_callback) {
    if (infinite_test_running.load()) {
        log(LogLevel::LOG_WARNING, "ReliabilityTest", "可靠性测试已在运行");
        if (completion_callback) {
            completion_callback(reliability_stats);
        }
        return;
    }
    
    infinite_test_running = true;
    stop_infinite_test = false;
    
    // 重置统计
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        reliability_stats = ReliabilityTestStats();
        reliability_stats.start_time = std::chrono::steady_clock::now();
    }
    
    // 在后台线程中执行测试
    infinite_reliability_test_thread = std::thread([this, support_target, retract_target,
                                                   support_timeout, retract_timeout, 
                                                   progress_callback, completion_callback]() {
        
        executeInfiniteReliabilityTest(support_target, retract_target,
                                      support_timeout, retract_timeout,
                                      progress_callback, completion_callback);
    });
    
    infinite_reliability_test_thread.detach();
    
    log(LogLevel::LOG_INFO, "ReliabilityTest", "无限连续可靠性测试已启动");
}

void EtherCATMaster::executeInfiniteReliabilityTest(float support_target,
                                                   float retract_target,
                                                   int support_timeout,
                                                   int retract_timeout,
                                                   ReliabilityProgressCallback progress_callback,
                                                   std::function<void(const ReliabilityTestStats&)> completion_callback) {
    
    int cycle = 0;
    auto test_start_time = std::chrono::steady_clock::now();
    auto last_report_time = test_start_time;
    
    try {
        log(LogLevel::LOG_INFO, "ReliabilityTest", "=== 开始无限连续可靠性测试 ===");
        log(LogLevel::LOG_INFO, "ReliabilityTest", "支撑目标压力: " + std::to_string(support_target) + " bar");
        log(LogLevel::LOG_INFO, "ReliabilityTest", "收回目标压力: < " + std::to_string(retract_target) + " bar");
        log(LogLevel::LOG_INFO, "ReliabilityTest", "支撑超时: " + std::to_string(support_timeout/1000) + " 秒");
        log(LogLevel::LOG_INFO, "ReliabilityTest", "收回超时: " + std::to_string(retract_timeout/1000) + " 秒");
        log(LogLevel::LOG_INFO, "ReliabilityTest", "按 'e' 结束测试并生成报告，按 's' 查看统计，按 'h' 查看帮助");
        
        while (!stop_infinite_test && running) {
            cycle++;
            
            log(LogLevel::LOG_INFO, "ReliabilityTest", "开始第 " + std::to_string(cycle) + " 周期", cycle);
            
            // 执行支撑测试
            auto support_start = std::chrono::steady_clock::now();
            TestResult support_result = executeSupportTest(support_target, support_timeout, nullptr, cycle);
            auto support_end = std::chrono::steady_clock::now();
            int support_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                support_end - support_start).count();
            
            // 记录支撑测试结果
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                reliability_stats.addCycleResult(cycle, support_result.success, 
                                                static_cast<float>(support_time_ms),
                                                false, 0.0f); // 收回结果稍后更新
            }
            
            if (support_result.success) {
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "周期 " + std::to_string(cycle) + " 支撑测试成功 (耗时: " + 
                    std::to_string(support_time_ms) + "ms)", cycle);
            } else {
                log(LogLevel::LOG_WARNING, "ReliabilityTest", 
                    "周期 " + std::to_string(cycle) + " 支撑测试失败 (耗时: " + 
                    std::to_string(support_time_ms) + "ms)", cycle);
            }
            
            // 短暂等待
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // 执行收回测试
            auto retract_start = std::chrono::steady_clock::now();
            TestResult retract_result = executeRetractTest(retract_target, retract_timeout, nullptr, cycle);
            auto retract_end = std::chrono::steady_clock::now();
            int retract_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                retract_end - retract_start).count();
            
            // 更新收回测试结果
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                // 获取当前统计的临时副本
                auto temp_stats = reliability_stats;
                // 创建新的统计对象，更新收回结果
                temp_stats.addCycleResult(cycle, support_result.success, 
                                         static_cast<float>(support_time_ms),
                                         retract_result.success, static_cast<float>(retract_time_ms));
                // 写回
                reliability_stats = temp_stats;
            }
            
            if (retract_result.success) {
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "周期 " + std::to_string(cycle) + " 收回测试成功 (耗时: " + 
                    std::to_string(retract_time_ms) + "ms)", cycle);
            } else {
                log(LogLevel::LOG_WARNING, "ReliabilityTest", 
                    "周期 " + std::to_string(cycle) + " 收回测试失败 (耗时: " + 
                    std::to_string(retract_time_ms) + "ms)", cycle);
            }
            
            // 每10个周期或每分钟更新一次进度
            auto current_time = std::chrono::steady_clock::now();
            bool should_report = (cycle % 10 == 0) || 
                                std::chrono::duration_cast<std::chrono::seconds>(
                                    current_time - last_report_time).count() >= 60;
            
            if (should_report) {
                last_report_time = current_time;
                
                std::lock_guard<std::mutex> lock(stats_mutex);
                
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    current_time - test_start_time).count();
                
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "进度报告 - 已运行 " + std::to_string(elapsed/60) + " 分 " + 
                    std::to_string(elapsed%60) + " 秒", cycle);
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "  已完成周期: " + std::to_string(cycle), cycle);
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "  支撑成功率: " + std::to_string(reliability_stats.getSupportSuccessRate()) + "%", cycle);
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "  收回成功率: " + std::to_string(reliability_stats.getRetractSuccessRate()) + "%", cycle);
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "  总体成功率: " + std::to_string(reliability_stats.getOverallSuccessRate()) + "%", cycle);
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "  平均支撑时间: " + std::to_string(reliability_stats.avg_support_time_ms) + "ms", cycle);
                log(LogLevel::LOG_INFO, "ReliabilityTest", 
                    "  平均收回时间: " + std::to_string(reliability_stats.avg_retract_time_ms) + "ms", cycle);
                
                // 调用进度回调
                if (progress_callback) {
                    progress_callback(reliability_stats);
                }
            }
            
            // 如果不是最后一个周期，等待一下
            if (!stop_infinite_test) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        
        // 测试结束
        std::lock_guard<std::mutex> lock(stats_mutex);
        reliability_stats.end_time = std::chrono::steady_clock::now();
        
        auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            reliability_stats.getElapsedTime()).count();
        
        log(LogLevel::LOG_INFO, "ReliabilityTest", 
            "无限可靠性测试已停止，总运行时间: " + 
            std::to_string(total_seconds/3600) + " 小时 " +
            std::to_string((total_seconds%3600)/60) + " 分 " +
            std::to_string(total_seconds%60) + " 秒");
        
        log(LogLevel::LOG_INFO, "ReliabilityTest", 
            "总周期数: " + std::to_string(cycle));
        
        if (completion_callback) {
            completion_callback(reliability_stats);
        }
        
    } catch (const std::exception& e) {
        log(LogLevel::LOG_ERROR, "ReliabilityTest", 
            std::string("可靠性测试异常: ") + e.what(), cycle);
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        reliability_stats.end_time = std::chrono::steady_clock::now();
        
        if (completion_callback) {
            completion_callback(reliability_stats);
        }
    }
    
    infinite_test_running = false;
}

void EtherCATMaster::stopReliabilityTest(bool generate_report) {
    if (infinite_test_running.load()) {
        stop_infinite_test = true;
        log(LogLevel::LOG_INFO, "ReliabilityTest", "正在停止可靠性测试...");
        
        // 等待测试线程结束
        if (infinite_reliability_test_thread.joinable()) {
            infinite_reliability_test_thread.join();
        }
        
        if (generate_report) {
            // 生成测试报告
            std::lock_guard<std::mutex> lock(stats_mutex);
            printReliabilityTestReport(reliability_stats);
            
            // 自动保存报告
            saveCurrentTestReport();
        }
        
        log(LogLevel::LOG_INFO, "ReliabilityTest", "可靠性测试已停止");
    }
}

bool EtherCATMaster::isReliabilityTestRunning() const {
    return infinite_test_running.load();
}

ReliabilityTestStats EtherCATMaster::getReliabilityTestStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    return reliability_stats;
}

// ==================== 修改测试执行函数以支持日志 ====================
TestResult EtherCATMaster::executeSupportTest(float target_pressure, int timeout_ms,
                                              TestProgressCallback progress_callback,
                                              int cycle_number) {
    TestResult result;
    result.status = TestStatus::TEST_RUNNING;
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (progress_callback) {
            result.message = "测试开始";
            progress_callback(result);
        }
        
        // 检查主站状态
        if (!verifyOperation("支撑测试")) {
            result.status = TestStatus::TEST_FAILED;
            result.success = false;
            result.message = "主站状态异常";
            log(LogLevel::LOG_ERROR, "SupportTest", "主站状态异常", cycle_number);
            return result;
        }
        
        // 第一步：确保通道2（收回控制）关闭
        if (!setRelayChannel(2, false)) {
            result.status = TestStatus::TEST_FAILED;
            result.success = false;
            result.message = "无法关闭通道2";
            log(LogLevel::LOG_ERROR, "SupportTest", "无法关闭通道2", cycle_number);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 第二步：打开通道1（支撑控制）
        if (!setRelayChannel(1, true)) {
            result.status = TestStatus::TEST_FAILED;
            result.success = false;
            result.message = "无法打开通道1";
            log(LogLevel::LOG_ERROR, "SupportTest", "无法打开通道1", cycle_number);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 第三步：实时监控压力传感器
        bool target_reached = false;
        std::vector<float> pressures(4, 0.0f);
        const int CHECK_INTERVAL_MS = 100;
        
        while (!test_cancelled.load()) {
            // 检查超时
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            
            if (elapsed_ms >= timeout_ms) {
                log(LogLevel::LOG_WARNING, "SupportTest", 
                    "支撑测试超时，已运行 " + std::to_string(elapsed_ms) + "ms", cycle_number);
                break;
            }
            
            // 读取所有压力传感器
            bool all_above_target = true;
            float min_pressure = 1000.0f;
            
            std::string log_entry = "压力传感器: ";
            for (int i = 1; i <= 4; i++) {
                pressures[i-1] = readAnalogInputAsPressure(i);
                log_entry += "P" + std::to_string(i) + "=" + 
                           std::to_string(pressures[i-1]) + "bar ";
                
                if (pressures[i-1] < target_pressure) {
                    all_above_target = false;
                }
                
                if (pressures[i-1] < min_pressure) {
                    min_pressure = pressures[i-1];
                }
            }
            
            // 每5秒记录一次详细压力
            if (elapsed_ms % 5000 < CHECK_INTERVAL_MS) {
                log(LogLevel::LOG_DEBUG, "SupportTest", log_entry, cycle_number);
            }
            
            // 更新进度
            if (progress_callback) {
                result.message = "监控中... 最小压力: " + std::to_string(min_pressure) + " bar";
                progress_callback(result);
            }
            
            // 检查是否所有传感器都达到目标压力
            if (all_above_target) {
                target_reached = true;
                log(LogLevel::LOG_INFO, "SupportTest", 
                    "支撑测试达到目标压力 " + std::to_string(target_pressure) + " bar", cycle_number);
                break;
            }
            
            // 等待下一次检查
            std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
        }
        
        // 第四步：关闭通道1（支撑控制）
        if (!setRelayChannel(1, false)) {
            log(LogLevel::LOG_WARNING, "SupportTest", "无法关闭通道1", cycle_number);
        }
        
        // 设置最终结果
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        result.final_pressures = pressures;
        
        if (target_reached && !test_cancelled.load()) {
            result.status = TestStatus::TEST_COMPLETED;
            result.success = true;
            result.message = "支撑测试成功完成";
            log(LogLevel::LOG_INFO, "SupportTest", 
                "支撑测试成功，耗时 " + std::to_string(result.elapsed_time_ms) + "ms", cycle_number);
        } else if (!test_cancelled.load()) {
            result.status = TestStatus::TEST_COMPLETED;
            result.success = false;
            result.message = "支撑测试未达到目标压力";
            log(LogLevel::LOG_WARNING, "SupportTest", 
                "支撑测试失败，耗时 " + std::to_string(result.elapsed_time_ms) + "ms", cycle_number);
        }
        
    } catch (const std::exception& e) {
        result.status = TestStatus::TEST_FAILED;
        result.success = false;
        result.message = std::string("测试异常: ") + e.what();
        log(LogLevel::LOG_ERROR, "SupportTest", std::string("测试异常: ") + e.what(), cycle_number);
    }
    
    return result;
}

TestResult EtherCATMaster::executeRetractTest(float target_pressure, int timeout_ms,
                                              TestProgressCallback progress_callback,
                                              int cycle_number) {
    TestResult result;
    result.status = TestStatus::TEST_RUNNING;
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (progress_callback) {
            result.message = "测试开始";
            progress_callback(result);
        }
        
        // 检查主站状态
        if (!verifyOperation("收回测试")) {
            result.status = TestStatus::TEST_FAILED;
            result.success = false;
            result.message = "主站状态异常";
            log(LogLevel::LOG_ERROR, "RetractTest", "主站状态异常", cycle_number);
            return result;
        }
        
        // 第一步：确保通道1（支撑控制）关闭
        if (!setRelayChannel(1, false)) {
            result.status = TestStatus::TEST_FAILED;
            result.success = false;
            result.message = "无法关闭通道1";
            log(LogLevel::LOG_ERROR, "RetractTest", "无法关闭通道1", cycle_number);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 第二步：打开通道2（收回控制）
        if (!setRelayChannel(2, true)) {
            result.status = TestStatus::TEST_FAILED;
            result.success = false;
            result.message = "无法打开通道2";
            log(LogLevel::LOG_ERROR, "RetractTest", "无法打开通道2", cycle_number);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 第三步：实时监控压力传感器
        bool target_reached = false;
        std::vector<float> pressures(4, 0.0f);
        const int CHECK_INTERVAL_MS = 100;
        
        while (!test_cancelled.load()) {
            // 检查超时
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            
            if (elapsed_ms >= timeout_ms) {
                log(LogLevel::LOG_WARNING, "RetractTest", 
                    "收回测试超时，已运行 " + std::to_string(elapsed_ms) + "ms", cycle_number);
                break;
            }
            
            // 读取所有压力传感器
            bool all_below_target = true;
            float max_pressure = 0.0f;
            
            std::string log_entry = "压力传感器: ";
            for (int i = 1; i <= 4; i++) {
                pressures[i-1] = readAnalogInputAsPressure(i);
                log_entry += "P" + std::to_string(i) + "=" + 
                           std::to_string(pressures[i-1]) + "bar ";
                
                if (pressures[i-1] >= target_pressure) {
                    all_below_target = false;
                }
                
                if (pressures[i-1] > max_pressure) {
                    max_pressure = pressures[i-1];
                }
            }
            
            // 每5秒记录一次详细压力
            if (elapsed_ms % 5000 < CHECK_INTERVAL_MS) {
                log(LogLevel::LOG_DEBUG, "RetractTest", log_entry, cycle_number);
            }
            
            // 更新进度
            if (progress_callback) {
                result.message = "监控中... 最大压力: " + std::to_string(max_pressure) + " bar";
                progress_callback(result);
            }
            
            // 检查是否所有传感器都低于目标压力
            if (all_below_target) {
                target_reached = true;
                log(LogLevel::LOG_INFO, "RetractTest", 
                    "收回测试达到目标压力 < " + std::to_string(target_pressure) + " bar", cycle_number);
                break;
            }
            
            // 等待下一次检查
            std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
        }
        
        // 第四步：关闭通道2（收回控制）
        if (!setRelayChannel(2, false)) {
            log(LogLevel::LOG_WARNING, "RetractTest", "无法关闭通道2", cycle_number);
        }
        
        // 设置最终结果
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        result.final_pressures = pressures;
        
        if (target_reached && !test_cancelled.load()) {
            result.status = TestStatus::TEST_COMPLETED;
            result.success = true;
            result.message = "收回测试成功完成";
            log(LogLevel::LOG_INFO, "RetractTest", 
                "收回测试成功，耗时 " + std::to_string(result.elapsed_time_ms) + "ms", cycle_number);
        } else if (!test_cancelled.load()) {
            result.status = TestStatus::TEST_COMPLETED;
            result.success = false;
            result.message = "收回测试未达到目标压力";
            log(LogLevel::LOG_WARNING, "RetractTest", 
                "收回测试失败，耗时 " + std::to_string(result.elapsed_time_ms) + "ms", cycle_number);
        }
        
    } catch (const std::exception& e) {
        result.status = TestStatus::TEST_FAILED;
        result.success = false;
        result.message = std::string("测试异常: ") + e.what();
        log(LogLevel::LOG_ERROR, "RetractTest", std::string("测试异常: ") + e.what(), cycle_number);
    }
    
    return result;
}

bool EtherCATMaster::configureSlaves() {
    std::cout << "配置从站和PDO映射..." << std::endl;
    
    // 配置 EK1100 (Slave 0) - 耦合器 (无PDO)
    std::cout << "配置 EK1100 耦合器 (位置 0)..." << std::endl;
    auto config0 = ecrt_master_slave_config(master, 0, 0, EK1100_VENDOR_ID, EK1100_PRODUCT_CODE);
    if (!config0) {
        std::cerr << "错误: 无法配置 EK1100 耦合器 (位置 0)" << std::endl;
        return false;
    }
    slave_configs.push_back(config0);
    std::cout << "EK1100 配置成功" << std::endl;
    
    // 配置 EL1008 (Slave 1) - 数字输入
    std::cout << "配置 EL1008 从站 (位置 1)..." << std::endl;
    auto config1 = ecrt_master_slave_config(master, 0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE);
    if (!config1) {
        std::cerr << "错误: 无法配置 EL1008 从站 (位置 1)" << std::endl;
        return false;
    }
    slave_configs.push_back(config1);
    
    // 配置 EL1008 PDO 条目
    ec_pdo_entry_info_t slave_1_pdo_entries[] = {
        {0x6000, 0x01, 1}, /* Input */
        {0x6010, 0x01, 1}, /* Input */
        {0x6020, 0x01, 1}, /* Input */
        {0x6030, 0x01, 1}, /* Input */
        {0x6040, 0x01, 1}, /* Input */
        {0x6050, 0x01, 1}, /* Input */
        {0x6060, 0x01, 1}, /* Input */
        {0x6070, 0x01, 1}, /* Input */
    };

    ec_pdo_info_t slave_1_pdos[] = {
        {0x1a00, 1, slave_1_pdo_entries + 0}, /* Channel 1 */
        {0x1a01, 1, slave_1_pdo_entries + 1}, /* Channel 2 */
        {0x1a02, 1, slave_1_pdo_entries + 2}, /* Channel 3 */
        {0x1a03, 1, slave_1_pdo_entries + 3}, /* Channel 4 */
        {0x1a04, 1, slave_1_pdo_entries + 4}, /* Channel 5 */
        {0x1a05, 1, slave_1_pdo_entries + 5}, /* Channel 6 */
        {0x1a06, 1, slave_1_pdo_entries + 6}, /* Channel 7 */
        {0x1a07, 1, slave_1_pdo_entries + 7}, /* Channel 8 */
    };

    ec_sync_info_t slave_1_syncs[] = {
        {0, EC_DIR_INPUT, 8, slave_1_pdos, EC_WD_DISABLE},
        {0xff, EC_DIR_INVALID, 0, NULL, EC_WD_DISABLE}
    };
    
    if (ecrt_slave_config_pdos(config1, EC_END, slave_1_syncs)) {
        std::cerr << "错误: 无法配置 EL1008 PDO 映射" << std::endl;
        return false;
    }
    std::cout << "EL1008 配置成功" << std::endl;

    // 配置 EL3074 (Slave 2) - 模拟输入
    std::cout << "配置 EL3074 从站 (位置 2)..." << std::endl;
    auto config2 = ecrt_master_slave_config(master, 0, 2, EL3074_VENDOR_ID, EL3074_PRODUCT_CODE);
    if (!config2) {
        std::cerr << "错误: 无法配置 EL3074 从站 (位置 2)" << std::endl;
        return false;
    }
    slave_configs.push_back(config2);
    
    // 配置 EL3074 PDO 条目
    ec_pdo_entry_info_t slave_2_pdo_entries[] = {
        {0x6000, 0x01, 1}, /* Underrange */
        {0x6000, 0x02, 1}, /* Overrange */
        {0x6000, 0x03, 2}, /* Limit 1 */
        {0x6000, 0x05, 2}, /* Limit 2 */
        {0x6000, 0x07, 1}, /* Error */
        {0x0000, 0x00, 7}, /* Gap */
        {0x6000, 0x0f, 1}, /* TxPDO State */
        {0x6000, 0x10, 1}, /* TxPDO Toggle */
        {0x6000, 0x11, 16}, /* Value */
        {0x6010, 0x01, 1}, /* Underrange */
        {0x6010, 0x02, 1}, /* Overrange */
        {0x6010, 0x03, 2}, /* Limit 1 */
        {0x6010, 0x05, 2}, /* Limit 2 */
        {0x6010, 0x07, 1}, /* Error */
        {0x0000, 0x00, 7}, /* Gap */
        {0x6010, 0x0f, 1}, /* TxPDO State */
        {0x6010, 0x10, 1}, /* TxPDO Toggle */
        {0x6010, 0x11, 16}, /* Value */
        {0x6020, 0x01, 1}, /* Underrange */
        {0x6020, 0x02, 1}, /* Overrange */
        {0x6020, 0x03, 2}, /* Limit 1 */
        {0x6020, 0x05, 2}, /* Limit 2 */
        {0x6020, 0x07, 1}, /* Error */
        {0x0000, 0x00, 7}, /* Gap */
        {0x6020, 0x0f, 1}, /* TxPDO State */
        {0x6020, 0x10, 1}, /* TxPDO Toggle */
        {0x6020, 0x11, 16}, /* Value */
        {0x6030, 0x01, 1}, /* Underrange */
        {0x6030, 0x02, 1}, /* Overrange */
        {0x6030, 0x03, 2}, /* Limit 1 */
        {0x6030, 0x05, 2}, /* Limit 2 */
        {0x6030, 0x07, 1}, /* Error */
        {0x0000, 0x00, 7}, /* Gap */
        {0x6030, 0x0f, 1}, /* TxPDO State */
        {0x6030, 0x10, 1}, /* TxPDO Toggle */
        {0x6030, 0x11, 16}, /* Value */
    };

    ec_pdo_info_t slave_2_pdos[] = {
        {0x1a00, 9, slave_2_pdo_entries + 0}, /* AI TxPDO-Map Standard Ch.1 */
        {0x1a02, 9, slave_2_pdo_entries + 9}, /* AI TxPDO-Map Standard Ch.2 */
        {0x1a04, 9, slave_2_pdo_entries + 18}, /* AI TxPDO-Map Standard Ch.3 */
        {0x1a06, 9, slave_2_pdo_entries + 27}, /* AI TxPDO-Map Standard Ch.4 */
    };

    ec_sync_info_t slave_2_syncs[] = {
        {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
        {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
        {2, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
        {3, EC_DIR_INPUT, 4, slave_2_pdos, EC_WD_DISABLE},
        {0xff, EC_DIR_INVALID, 0, NULL, EC_WD_DISABLE}
    };
    
    if (ecrt_slave_config_pdos(config2, EC_END, slave_2_syncs)) {
        std::cerr << "错误: 无法配置 EL3074 PDO 映射" << std::endl;
        return false;
    }
    std::cout << "EL3074 配置成功" << std::endl;

    // 配置 EL2634 (Slave 3) - 继电器输出
    std::cout << "配置 EL2634 从站 (位置 3)..." << std::endl;
    auto config3 = ecrt_master_slave_config(master, 0, 3, EL2634_VENDOR_ID, EL2634_PRODUCT_CODE);
    if (!config3) {
        std::cerr << "错误: 无法配置 EL2634 从站 (位置 3)" << std::endl;
        return false;
    }
    slave_configs.push_back(config3);
    
    // 配置 EL2634 PDO 条目
    ec_pdo_entry_info_t slave_3_pdo_entries[] = {
        {0x7000, 0x01, 1}, /* Output */
        {0x7010, 0x01, 1}, /* Output */
        {0x7020, 0x01, 1}, /* Output */
        {0x7030, 0x01, 1}, /* Output */
    };

    ec_pdo_info_t slave_3_pdos[] = {
        {0x1600, 1, slave_3_pdo_entries + 0}, /* Channel 1 */
        {0x1601, 1, slave_3_pdo_entries + 1}, /* Channel 2 */
        {0x1602, 1, slave_3_pdo_entries + 2}, /* Channel 3 */
        {0x1603, 1, slave_3_pdo_entries + 3}, /* Channel 4 */
    };

    ec_sync_info_t slave_3_syncs[] = {
        {0, EC_DIR_OUTPUT, 4, slave_3_pdos, EC_WD_ENABLE},
        {0xff, EC_DIR_INVALID, 0, NULL, EC_WD_DISABLE}
    };
    
    if (ecrt_slave_config_pdos(config3, EC_END, slave_3_syncs)) {
        std::cerr << "错误: 无法配置 EL2634 PDO 映射" << std::endl;
        return false;
    }
    std::cout << "EL2634 配置成功" << std::endl;
    
    // 配置 EL6001 (Slave 4) - RS232接口 (无PDO)
    std::cout << "配置 EL6001 RS232接口 (位置 4)..." << std::endl;
    auto config4 = ecrt_master_slave_config(master, 0, 4, EL6001_VENDOR_ID, EL6001_PRODUCT_CODE);
    if (!config4) {
        std::cerr << "警告: 无法配置 EL6001 从站 (位置 4)，继续..." << std::endl;
        // 不返回 false，因为这不是关键从站
    } else {
        slave_configs.push_back(config4);
        std::cout << "EL6001 配置成功" << std::endl;
    }
    
    // 配置 EL6751 (Slave 5) - CANopen主站 (无PDO)
    std::cout << "配置 EL6751 CANopen主站 (位置 5)..." << std::endl;
    auto config5 = ecrt_master_slave_config(master, 0, 5, EL6751_VENDOR_ID, EL6751_PRODUCT_CODE);
    if (!config5) {
        std::cerr << "警告: 无法配置 EL6751 从站 (位置 5)，继续..." << std::endl;
        // 不返回 false，因为这不是关键从站
    } else {
        slave_configs.push_back(config5);
        std::cout << "EL6751 配置成功" << std::endl;
    }

    // 注册PDO条目到域
    std::cout << "注册PDO条目到域..." << std::endl;

    ec_pdo_entry_reg_t domain_regs[] = {
        // EL1008 - 数字输入 (注册所有8个通道)
        {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6000, 1, &off_dig_in[0], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6010, 1, &off_dig_in[1], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6020, 1, &off_dig_in[2], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6030, 1, &off_dig_in[3], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6040, 1, &off_dig_in[4], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6050, 1, &off_dig_in[5], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6060, 1, &off_dig_in[6], NULL},
        // {0, 1, EL1008_VENDOR_ID, EL1008_PRODUCT_CODE, 0x6070, 1, &off_dig_in[7], NULL},
        
        // EL3074 - 模拟输入 (只注册模拟值，子索引0x11)
        {0, 2, EL3074_VENDOR_ID, EL3074_PRODUCT_CODE, 0x6000, 0x11, &off_ai_val[0], NULL},
        {0, 2, EL3074_VENDOR_ID, EL3074_PRODUCT_CODE, 0x6010, 0x11, &off_ai_val[1], NULL},
        {0, 2, EL3074_VENDOR_ID, EL3074_PRODUCT_CODE, 0x6020, 0x11, &off_ai_val[2], NULL},
        {0, 2, EL3074_VENDOR_ID, EL3074_PRODUCT_CODE, 0x6030, 0x11, &off_ai_val[3], NULL},
        
        // EL2634 - 继电器输出 (注册所有4个通道)
        {0, 3, EL2634_VENDOR_ID, EL2634_PRODUCT_CODE, 0x7000, 1, &off_relay_out[0], NULL},
        // {0, 3, EL2634_VENDOR_ID, EL2634_PRODUCT_CODE, 0x7010, 1, &off_relay_out[1], NULL},
        // {0, 3, EL2634_VENDOR_ID, EL2634_PRODUCT_CODE, 0x7020, 1, &off_relay_out[2], NULL},
        // {0, 3, EL2634_VENDOR_ID, EL2634_PRODUCT_CODE, 0x7030, 1, &off_relay_out[3], NULL},
        
        {}  // 结束标记
    };
    
    if (ecrt_domain_reg_pdo_entry_list(domain, domain_regs)) {
        std::cerr << "错误: 无法注册 PDO 条目到域" << std::endl;
        return false;
    }

    std::cout << "从站配置完成: " << slave_configs.size() << " 个从站已配置" << std::endl;
    return true;
}
// ==================== 修改其他关键函数以支持日志 ====================
bool EtherCATMaster::start() {
    if (!initialized) {
        log(LogLevel::LOG_ERROR, "Master", "主站未初始化");
        return false;
    }

    log(LogLevel::LOG_INFO, "Master", "激活 EtherCAT 主站...");
    
    // 激活主站
    if (ecrt_master_activate(master)) {
        log(LogLevel::LOG_ERROR, "Master", "无法激活主站");
        current_status = MasterStatus::STATUS_ERROR;
        return false;
    }

    // 获取域数据
    domain_data = ecrt_domain_data(domain);
    if (!domain_data) {
        log(LogLevel::LOG_ERROR, "Master", "无法获取域数据");
        current_status = MasterStatus::STATUS_ERROR;
        return false;
    }

    running = true;
    current_status = MasterStatus::STATUS_INITIALIZING;
    
    // 启动处理线程
    process_thread = std::thread(&EtherCATMaster::processThreadFunc, this);
    
    // 启动任务处理线程
    // task_thread = std::thread(&EtherCATMaster::taskThreadFunc, this);
    
    // 等待主站进入运行状态（减少等待时间，避免阻塞UI）
    if (!waitForOperational(1000)) {
        log(LogLevel::LOG_WARNING, "Master", "主站未能立即进入运行状态，继续启动...");
    }
    
    log(LogLevel::LOG_INFO, "Master", "EtherCAT 主站已启动并运行");
    
    // 自动设置日志文件
    if (!log_to_file) {
        std::string default_log = "ethercat_test_" + generateTimestamp() + ".log";
        setLogFile(default_log);
    }
    
    return true;
}

void EtherCATMaster::stop() {
    if (master && running) {
        log(LogLevel::LOG_INFO, "Master", "正在停止 EtherCAT 主站...");
        
        running = false;
        current_status = MasterStatus::STATUS_STOPPED;
        
        // 取消当前测试
        cancelCurrentTest();
        stopReliabilityTest(false);  // 不生成报告
        
        // 确保所有继电器关闭
        setAllRelays(false);
        
        // 通知任务线程退出
        task_cv.notify_all();
        
        // 短暂等待以确保命令执行
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 等待处理线程结束
        if (process_thread.joinable()) {
            process_thread.join();
        }
        
        // 等待任务线程结束
        if (task_thread.joinable()) {
            task_thread.join();
        }
        
        // 等待测试线程结束
        if (test_thread.joinable()) {
            test_thread.join();
        }
        
        // 等待无限测试线程结束
        if (infinite_reliability_test_thread.joinable()) {
            infinite_reliability_test_thread.join();
        }
        
        // 等待快捷键监听线程结束
        if (hotkey_listening) {
            hotkey_listening = false;
            if (hotkey_thread.joinable()) {
                hotkey_thread.join();
            }
        }
        
        ecrt_release_master(master);
        master = nullptr;
        domain = nullptr;
        domain_data = nullptr;
        slave_configs.clear();
        initialized = false;
        
        // 关闭日志文件
        if (log_file.is_open()) {
            log_file.close();
        }
        
        log(LogLevel::LOG_INFO, "Master", "EtherCAT 主站已停止");
    }
}

// ==================== 新增功能函数 ====================
void EtherCATMaster::saveCurrentTestReport(const std::string& filename) {
    std::lock_guard<std::mutex> lock(stats_mutex);
    
    std::string report_filename = filename;
    if (report_filename.empty()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        char time_str[100];
        std::strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", std::localtime(&time_t));
        report_filename = std::string("reliability_report_") + time_str + ".txt";
    }
    
    if (saveTestResultsToFile(report_filename, reliability_stats)) {
        log(LogLevel::LOG_INFO, "Report", "测试报告已保存到: " + report_filename);
    } else {
        log(LogLevel::LOG_ERROR, "Report", "保存测试报告失败");
    }
}

void EtherCATMaster::setHotkeyCallback(std::function<void(int)> callback) {
    hotkey_callback = callback;
}

std::string EtherCATMaster::generateTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char time_str[100];
    std::strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", std::localtime(&time_t));
    return std::string(time_str);
}

std::string EtherCATMaster::generateLogFilename() const {
    return "ethercat_log_" + generateTimestamp() + ".log";
}

// ==================== 保持原有函数（省略了部分实现细节）====================
// 注意：为了节省篇幅，这里省略了部分函数的实现
// 您需要将原有的函数实现保留在这里，并确保它们使用新的log函数进行日志记录

bool EtherCATMaster::saveTestResultsToFile(const std::string& filename, const ReliabilityTestStats& stats)  {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            log(LogLevel::LOG_ERROR, "Report", "无法打开文件进行写入: " + filename);
            return false;
        }
        
        file << "=== 液压脚撑可靠性测试报告 ===" << std::endl;
        file << "生成时间: " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << std::endl;
        file << "总测试周期数: " << stats.total_cycles << std::endl;
        file << "支撑成功次数: " << stats.support_success_count << std::endl;
        file << "支撑失败次数: " << stats.support_fail_count << std::endl;
        file << "收回成功次数: " << stats.retract_success_count << std::endl;
        file << "收回失败次数: " << stats.retract_fail_count << std::endl;
        file << "支撑成功率: " << std::fixed << std::setprecision(2) << stats.getSupportSuccessRate() << "%" << std::endl;
        file << "收回成功率: " << std::fixed << std::setprecision(2) << stats.getRetractSuccessRate() << "%" << std::endl;
        file << "总成功率: " << std::fixed << std::setprecision(2) << stats.getOverallSuccessRate() << "%" << std::endl;
        file << "平均支撑时间: " << std::fixed << std::setprecision(1) << stats.avg_support_time_ms << "ms" << std::endl;
        file << "平均收回时间: " << std::fixed << std::setprecision(1) << stats.avg_retract_time_ms << "ms" << std::endl;
        file << "最大连续支撑失败: " << stats.max_support_failures << std::endl;
        file << "最大连续收回失败: " << stats.max_retract_failures << std::endl;
        
        auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(stats.getElapsedTime()).count();
        file << "总耗时: " << elapsed_seconds/3600 << " 小时 " 
             << (elapsed_seconds%3600)/60 << " 分 " 
             << elapsed_seconds%60 << " 秒" << std::endl;
        
        // 写入关键日志
        file << "\n=== 关键日志记录 ===" << std::endl;
        for (const auto& log : stats.critical_logs) {
            file << log.toString() << std::endl;
        }
        
        // 写入最近周期结果
        file << "\n=== 最近100个周期结果 ===" << std::endl;
        for (const auto& result : stats.recent_cycles) {
            file << "周期 " << result.first << ": " << (result.second ? "成功" : "失败") << std::endl;
        }
        
        file.close();
        return true;
        
    } catch (const std::exception& e) {
        log(LogLevel::LOG_ERROR, "Report", std::string("保存测试结果到文件失败: ") + e.what());
        return false;
    }
}

void EtherCATMaster::printReliabilityTestReport(const ReliabilityTestStats& stats) const {
    auto elapsed = stats.getElapsedTime();
    auto hours = std::chrono::duration_cast<std::chrono::hours>(elapsed).count();
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() % 60;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() % 60;
    
    std::cout << "\n=== 可靠性测试报告 ===" << std::endl;
    std::cout << "运行时间: " << hours << " 小时 " << minutes << " 分 " << seconds << " 秒" << std::endl;
    std::cout << "总测试周期数: " << stats.total_cycles << std::endl;
    std::cout << "支撑成功次数: " << stats.support_success_count << std::endl;
    std::cout << "支撑失败次数: " << stats.support_fail_count << std::endl;
    std::cout << "收回成功次数: " << stats.retract_success_count << std::endl;
    std::cout << "收回失败次数: " << stats.retract_fail_count << std::endl;
    std::cout << "支撑成功率: " << std::fixed << std::setprecision(2) << stats.getSupportSuccessRate() << "%" << std::endl;
    std::cout << "收回成功率: " << std::fixed << std::setprecision(2) << stats.getRetractSuccessRate() << "%" << std::endl;
    std::cout << "总成功率: " << std::fixed << std::setprecision(2) << stats.getOverallSuccessRate() << "%" << std::endl;
    std::cout << "最近100周期成功率: " << std::fixed << std::setprecision(2) << stats.getRecentSupportSuccessRate() << "%" << std::endl;
    std::cout << "平均支撑时间: " << std::fixed << std::setprecision(1) << stats.avg_support_time_ms << "ms" << std::endl;
    std::cout << "平均收回时间: " << std::fixed << std::setprecision(1) << stats.avg_retract_time_ms << "ms" << std::endl;
    std::cout << "最大连续支撑失败: " << stats.max_support_failures << std::endl;
    std::cout << "最大连续收回失败: " << stats.max_retract_failures << std::endl;
    
    // 显示关键日志数量
    std::cout << "关键日志数量: " << stats.critical_logs.size() << std::endl;
    
    std::cout << "=============================" << std::endl;
}

bool EtherCATMaster::setRelayChannel(uint8_t channel, bool state) {

    if (!verifyOperation("设置继电器通道")) {
        return false;
    }

    if (channel < 1 || channel > 4) {
        std::cerr << "错误: 通道号必须在 1-4 范围内" << std::endl;
        return false;
    }

    if (!running) {
        std::cerr << "错误: 主站未运行" << std::endl;
        return false;
    }

    std::cout << "设置继电器通道 " << static_cast<int>(channel) << " 为: " 
              << (state ? "开启" : "关闭") << std::endl;

    // 更新继电器状态缓存
    uint8_t current_states = relay_states.load();
    if (state) {
        current_states |= (1 << (channel - 1));
    } else {
        current_states &= ~(1 << (channel - 1));
    }
    relay_states.store(current_states);
    
    return true;
}

bool EtherCATMaster::setAllRelays(bool state) {

    if (!verifyOperation("设置所有继电器")) {
        return false;
    }

    if (!running) {
        std::cerr << "错误: 主站未运行" << std::endl;
        return false;
    }

    // 更新所有继电器状态
    uint8_t new_states = state ? 0x0F : 0x00; // 4个通道全部开启或关闭
    relay_states.store(new_states);
    
    std::cout << "设置所有继电器为: " << (state ? "开启" : "关闭") << std::endl;
    return true;
}

void EtherCATMaster::printDomainData() {
    if (!domain_data) {
        std::cout << "域数据不可用" << std::endl;
        return;
    }
    
    std::cout << "=== 域数据 ===" << std::endl;
    
    // 打印EL1008数字输入状态
    std::cout << "EL1008 数字输入: ";
    for (int i = 0; i < 8; i++) {
        uint8_t* input_data = domain_data + off_dig_in[i];
        uint8_t di_data = EC_READ_U8(input_data);
        std::cout << "Ch" << (i+1) << "=" << static_cast<int>(di_data) << " ";
    }
    std::cout << std::endl;
    
    // 打印EL3074压力传感器状态
    std::cout << "EL3074 压力传感器: " << std::endl;
    for (int i = 0; i < 4; i++) {
        int16_t raw_value = readAnalogInputPDO(i + 1);
        float current_value = convertAnalogToCurrent(raw_value);
        float pressure_value = convertCurrentToPressure(current_value);
        PressureStatus status = checkPressureStatus(i + 1);
        
        std::cout << "  Ch" << (i+1) << ": "
                  << "原始值=" << raw_value << ", "
                  << "电流=" << std::fixed << std::setprecision(3) << current_value << "mA, "
                  << "压力=" << std::fixed << std::setprecision(2) << pressure_value << "bar, "
                  << "状态=" << getPressureStatusString(status);
        
        // 添加警告标记
        if (status == PRESSURE_OVERLOAD) {
            std::cout << " [危险!]";
        } else if (status == PRESSURE_OVER_RANGE) {
            std::cout << " [警告]";
        } else if (status == PRESSURE_ZERO_DRIFT) {
            std::cout << " [注意]";
        }
        std::cout << std::endl;
    }
    
    // 打印EL2634继电器输出状态
    std::cout << "EL2634 继电器输出: ";
    uint8_t current_states = relay_states.load();
    for (int i = 0; i < 4; i++) {
        bool state = (current_states & (1 << i)) != 0;
        std::cout << "Ch" << (i+1) << "=" << (state ? "1" : "0") << " ";
    }
    std::cout << std::endl;
    
    std::cout << "==============" << std::endl;
}

bool EtherCATMaster::toggleRelayChannel(uint8_t channel) {
    if (!verifyOperation("切换继电器通道")) {
        return false;
    }
    if (!running) {
        std::cerr << "错误: 主站未运行" << std::endl;
        return false;
    }

    if (channel < 1 || channel > 4) {
        std::cerr << "错误: 通道号必须在 1-4 范围内" << std::endl;
        return false;
    }

    // 切换继电器状态
    uint8_t current_states = relay_states.load();
    current_states ^= (1 << (channel - 1)); // 使用异或切换位
    relay_states.store(current_states);
    
    bool new_state = (current_states & (1 << (channel - 1))) != 0;
    std::cout << "切换通道 " << static_cast<int>(channel) 
              << " 到 " << (new_state ? "开启" : "关闭") << std::endl;
    
    return true;
}

float EtherCATMaster::readAnalogInputAsPressure(uint8_t channel) {
    if (channel < 1 || channel > 4) {
        return -1.0f;
    }

    if (!running) {
        return -1.0f;
    }

    int16_t raw_value = readAnalogInputPDO(channel);
    float current_value = convertAnalogToCurrent(raw_value);
    float pressure_value = convertCurrentToPressure(current_value);
    
    // 注意：不输出日志到控制台，避免阻塞 UI
    // 如果需要详细日志，可以使用 log() 函数
    
    return pressure_value;
}

void EtherCATMaster::printSlaveStates() {
    std::cout << "=== 从站状态 ===" << std::endl;
    std::cout << "1. EL1008 - 8通道数字输入 (位置 1)" << std::endl;
    std::cout << "2. EL3074 - 4通道模拟输入 (位置 2)" << std::endl;
    std::cout << "3. EL2634 - 4通道继电器输出 (位置 3)" << std::endl;
    std::cout << "================" << std::endl;
}
std::vector<bool> EtherCATMaster::readAllDigitalInputs() {
    std::vector<bool> states;
    
    if (!running || !domain_data) {
        return states;
    }
    
    // 直接读取PDO数据，不做额外验证（避免阻塞UI）
    for (uint8_t i = 1; i <= 8; i++) {
        states.push_back(readDigitalInputPDO(i));
    }
    return states;
}

bool EtherCATMaster::readDigitalInputPDO(uint8_t channel) {
    if (!domain_data) return false;
    if (channel < 1 || channel > 8) return false;
    
    // EL1008 输入数据在域数据中的偏移量
    uint8_t* input_data = domain_data + off_dig_in[channel - 1];
    
    // 使用 EC_READ_U8 宏读取输入状态
    return (EC_READ_U8(input_data) & 0x01) != 0;
}

bool EtherCATMaster::readDigitalInput(uint8_t channel) {
    if (channel < 1 || channel > 8) {
        return false;
    }

    if (!running || !domain_data) {
        return false;
    }

    // 直接读取PDO数据
    return readDigitalInputPDO(channel);
}
std::vector<float> EtherCATMaster::readAllAnalogInputs() {
    std::vector<float> values;
    for (uint8_t i = 1; i <= 4; i++) {
        values.push_back(readAnalogInput(i));
    }
    return values;
}

void EtherCATMaster::printMasterState() {
    // if (!master) return;
    
    // ecrt_master_state(master, &master_state);
    
    // std::cout << "=== EtherCAT 主站状态 ===" << std::endl;
    // std::cout << "从站数量: " << master_state.slaves_responding << std::endl;
    // std::cout << "报警: " << (master_state.al_states ? "是" : "否") << std::endl;
    // std::cout << "链接状态: " << (master_state.link_up ? "正常" : "断开") << std::endl;
    // std::cout << "=========================" << std::endl;


    if (!master) return;
    
    ecrt_master_state(master, &master_state);
    
    std::cout << "=== EtherCAT 主站状态 ===" << std::endl;
    std::cout << "响应从站数量: " << master_state.slaves_responding << std::endl;
    
    // 解析应用层状态
    std::cout << "应用层状态: ";
    std::vector<std::string> al_states;
    
    if (master_state.al_states & 0x01) { // Bit 0: INIT
        al_states.push_back("INIT");
    }
    if (master_state.al_states & 0x02) { // Bit 1: PREOP
        al_states.push_back("PREOP");
    }
    if (master_state.al_states & 0x04) { // Bit 2: SAFEOP
        al_states.push_back("SAFEOP");
    }
    if (master_state.al_states & 0x08) { // Bit 3: OP
        al_states.push_back("OP");
    }
    
    if (al_states.empty()) {
        std::cout << "无状态";
    } else {
        for (size_t i = 0; i < al_states.size(); ++i) {
            if (i > 0) std::cout << " | ";
            std::cout << al_states[i];
        }
    }
    std::cout << " (0x" << std::hex << static_cast<unsigned int>(master_state.al_states) << std::dec << ")" << std::endl;
    
    std::cout << "以太网链接: " << (master_state.link_up ? "正常" : "断开") << std::endl;
    std::cout << "=========================" << std::endl;
}


float EtherCATMaster::readAnalogInput(uint8_t channel) {
    if (channel < 1 || channel > 4) {
        std::cerr << "错误: 通道号必须在 1-4 范围内" << std::endl;
        return -1;
    }

    if (!running) {
        std::cerr << "错误: 主站未运行" << std::endl;
        return -1;
    }

    // 使用PDO读取模拟输入
    int16_t raw_value = readAnalogInputPDO(channel);

    // 转换为电流值
    float current_value = convertAnalogToCurrent(raw_value);
    
    std::cout << "模拟输入通道 " << static_cast<int>(channel) << ": " 
              << "原始值=" << raw_value << ", "
              << "电流值=" << std::fixed << std::setprecision(3) << current_value << "mA" 
              << std::endl;

    return current_value;
}

int16_t EtherCATMaster::readAnalogInputPDO(uint8_t channel) {
    if (!domain_data) return -1;
    if (channel < 1 || channel > 4) return -1;
    
    // EL3074 模拟输入数据在域数据中的偏移量
    uint8_t* analog_data = domain_data + off_ai_val[channel - 1];
    
    // 读取16位值
    return EC_READ_S16(analog_data);
}

// 添加模拟量转换函数
float EtherCATMaster::convertAnalogToCurrent(int16_t analog_value) {
    // // 实际电流值 = 模拟量值 * (20 - 4) / 32767 + 4
    // // 转换为4-20mA电流值
    // return static_cast<float>(analog_value) * (20.0f - 4.0f) / 32767.0f + 4.0f;

    // 模拟量值转电流值: 4-20mA
    // 公式: 电流值 = 模拟量值 × (20 - 4) / 32767 + 4
    return static_cast<float>(analog_value) * (CURRENT_RANGE_MAX - CURRENT_RANGE_MIN) / 
           ADC_MAX_VALUE + CURRENT_RANGE_MIN;
}

float EtherCATMaster::convertCurrentToPressure(float current_value) {
    // 电流值转压力值: 4-20mA 对应 0-100bar
    // 线性转换公式: 压力值 = (电流值 - 4) × (100 - 0) / (20 - 4)
    float pressure = (current_value - CURRENT_RANGE_MIN) * 
                     (PRESSURE_RANGE_MAX - PRESSURE_RANGE_MIN) / 
                     (CURRENT_RANGE_MAX - CURRENT_RANGE_MIN);
    
    // 限制压力值在合理范围内
    if (pressure < PRESSURE_RANGE_MIN) {
        pressure = PRESSURE_RANGE_MIN;
    }
    
    return pressure;
}

EtherCATMaster::PressureStatus EtherCATMaster::checkPressureStatus(uint8_t channel) {
    if (channel < 1 || channel > 4) {
        return PRESSURE_OUT_OF_RANGE;
    }

    int16_t analog_value = readAnalogInputPDO(channel);
    
    // 检查传感器故障
    if (checkForSensorError(analog_value)) {
        return PRESSURE_SENSOR_ERROR;
    }
    
    // 检查零点漂移
    if (checkForZeroDrift(analog_value)) {
        return PRESSURE_ZERO_DRIFT;
    }
    
    // 转换为压力值
    float pressure = convertAnalogToPressure(analog_value);
    
    // 检查过载
    if (checkForOverload(pressure)) {
        return PRESSURE_OVERLOAD;
    }
    
    // 检查超量程
    if (pressure > PRESSURE_RANGE_MAX) {
        return PRESSURE_OVER_RANGE;
    }
    
    return PRESSURE_NORMAL;
}

// 压力状态检查函数
bool EtherCATMaster::checkForZeroDrift(int16_t analog_value) {
    // 检查零点漂移: 电流值 < 3.8mA (预留0.2mA容差)
    float current = convertAnalogToCurrent(analog_value);
    return current < (CURRENT_RANGE_MIN - 0.2f);
}

bool EtherCATMaster::checkForOverload(float pressure_value) {
    // 检查过载: 压力 > 200bar
    return pressure_value > OVERLOAD_PRESSURE;
}

float EtherCATMaster::convertAnalogToPressure(int16_t analog_value) {
    // 一步转换: 模拟量值 → 压力值
    float current_value = convertAnalogToCurrent(analog_value);
    return convertCurrentToPressure(current_value);
}

bool EtherCATMaster::checkForSensorError(int16_t analog_value) {
    // 检查传感器故障: 电流值超出正常范围
    float current = convertAnalogToCurrent(analog_value);
    return current < 3.0f || current > 21.0f; // 超出3-21mA范围视为故障
}

std::string EtherCATMaster::getPressureStatusString(PressureStatus status) {
    switch (status) {
        case PRESSURE_NORMAL:
            return "正常";
        case PRESSURE_ZERO_DRIFT:
            return "零点漂移";
        case PRESSURE_OVER_RANGE:
            return "超量程(100-200bar)";
        case PRESSURE_OVERLOAD:
            return "过载警告(>200bar)";
        case PRESSURE_SENSOR_ERROR:
            return "传感器故障";
        case PRESSURE_OUT_OF_RANGE:
            return "通道超出范围";
        default:
            return "未知状态";
    }
}

void EtherCATMaster::processThreadFunc() {
    std::cout << "启动 EtherCAT 处理线程..." << std::endl;
    
    auto next_cycle = std::chrono::steady_clock::now();
    int cycle_counter = 0;
    
    while (running) {
        next_cycle += std::chrono::milliseconds(10); // 10ms 周期
        
        processCycle();
        
        // 每10个周期更新一次状态
        if (cycle_counter % 10 == 0) {
            updateMasterStatus();
        }

        cycle_counter++;
        if (cycle_counter % 100 == 0) { // 每100个周期检查一次状态
            checkDomainState();
            if (cycle_counter % 1000 == 0) { // 每1000个周期检查一次主站状态
                checkMasterState();
            }
        }
        
        std::this_thread::sleep_until(next_cycle);
    }
    
    std::cout << "EtherCAT 处理线程已停止" << std::endl;
}

void EtherCATMaster::processCycle() {
    if (!running) return;
    
    // 接收 EtherCAT 帧
    ecrt_master_receive(master);
    
    // 处理域数据
    ecrt_domain_process(domain);
    
    // 写入继电器输出状态
    writeRelayOutputs();
    
    // 发送过程数据
    ecrt_domain_queue(domain);
    ecrt_master_send(master);
}

void EtherCATMaster::checkDomainState() {
    ec_domain_state_t ds;
    ecrt_domain_state(domain, &ds);
    
    if (ds.working_counter != domain_state.working_counter) {
        std::cout << "Domain: WC " << ds.working_counter << std::endl;
    }
    if (ds.wc_state != domain_state.wc_state) {
        std::cout << "Domain: State " << ds.wc_state << std::endl;
    }
    domain_state = ds;
}

void EtherCATMaster::writeRelayOutputs() {
    if (!domain_data) return;
    
    // 使用 EC_WRITE_U8 宏写入继电器输出状态
    // 类似于示例代码中的: EC_WRITE_U8(domain1_pd + off_dig_out0, blink ? 0xFF : 0x00);
    uint8_t current_states = relay_states.load();
    
    // for (int i = 0; i < 4; i++) {
    //     uint8_t* output_data = domain_data + off_relay_out[i];
    //     uint8_t channel_state = (current_states & (1 << i)) ? 0x01 : 0;
    //     EC_WRITE_U8(output_data, channel_state);
    // }

    // uint8_t* output_data = domain_data + off_relay_out[0];
    // uint8_t channel_state = (current_states) ? 0x01 : 0;
    // EC_WRITE_U8(output_data, channel_state);


    uint8_t* output_data = domain_data + off_relay_out[0];
    EC_WRITE_U8(output_data, current_states);
}

void EtherCATMaster::checkMasterState() {
    ec_master_state_t ms;
    ecrt_master_state(master, &ms);
    
    if (ms.slaves_responding != master_state.slaves_responding) {
        std::cout << "Slaves: " << ms.slaves_responding << std::endl;
    }
    if (ms.al_states != master_state.al_states) {
        std::cout << "AL states: 0x" << std::hex << ms.al_states << std::dec << std::endl;
    }
    if (ms.link_up != master_state.link_up) {
        std::cout << "Link: " << (ms.link_up ? "up" : "down") << std::endl;
    }
    master_state = ms;
}

void EtherCATMaster::cancelCurrentTest() {
    test_cancelled = true;
    if (test_thread.joinable()) {
        test_thread.join();
    }
    test_running = false;
    current_test_status = TestStatus::TEST_CANCELLED;
}

void EtherCATMaster::taskThreadFunc() {
    log(LogLevel::LOG_INFO, "Master", "启动任务线程");
    
    while (running) {
        std::unique_lock<std::mutex> lock(task_mutex);
        task_cv.wait(lock, [this]() { 
            return !running || !task_queue.empty(); 
        });
        
        if (!running) break;
        
        if (!task_queue.empty()) {
            auto task = task_queue.front();
            task_queue.pop();
            lock.unlock();
            
            try {
                task();
            } catch (const std::exception& e) {
                log(LogLevel::LOG_ERROR, "TaskThread", std::string("任务执行异常: ") + e.what());
            }
        }
    }
    
    log(LogLevel::LOG_INFO, "Master", "任务线程退出");
}

// ==================== 缺失的函数实现 ====================

// 添加任务到队列
void EtherCATMaster::addTask(const std::function<void()>& task) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push(task);
    task_cv.notify_one();
}

// 快捷键监听线程函数（成员函数版本，实际使用全局函数）
void EtherCATMaster::hotkeyListenerThread() {
    // 快捷键监听功能由全局函数 hotkeyListener() 实现
    // 此函数为接口兼容性保留
}

// 获取测试状态
TestStatus EtherCATMaster::getTestStatus() const {
    return current_test_status.load();
}

// 设置压力数据回调
void EtherCATMaster::setPressureDataCallback(PressureDataCallback callback) {
    pressure_callback = callback;
}

// 异步设置继电器通道
void EtherCATMaster::setRelayChannelAsync(uint8_t channel, bool state, 
                                          std::function<void(bool)> callback) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push([this, channel, state, callback]() {
        bool result = setRelayChannel(channel, state);
        if (callback) {
            callback(result);
        }
    });
    task_cv.notify_one();
}

// 异步设置所有继电器
void EtherCATMaster::setAllRelaysAsync(bool state, 
                                       std::function<void(bool)> callback) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push([this, state, callback]() {
        bool result = setAllRelays(state);
        if (callback) {
            callback(result);
        }
    });
    task_cv.notify_one();
}

// 读取模拟输入为电流值 (mA)
float EtherCATMaster::readAnalogInputAsCurrent(uint8_t channel) {
    if (channel < 1 || channel > 4) {
        log(LogLevel::LOG_ERROR, "Analog", "无效的模拟通道: " + std::to_string(channel));
        return 0.0f;
    }
    
    int16_t raw_value = readAnalogInputPDO(channel);
    return convertAnalogToCurrent(raw_value);
}

// 读取所有模拟输入为电流值
std::vector<float> EtherCATMaster::readAllAnalogInputsAsCurrent() {
    std::vector<float> currents(4);
    
    for (int i = 0; i < 4; i++) {
        int16_t raw_value = readAnalogInputPDO(i + 1);
        currents[i] = convertAnalogToCurrent(raw_value);
    }
    
    return currents;
}

// 读取所有模拟输入为压力值
std::vector<float> EtherCATMaster::readAllAnalogInputsAsPressure() {
    std::vector<float> pressures(4, 0.0f);
    
    if (!running || !domain_data) {
        return pressures;
    }
    
    for (int i = 0; i < 4; i++) {
        int16_t raw_value = readAnalogInputPDO(i + 1);
        float current_value = convertAnalogToCurrent(raw_value);
        pressures[i] = convertCurrentToPressure(current_value);
    }
    
    return pressures;
}

// 异步读取模拟输入
void EtherCATMaster::readAnalogInputAsync(uint8_t channel, 
                                          std::function<void(float, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push([this, channel, callback]() {
        float pressure = readAnalogInputAsPressure(channel);
        PressureStatus status = checkPressureStatus(channel);
        std::string status_str = getPressureStatusString(status);
        if (callback) {
            callback(pressure, status_str);
        }
    });
    task_cv.notify_one();
}

// 异步读取所有模拟输入
void EtherCATMaster::readAllAnalogInputsAsync(
    std::function<void(const std::vector<float>&, 
                      const std::vector<std::string>&)> callback) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push([this, callback]() {
        std::vector<float> pressures = readAllAnalogInputsAsPressure();
        std::vector<std::string> statuses(4);
        for (int i = 0; i < 4; i++) {
            PressureStatus status = checkPressureStatus(i + 1);
            statuses[i] = getPressureStatusString(status);
        }
        if (callback) {
            callback(pressures, statuses);
        }
    });
    task_cv.notify_one();
}

// 支撑测试异步执行
void EtherCATMaster::startSupportTestAsync(float target_pressure, 
                                           int timeout_ms,
                                           TestProgressCallback progress_callback,
                                           std::function<void(const TestResult&)> completion_callback) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push([this, target_pressure, timeout_ms, progress_callback, completion_callback]() {
        TestResult result;
        result.status = TestStatus::TEST_RUNNING;
        current_test_status = TestStatus::TEST_RUNNING;
        
        log(LogLevel::LOG_INFO, "Test", "开始支撑测试，目标压力: " + std::to_string(target_pressure) + " bar");
        
        // 打开继电器1，关闭继电器2
        setRelayChannel(1, true);
        setRelayChannel(2, false);
        
        auto start_time = std::chrono::steady_clock::now();
        bool success = false;
        
        while (!test_cancelled) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed > timeout_ms) {
                log(LogLevel::LOG_WARNING, "Test", "支撑测试超时");
                break;
            }
            
            // 检查压力
            std::vector<float> pressures = readAllAnalogInputsAsPressure();
            bool all_reached = true;
            for (float p : pressures) {
                if (p < target_pressure) {
                    all_reached = false;
                    break;
                }
            }
            
            if (all_reached) {
                success = true;
                log(LogLevel::LOG_INFO, "Test", "支撑测试成功，耗时: " + std::to_string(elapsed) + " ms");
                break;
            }
            
            // 报告进度
            if (progress_callback) {
                result.final_pressures = pressures;
                result.elapsed_time_ms = static_cast<int>(elapsed);
                progress_callback(result);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // 完成
        result.success = success;
        result.status = success ? TestStatus::TEST_COMPLETED : TestStatus::TEST_FAILED;
        result.final_pressures = readAllAnalogInputsAsPressure();
        result.elapsed_time_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count());
        result.message = success ? "支撑测试成功" : "支撑测试失败";
        
        current_test_status = result.status;
        test_cancelled = false;
        
        if (completion_callback) {
            completion_callback(result);
        }
    });
    task_cv.notify_one();
}

// 收回测试异步执行
void EtherCATMaster::startRetractTestAsync(float target_pressure,
                                           int timeout_ms,
                                           TestProgressCallback progress_callback,
                                           std::function<void(const TestResult&)> completion_callback) {
    std::lock_guard<std::mutex> lock(task_mutex);
    task_queue.push([this, target_pressure, timeout_ms, progress_callback, completion_callback]() {
        TestResult result;
        result.status = TestStatus::TEST_RUNNING;
        current_test_status = TestStatus::TEST_RUNNING;
        
        log(LogLevel::LOG_INFO, "Test", "开始收回测试，目标压力: " + std::to_string(target_pressure) + " bar");
        
        // 关闭继电器1，打开继电器2
        setRelayChannel(1, false);
        setRelayChannel(2, true);
        
        auto start_time = std::chrono::steady_clock::now();
        bool success = false;
        
        while (!test_cancelled) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed > timeout_ms) {
                log(LogLevel::LOG_WARNING, "Test", "收回测试超时");
                break;
            }
            
            // 检查压力
            std::vector<float> pressures = readAllAnalogInputsAsPressure();
            bool all_reached = true;
            for (float p : pressures) {
                if (p > target_pressure) {
                    all_reached = false;
                    break;
                }
            }
            
            if (all_reached) {
                success = true;
                log(LogLevel::LOG_INFO, "Test", "收回测试成功，耗时: " + std::to_string(elapsed) + " ms");
                break;
            }
            
            // 报告进度
            if (progress_callback) {
                result.final_pressures = pressures;
                result.elapsed_time_ms = static_cast<int>(elapsed);
                progress_callback(result);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // 完成
        result.success = success;
        result.status = success ? TestStatus::TEST_COMPLETED : TestStatus::TEST_FAILED;
        result.final_pressures = readAllAnalogInputsAsPressure();
        result.elapsed_time_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count());
        result.message = success ? "收回测试成功" : "收回测试失败";
        
        current_test_status = result.status;
        test_cancelled = false;
        
        if (completion_callback) {
            completion_callback(result);
        }
    });
    task_cv.notify_one();
}
