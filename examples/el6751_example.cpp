/**
 * @file el6751_example.cpp
 * @brief EL6751 CANopen 主站控制示例
 * 
 * 本示例演示如何使用 IgH EtherCAT Master 控制 EL6751，
 * 进行 CANopen 从站扫描和 EDS 文件加载。
 * 
 * 编译命令:
 *   g++ -o el6751_example el6751_example.cpp \
 *       ../src/ethercat/EL6751Controller.cpp \
 *       -I../include \
 *       -I/opt/etherlab/include \
 *       -L/opt/etherlab/lib \
 *       -lethercat -lpthread
 * 
 * 运行需要 root 权限:
 *   sudo ./el6751_example
 */

#include "ethercat/EL6751Controller.h"
#include <iostream>
#include <csignal>
#include <unistd.h>

// 全局变量用于信号处理
static volatile bool g_running = true;

void signalHandler(int signum) {
    std::cout << "\n收到信号 " << signum << "，停止程序..." << std::endl;
    g_running = false;
}

void printUsage(const char* progName) {
    std::cout << "用法: " << progName << " [选项]" << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -p <位置>     EL6751 从站位置 (默认: 0)" << std::endl;
    std::cout << "  -b <波特率>   CANopen 波特率代码 (0-8, 默认: 2=500kbps)" << std::endl;
    std::cout << "  -e <文件>     加载 EDS 文件" << std::endl;
    std::cout << "  -n <节点ID>   指定节点 ID (与 -e 配合使用)" << std::endl;
    std::cout << "  -s            扫描 CANopen 节点" << std::endl;
    std::cout << "  -h            显示帮助" << std::endl;
    std::cout << std::endl;
    std::cout << "波特率代码:" << std::endl;
    std::cout << "  0 = 1 Mbps" << std::endl;
    std::cout << "  1 = 800 kbps" << std::endl;
    std::cout << "  2 = 500 kbps (默认)" << std::endl;
    std::cout << "  3 = 250 kbps" << std::endl;
    std::cout << "  4 = 125 kbps" << std::endl;
    std::cout << "  5 = 100 kbps" << std::endl;
    std::cout << "  6 = 50 kbps" << std::endl;
    std::cout << "  7 = 20 kbps" << std::endl;
    std::cout << "  8 = 10 kbps" << std::endl;
}

