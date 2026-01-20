#ifndef ETHERCATMASTER_H
#define ETHERCATMASTER_H

// 仅在Linux + WITH_IGH_ETHERCAT时包含真正的EtherCAT头文件
#if defined(__linux__) && WITH_IGH_ETHERCAT
#include <ecrt.h>
#else
// 为非Linux平台或无EtherCAT环境定义占位类型
struct ec_master_t;
struct ec_domain_t;
struct ec_slave_config_t;
struct ec_master_state_t {
    unsigned int slaves_responding;
    unsigned int al_states;
    unsigned int link_up;
};
struct ec_domain_state_t {
    unsigned int working_counter;
    unsigned int wc_state;
};
#endif

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <functional>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <deque>

// 从站配置
// EK1100 耦合器 (位置 0)
constexpr uint16_t EK1100_VENDOR_ID = 0x00000002;
constexpr uint32_t EK1100_PRODUCT_CODE = 0x044c2c52;

// EL1008 数字输入 (位置 1)
constexpr uint16_t EL1008_VENDOR_ID = 0x00000002;
constexpr uint32_t EL1008_PRODUCT_CODE = 0x03f03052;

// EL3074 模拟输入 (位置 2)
constexpr uint16_t EL3074_VENDOR_ID = 0x00000002;
constexpr uint32_t EL3074_PRODUCT_CODE = 0x0c023052;

// EL2634 继电器输出 (位置 3)
constexpr uint16_t EL2634_VENDOR_ID = 0x00000002;
constexpr uint32_t EL2634_PRODUCT_CODE = 0x0a4a3052;

// EL6001 RS232接口 (位置 4) - 不需要PDO配置
constexpr uint16_t EL6001_VENDOR_ID = 0x00000002;
constexpr uint32_t EL6001_PRODUCT_CODE = 0x17713052;

// EL6751 CANopen主站 (位置 5) - 不需要PDO配置
constexpr uint16_t EL6751_VENDOR_ID = 0x00000002;
constexpr uint32_t EL6751_PRODUCT_CODE = 0x1a5f3052;

// 添加压力传感器相关常量
constexpr float PRESSURE_RANGE_MIN = 0.0f;      // 最小压力 0 bar
constexpr float PRESSURE_RANGE_MAX = 100.0f;    // 最大压力 100 bar
constexpr float CURRENT_RANGE_MIN = 4.0f;       // 最小电流 4 mA
constexpr float CURRENT_RANGE_MAX = 20.0f;      // 最大电流 20 mA
constexpr float OVERLOAD_PRESSURE = 200.0f;     // 过载压力 200 bar
constexpr float BURST_PRESSURE = 800.0f;        // 爆破压力 800 bar
constexpr int16_t ADC_MAX_VALUE = 32767;        // ADC最大值

// 主站状态枚举
enum class MasterStatus {
    STATUS_UNINITIALIZED,   // 未初始化
    STATUS_INITIALIZING,    // 初始化中
    STATUS_OPERATIONAL,     // 运行正常
    STATUS_WARNING,         // 警告状态
    STATUS_ERROR,           // 错误状态
    STATUS_STOPPED,         // 已停止
    STATUS_FAULT            // 故障状态
};

// 测试状态枚举
enum class TestStatus {
    TEST_IDLE,              // 测试空闲
    TEST_RUNNING,           // 测试运行中
    TEST_PAUSED,            // 测试暂停
    TEST_COMPLETED,         // 测试完成
    TEST_FAILED,            // 测试失败
    TEST_CANCELLED          // 测试取消
};

// 日志级别
enum class LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
};

// 日志条目
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string module;
    std::string message;
    int cycle_number;  // 关联的测试周期号
    
    LogEntry() : level(LogLevel::LOG_INFO), cycle_number(0) {}
    
    std::string toString() const {
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        char time_str[100];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));
        
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()) % 1000;
        
        std::string level_str;
        switch(level) {
            case LogLevel::LOG_DEBUG: level_str = "DEBUG"; break;
            case LogLevel::LOG_INFO: level_str = "INFO"; break;
            case LogLevel::LOG_WARNING: level_str = "WARNING"; break;
            case LogLevel::LOG_ERROR: level_str = "ERROR"; break;
            case LogLevel::LOG_CRITICAL: level_str = "CRITICAL"; break;
        }
        
        std::string result = std::string(time_str) + "." + 
                           std::to_string(ms.count()) + " [" + 
                           level_str + "] [" + module + "] ";
        if (cycle_number > 0) {
            result += "[Cycle " + std::to_string(cycle_number) + "] ";
        }
        result += message;
        return result;
    }
};

// 连续可靠性测试统计（无限运行）
struct ReliabilityTestStats {
    int total_cycles;                   // 总测试周期数
    int current_cycle;                  // 当前周期
    int support_success_count;          // 支撑成功次数
    int support_fail_count;             // 支撑失败次数
    int retract_success_count;          // 收回成功次数
    int retract_fail_count;             // 收回失败次数
    int consecutive_support_failures;   // 连续支撑失败次数
    int consecutive_retract_failures;   // 连续收回失败次数
    int max_support_failures;           // 最大连续支撑失败
    int max_retract_failures;           // 最大连续收回失败
    float avg_support_time_ms;          // 平均支撑时间(ms)
    float avg_retract_time_ms;          // 平均收回时间(ms)
    std::chrono::steady_clock::time_point start_time;  // 测试开始时间
    std::chrono::steady_clock::time_point end_time;    // 测试结束时间
    std::deque<std::pair<int, bool>> recent_cycles;    // 最近100个周期结果<周期号, 是否成功>
    std::deque<float> recent_support_times;            // 最近100个支撑耗时
    std::deque<float> recent_retract_times;            // 最近100个收回耗时
    std::vector<LogEntry> critical_logs;               // 关键日志（错误、警告等）
    
    ReliabilityTestStats() 
        : total_cycles(0)
        , current_cycle(0)
        , support_success_count(0)
        , support_fail_count(0)
        , retract_success_count(0)
        , retract_fail_count(0)
        , consecutive_support_failures(0)
        , consecutive_retract_failures(0)
        , max_support_failures(0)
        , max_retract_failures(0)
        , avg_support_time_ms(0.0f)
        , avg_retract_time_ms(0.0f) {
    }
    
    // 获取最近N个周期的统计数据
    float getRecentSupportSuccessRate(int n = 100) const {
        if (recent_cycles.empty() || n <= 0) return 0.0f;
        
        int count = std::min(n, static_cast<int>(recent_cycles.size()));
        int success_count = 0;
        for (int i = 0; i < count; i++) {
            if (recent_cycles[i].second) success_count++;
        }
        return (success_count * 100.0f) / count;
    }
    
    // 获取当前小时的成功率
    float getSupportSuccessRate() const {
        if (total_cycles == 0) return 0.0f;
        return (support_success_count * 100.0f) / total_cycles;
    }
    
    float getRetractSuccessRate() const {
        if (total_cycles == 0) return 0.0f;
        return (retract_success_count * 100.0f) / total_cycles;
    }
    
    float getOverallSuccessRate() const {
        if (total_cycles == 0) return 0.0f;
        int total_operations = total_cycles * 2;  // 每个周期有2个操作
        int total_success = support_success_count + retract_success_count;
        return (total_success * 100.0f) / total_operations;
    }
    
    std::chrono::duration<double> getElapsedTime() const {
        if (end_time.time_since_epoch().count() == 0) {
            return std::chrono::steady_clock::now() - start_time;
        }
        return end_time - start_time;
    }
    
    // 添加新的周期结果
    void addCycleResult(int cycle, bool support_success, float support_time, 
                       bool retract_success, float retract_time) {
        current_cycle = cycle;
        total_cycles = cycle;
        
        // 更新支撑统计
        if (support_success) {
            support_success_count++;
            consecutive_support_failures = 0;
        } else {
            support_fail_count++;
            consecutive_support_failures++;
            if (consecutive_support_failures > max_support_failures) {
                max_support_failures = consecutive_support_failures;
            }
        }
        
        // 更新收回统计
        if (retract_success) {
            retract_success_count++;
            consecutive_retract_failures = 0;
        } else {
            retract_fail_count++;
            consecutive_retract_failures++;
            if (consecutive_retract_failures > max_retract_failures) {
                max_retract_failures = consecutive_retract_failures;
            }
        }
        
        // 更新最近周期记录
        recent_cycles.push_front(std::make_pair(cycle, support_success));
        recent_support_times.push_front(support_time);
        recent_retract_times.push_front(retract_time);
        
        // 保持最近100个记录
        const size_t MAX_RECENT = 100;
        if (recent_cycles.size() > MAX_RECENT) {
            recent_cycles.pop_back();
            recent_support_times.pop_back();
            recent_retract_times.pop_back();
        }
        
        // 更新平均时间
        updateAverageTimes();
    }
    