int main(int argc, char* argv[]) {
    // 默认参数
    uint16_t slavePosition = 0;
    int baudrateCode = 2;  // 500 kbps
    std::string edsFile;
    uint8_t nodeId = 1;
    bool doScan = false;
    
    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "p:b:e:n:sh")) != -1) {
        switch (opt) {
            case 'p':
                slavePosition = std::stoi(optarg);
                break;
            case 'b':
                baudrateCode = std::stoi(optarg);
                if (baudrateCode < 0 || baudrateCode > 8) {
                    std::cerr << "无效的波特率代码: " << baudrateCode << std::endl;
                    return 1;
                }
                break;
            case 'e':
                edsFile = optarg;
                break;
            case 'n':
                nodeId = std::stoi(optarg);
                break;
            case 's':
                doScan = true;
                break;
            case 'h':
            default:
                printUsage(argv[0]);
                return 0;
        }
    }
    
    // 设置信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::cout << "========================================" << std::endl;
    std::cout << " EL6751 CANopen 主站控制示例" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "从站位置: " << slavePosition << std::endl;
    std::cout << "波特率代码: " << baudrateCode << std::endl;
    
    // 请求 EtherCAT 主站
    ec_master_t* master = ecrt_request_master(0);
    if (!master) {
        std::cerr << "无法请求 EtherCAT 主站" << std::endl;
        std::cerr << "请确保:" << std::endl;
        std::cerr << "  1. IgH EtherCAT Master 已安装" << std::endl;
        std::cerr << "  2. EtherCAT 服务正在运行 (sudo systemctl status ethercat)" << std::endl;
        std::cerr << "  3. 以 root 权限运行程序" << std::endl;
        return 1;
    }
    std::cout << "EtherCAT 主站请求成功" << std::endl;
    
    // 创建 EL6751 控制器
    EL6751Controller el6751;
    
    // 设置节点发现回调
    el6751.setNodeDiscoveryCallback([](const CANopenNodeInfo& node) {
        std::cout << "发现 CANopen 节点:" << std::endl;
        std::cout << "  Node ID: " << (int)node.nodeId << std::endl;
        std::cout << "  状态: " << (node.isOnline ? "在线" : "离线") << std::endl;
    });
    
    // 设置错误回调
    el6751.setErrorCallback([](const std::string& error) {
        std::cerr << "EL6751 错误: " << error << std::endl;
    });
    
    // 初始化 EL6751
    if (!el6751.initialize(master, slavePosition)) {
        std::cerr << "EL6751 初始化失败" << std::endl;
        ecrt_release_master(master);
        return 1;
    }
    
    // 设置波特率
    CANopenBaudrate baudrate = static_cast<CANopenBaudrate>(baudrateCode);
    if (!el6751.setBaudrate(baudrate)) {
        std::cerr << "设置波特率失败" << std::endl;
    }
    
    // 创建域
    ec_domain_t* domain = ecrt_master_create_domain(master);
    if (!domain) {
        std::cerr << "无法创建域" << std::endl;
        ecrt_release_master(master);
        return 1;
    }
    
    // 激活主站
    if (ecrt_master_activate(master)) {
        std::cerr << "无法激活主站" << std::endl;
        ecrt_release_master(master);
        return 1;
    }
    std::cout << "EtherCAT 主站已激活" << std::endl;
    
    // 获取域数据
    uint8_t* domainData = ecrt_domain_data(domain);
    if (!domainData) {
        std::cerr << "无法获取域数据" << std::endl;
        ecrt_release_master(master);
        return 1;
    }
    
    // 等待从站进入 OP 状态
    std::cout << "等待从站进入 OP 状态..." << std::endl;
    for (int i = 0; i < 50 && g_running; ++i) {  // 最多等待 5 秒
        ecrt_master_receive(master);
        ecrt_domain_process(domain);
        ecrt_domain_queue(domain);
        ecrt_master_send(master);
        usleep(100000);  // 100ms
        
        ec_master_state_t masterState;
        ecrt_master_state(master, &masterState);
        
        if (masterState.al_states & 0x08) {  // OP 状态
            std::cout << "从站已进入 OP 状态" << std::endl;
            break;
        }
    }
    
    // 扫描 CANopen 节点
    if (doScan) {
        std::cout << "\n--- 开始扫描 CANopen 节点 ---" << std::endl;
        if (el6751.startNodeScan()) {
            auto nodes = el6751.getDiscoveredNodes();
            std::cout << "发现 " << nodes.size() << " 个节点" << std::endl;
            
            for (const auto& node : nodes) {
                std::cout << "  节点 " << (int)node.nodeId << ": "
                         << (node.isOnline ? "在线" : "离线") << std::endl;
            }
        }
        el6751.stopNodeScan();
    }
    
    // 加载 EDS 文件
    if (!edsFile.empty()) {
        std::cout << "\n--- 加载 EDS 文件 ---" << std::endl;
        if (el6751.loadEDSFile(edsFile, nodeId)) {
            std::cout << "EDS 文件加载成功" << std::endl;
            
            // 应用配置
            if (el6751.applyEDSConfiguration(nodeId)) {
                std::cout << "EDS 配置已应用" << std::endl;
            }
        } else {
            std::cerr << "EDS 文件加载失败" << std::endl;
        }
    }
    
    // 打印诊断信息
    std::cout << "\n--- 诊断信息 ---" << std::endl;
    el6751.printDiagnostics();
    
    // 主循环
    std::cout << "\n--- 运行中 (按 Ctrl+C 退出) ---" << std::endl;
    int cycleCount = 0;
    
    while (g_running) {
        // 接收 EtherCAT 帧
        ecrt_master_receive(master);
        ecrt_domain_process(domain);
        
        // 在这里添加您的控制逻辑
        // 例如：读取 PDO 数据、写入输出等
        
        // 发送 EtherCAT 帧
        ecrt_domain_queue(domain);
        ecrt_master_send(master);
        
        // 周期性输出状态
        if (cycleCount % 1000 == 0) {
            std::cout << "." << std::flush;
        }
        
        cycleCount++;
        usleep(1000);  // 1ms 周期
    }
    
    std::cout << std::endl;
    
    // 停止所有 CANopen 节点
    el6751.stopAllNodes();
    
    // 释放主站
    ecrt_release_master(master);
    
    std::cout << "程序已退出" << std::endl;
    return 0;
}