    void updateAverageTimes() {
        if (!recent_support_times.empty()) {
            float total = 0;
            for (float t : recent_support_times) total += t;
            avg_support_time_ms = total / recent_support_times.size();
        }
        
        if (!recent_retract_times.empty()) {
            float total = 0;
            for (float t : recent_retract_times) total += t;
            avg_retract_time_ms = total / recent_retract_times.size();
        }
    }
    
    void addCriticalLog(const LogEntry& log) {
        critical_logs.push_back(log);
        // 只保留最近的50条关键日志
        if (critical_logs.size() > 50) {
            critical_logs.erase(critical_logs.begin());
        }
    }
};

// 测试结果结构体
struct TestResult {
    TestStatus status;                     // 测试状态
    bool success;                          // 是否成功
    std::string message;                   // 测试消息
    std::vector<float> final_pressures;    // 最终压力值
    std::vector<std::string> logs;         // 测试日志
    int elapsed_time_ms;                   // 耗时(毫秒)
    ReliabilityTestStats stats;            // 可靠性测试统计
    
    TestResult() 
        : status(TestStatus::TEST_IDLE)
        , success(false)
        , elapsed_time_ms(0) {
    }
};

// 压力数据回调函数类型
using PressureDataCallback = std::function<void(int channel, float pressure, const std::string& status)>;
using TestProgressCallback = std::function<void(const TestResult& result)>;
using ReliabilityProgressCallback = std::function<void(const ReliabilityTestStats& stats)>;

// 日志回调函数类型
using LogCallback = std::function<void(const LogEntry& log)>;

// 主站状态信息结构体
struct MasterStateInfo {
    MasterStatus status;             // 主站状态
    int slaves_responding;           // 响应从站数量
    uint8_t al_states;               // 应用层状态
    bool link_up;                    // 以太网链接状态
    std::chrono::system_clock::time_point last_update; // 最后更新时间
    
    MasterStateInfo() 
        : status(MasterStatus::STATUS_UNINITIALIZED)
        , slaves_responding(0)
        , al_states(0)
        , link_up(false)
        , last_update(std::chrono::system_clock::now()) {
    }
};

class EtherCATMaster {
public:
    EtherCATMaster();
    ~EtherCATMaster();

    // 禁止拷贝和赋值
    EtherCATMaster(const EtherCATMaster&) = delete;
    EtherCATMaster& operator=(const EtherCATMaster&) = delete;

    bool initialize();
    bool start();
    void stop();
    void processCycle();

    // EL2634 继电器控制 - PDO方式 (同步版本)
    bool setRelayChannel(uint8_t channel, bool state);
    bool setAllRelays(bool state);
    bool toggleRelayChannel(uint8_t channel);
    
    // EL2634 继电器控制 - PDO方式 (异步版本)
    void setRelayChannelAsync(uint8_t channel, bool state, 
                             std::function<void(bool)> callback = nullptr);
    void setAllRelaysAsync(bool state, 
                          std::function<void(bool)> callback = nullptr);
    
    // EL1008 数字输入读取 - PDO方式
    bool readDigitalInput(uint8_t channel);
    std::vector<bool> readAllDigitalInputs();
    
    // EL3074 模拟输入读取 - PDO方式
    float readAnalogInput(uint8_t channel);
    std::vector<float> readAllAnalogInputs();

    float readAnalogInputAsCurrent(uint8_t channel); // 返回电流值(mA)
    float readAnalogInputAsPressure(uint8_t channel);// 返回压力值(bar)

    std::vector<float> readAllAnalogInputsAsCurrent(); // 返回电流值向量
    std::vector<float> readAllAnalogInputsAsPressure(); // 返回压力值向量

    // 异步读取压力传感器
    void readAnalogInputAsync(uint8_t channel, 
                             std::function<void(float, const std::string&)> callback);
    void readAllAnalogInputsAsync(
        std::function<void(const std::vector<float>&, 
                          const std::vector<std::string>&)> callback);

    // 压力传感器状态检查
    enum PressureStatus {
        PRESSURE_NORMAL = 0,     // 正常范围 0-100 bar
        PRESSURE_ZERO_DRIFT,     // 零点漂移 (< 4mA)
        PRESSURE_OVER_RANGE,     // 超量程 100-200 bar
        PRESSURE_OVERLOAD,       // 过载 > 200 bar
        PRESSURE_SENSOR_ERROR,   // 传感器故障
        PRESSURE_OUT_OF_RANGE    // 超出可测量范围
    };

    PressureStatus checkPressureStatus(uint8_t channel);
    std::string getPressureStatusString(PressureStatus status);

    // 模拟量转换函数
    float convertAnalogToCurrent(int16_t analog_value);
    float convertCurrentToPressure(float current_value);
    float convertAnalogToPressure(int16_t analog_value);

    // 状态检查函数
    bool checkForZeroDrift(int16_t analog_value);
    bool checkForOverload(float pressure_value);
    bool checkForSensorError(int16_t analog_value);
    
    // 状态监控
    void printMasterState();
    void printSlaveStates();
    void printDomainData();
    void checkDomainState();
    void checkMasterState();
    
    bool isRunning() const { return running; }
    bool isInitialized() const { return initialized; }
    
    // 新增：主站状态实时监测功能
    bool checkMasterHealth();                           // 检查主站健康状态
    MasterStatus getMasterStatus() const;               // 获取主站状态
    MasterStateInfo getMasterStateInfo() const;         // 获取详细的主站状态信息
    bool waitForOperational(int timeout_ms = 5000);     // 等待主站进入运行状态
    bool isOperational() const;                         // 检查主站是否运行正常
    std::string getMasterStatusString() const;          // 获取状态字符串
    void printHealthStatus();                           // 打印健康状态
    bool verifyOperation(const std::string& operation_name);  // 验证操作可行性

    // 新增：UI友好的异步测试函数
    void startSupportTestAsync(float target_pressure = 22.0f, 
                               int timeout_ms = 15000,
                               TestProgressCallback progress_callback = nullptr,
                               std::function<void(const TestResult&)> completion_callback = nullptr);
    
    void startRetractTestAsync(float target_pressure = 1.0f,
                               int timeout_ms = 15000,
                               TestProgressCallback progress_callback = nullptr,
                               std::function<void(const TestResult&)> completion_callback = nullptr);
    
    void cancelCurrentTest();                           // 取消当前测试
    TestStatus getTestStatus() const;                   // 获取测试状态
    void setPressureDataCallback(PressureDataCallback callback); // 设置压力数据回调

    // 新增：无限连续可靠性测试
    void startInfiniteReliabilityTestAsync(float support_target = 22.0f,
                                           float retract_target = 1.0f,
                                           int support_timeout = 15000,
                                           int retract_timeout = 15000,
                                           ReliabilityProgressCallback progress_callback = nullptr,
                                           std::function<void(const ReliabilityTestStats&)> completion_callback = nullptr);
    
    void stopReliabilityTest(bool generate_report = true);  // 停止可靠性测试
    bool isReliabilityTestRunning() const;              // 检查可靠性测试是否运行
    ReliabilityTestStats getReliabilityTestStats() const; // 获取可靠性测试统计
    
    // 新增：日志记录功能
    void log(LogLevel level, const std::string& module, const std::string& message, int cycle_number = 0);
    void setLogCallback(LogCallback callback);          // 设置日志回调
    void setLogFile(const std::string& filename);       // 设置日志文件
    void flushLog();                                    // 刷新日志到文件
    std::vector<LogEntry> getRecentLogs(int count = 100) const; // 获取最近的日志
    std::vector<LogEntry> getCriticalLogs() const;      // 获取关键日志
    
    // 新增：测试结果保存功能
    bool saveTestResultsToFile(const std::string& filename, const ReliabilityTestStats& stats);
    void printReliabilityTestReport(const ReliabilityTestStats& stats) const;
    void saveCurrentTestReport(const std::string& filename = ""); // 保存当前测试报告
    
    // 新增：快捷键支持
    void setHotkeyCallback(std::function<void(int)> callback); // 设置快捷键回调

private:
    ec_master_t* master;
    ec_domain_t* domain;
    ec_master_state_t master_state;
    ec_domain_state_t domain_state;
    
    std::vector<ec_slave_config_t*> slave_configs;
    
    // PDO 数据指针
    uint8_t* domain_data;
    
    // 从站偏移量
    unsigned int off_dig_in[8];  // EL1008 8个数字输入
    unsigned int off_ai_val[4];  // EL3074 4个模拟输入值
    unsigned int off_relay_out[4]; // EL2634 4个继电器输出
    
    // 继电器状态缓存（用于确保状态一致性）
    std::atomic<uint8_t> relay_states;
    
    bool initialized;
    std::atomic<bool> running;
    std::thread process_thread;
    
    // 新增：状态监测相关成员
    MasterStateInfo master_state_info;
    mutable std::mutex state_mutex;                     // 保护状态信息
    std::atomic<MasterStatus> current_status;           // 当前主站状态
    
    // 新增：异步任务相关成员
    std::thread test_thread;                            // 测试线程
    std::atomic<bool> test_running;                     // 测试是否运行中
    std::atomic<bool> test_cancelled;                   // 测试是否被取消
    std::atomic<TestStatus> current_test_status;        // 当前测试状态
    std::mutex task_mutex;                              // 任务队列互斥锁
    std::condition_variable task_cv;                    // 任务条件变量
    std::queue<std::function<void()>> task_queue;       // 任务队列
    std::thread task_thread;                            // 任务处理线程
    PressureDataCallback pressure_callback;             // 压力数据回调
    
    // 新增：无限可靠性测试相关成员
    std::thread infinite_reliability_test_thread;       // 无限可靠性测试线程
    std::atomic<bool> infinite_test_running;            // 无限测试是否运行
    std::atomic<bool> stop_infinite_test;               // 停止无限测试标志
    ReliabilityTestStats reliability_stats;             // 可靠性测试统计
    mutable std::mutex stats_mutex;                     // 保护统计数据
    
    // 新增：日志记录相关成员
    mutable std::mutex log_mutex;                       // 保护日志
    std::deque<LogEntry> log_history;                   // 日志历史记录
    LogCallback log_callback;                           // 日志回调函数
    std::ofstream log_file;                             // 日志文件流
    std::string log_filename;                           // 日志文件名
    std::atomic<bool> log_to_file;                      // 是否记录到文件
    std::atomic<int> log_file_counter;                  // 日志文件计数器
    std::chrono::system_clock::time_point last_log_file_check; // 最后检查日志文件时间
    
    // 新增：快捷键支持
    std::function<void(int)> hotkey_callback;           // 快捷键回调
    std::thread hotkey_thread;                          // 快捷键监听线程
    std::atomic<bool> hotkey_listening;                 // 是否监听快捷键
    
    bool configureSlaves();
    void processThreadFunc();
    void updateMasterStatus();                          // 更新主站状态
    
    // 任务处理线程函数
    void taskThreadFunc();
    
    // 添加任务到队列
    void addTask(const std::function<void()>& task);
    
    // 测试执行函数
    TestResult executeSupportTest(float target_pressure, int timeout_ms, 
                                  TestProgressCallback progress_callback,
                                  int cycle_number = 0);
    TestResult executeRetractTest(float target_pressure, int timeout_ms,
                                  TestProgressCallback progress_callback,
                                  int cycle_number = 0);
    
    // 无限可靠性测试执行函数
    void executeInfiniteReliabilityTest(float support_target,
                                        float retract_target,
                                        int support_timeout,
                                        int retract_timeout,
                                        ReliabilityProgressCallback progress_callback,
                                        std::function<void(const ReliabilityTestStats&)> completion_callback);
    
    // 使用EC库宏进行PDO访问
    void writeRelayOutputs();
    bool readDigitalInputPDO(uint8_t channel);
    int16_t readAnalogInputPDO(uint8_t channel);
    
    // 新增：日志管理函数
    void rotateLogFile();                               // 轮转日志文件
    void checkLogFileSize();                            // 检查日志文件大小
    void writeLogToFile(const LogEntry& log);           // 写日志到文件
    
    // 新增：快捷键监听函数
    void hotkeyListenerThread();                        // 快捷键监听线程函数
    
    // 新增：工具函数
    std::string generateTimestamp() const;              // 生成时间戳字符串
    std::string generateLogFilename() const;            // 生成日志文件名
};

// 工具函数
namespace EtherCATUtils {
    void printUsage();
    void runTestSequence(EtherCATMaster& master);
    void runMonitorMode(EtherCATMaster& master);
    void runInteractiveMode(EtherCATMaster& master);
    void runSupportTest(EtherCATMaster& master);
    void runRetractTest(EtherCATMaster& master);
    void runInfiniteReliabilityTest(EtherCATMaster& master);
    void runAsyncTestExample(EtherCATMaster& master);
}

#endif // ETHERCATMASTER_H
