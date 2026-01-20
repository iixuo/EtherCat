#ifndef EL6751_CONTROLLER_H
#define EL6751_CONTROLLER_H

#include <ecrt.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <functional>
#include <memory>

// EL6751 从站标识 (Beckhoff CANopen Master Terminal)
constexpr uint32_t EL6751_VENDOR_ID = 0x00000002;
constexpr uint32_t EL6751_PRODUCT_CODE = 0x1a5f3052;

// CANopen 对象字典索引
namespace EL6751_OD {
    // 基本设备信息
    constexpr uint16_t DEVICE_TYPE = 0x1000;
    constexpr uint16_t ERROR_REGISTER = 0x1001;
    constexpr uint16_t IDENTITY_OBJECT = 0x1018;
    
    // CANopen 主站配置
    constexpr uint16_t CANOPEN_CONFIG = 0x8000;
    constexpr uint16_t NODE_LIST = 0x8001;
    constexpr uint16_t MASTER_SETTINGS = 0xF800;
    constexpr uint16_t MODULAR_DEVICE = 0xF000;
    
    // CANopen 诊断
    constexpr uint16_t CANOPEN_STATUS = 0x8100;
    constexpr uint16_t NODE_STATUS = 0x8101;
}

// CANopen 波特率定义
enum class CANopenBaudrate : uint8_t {
    BAUD_1M = 0,
    BAUD_800K = 1,
    BAUD_500K = 2,
    BAUD_250K = 3,
    BAUD_125K = 4,
    BAUD_100K = 5,
    BAUD_50K = 6,
    BAUD_20K = 7,
    BAUD_10K = 8
};

// CANopen 节点状态
enum class CANopenNodeState : uint8_t {
    BOOTUP = 0x00,
    STOPPED = 0x04,
    OPERATIONAL = 0x05,
    PRE_OPERATIONAL = 0x7F,
    UNKNOWN = 0xFF
};

// EDS 文件解析后的对象条目
struct EDSObjectEntry {
    uint16_t index;
    uint8_t subIndex;
    std::string name;
    std::string dataType;
    std::string accessType;  // "ro", "rw", "wo", "const"
    std::string defaultValue;
    uint16_t bitLength;
    
    EDSObjectEntry() : index(0), subIndex(0), bitLength(0) {}
};

// CANopen 节点信息
struct CANopenNodeInfo {
    uint8_t nodeId;
    CANopenNodeState state;
    uint32_t vendorId;
    uint32_t productCode;
    uint32_t revisionNumber;
    uint32_t serialNumber;
    std::string deviceName;
    bool isOnline;
    
    // EDS 文件中的对象字典
    std::vector<EDSObjectEntry> objectDictionary;
    
    CANopenNodeInfo() 
        : nodeId(0), state(CANopenNodeState::UNKNOWN)
        , vendorId(0), productCode(0), revisionNumber(0), serialNumber(0)
        , isOnline(false) {}
};

// PDO 映射配置
struct PDOMapping {
    uint16_t cobId;
    uint16_t index;
    uint8_t subIndex;
    uint8_t bitLength;
};

// EL6751 控制器类
class EL6751Controller {
public:
    // 回调函数类型定义
    using NodeDiscoveryCallback = std::function<void(const CANopenNodeInfo&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    EL6751Controller();
    ~EL6751Controller();
    
    // 初始化
    bool initialize(ec_master_t* master, uint16_t slavePosition);
    
    // 配置 CANopen 网络
    bool setBaudrate(CANopenBaudrate baudrate);
    CANopenBaudrate getBaudrate();
    
    // 从站扫描
    bool startNodeScan();
    bool stopNodeScan();
    std::vector<CANopenNodeInfo> getDiscoveredNodes();
    bool isNodeOnline(uint8_t nodeId);
    
    // EDS 文件操作
    bool loadEDSFile(const std::string& filename, uint8_t nodeId);
    bool parseEDSFile(const std::string& filename, std::vector<EDSObjectEntry>& entries);
    bool applyEDSConfiguration(uint8_t nodeId);
    
    // CANopen 节点控制
    bool setNodeState(uint8_t nodeId, CANopenNodeState state);
    CANopenNodeState getNodeState(uint8_t nodeId);
    bool startAllNodes();
    bool stopAllNodes();
    bool resetNode(uint8_t nodeId);
    
    // SDO 访问 (通过 EtherCAT SDO 到 CANopen SDO)
    bool readNodeSDO(uint8_t nodeId, uint16_t index, uint8_t subIndex, 
                     void* data, size_t* dataLen);
    bool writeNodeSDO(uint8_t nodeId, uint16_t index, uint8_t subIndex,
                      const void* data, size_t dataLen);
    
    // PDO 配置
    bool configureRxPDO(uint8_t nodeId, const std::vector<PDOMapping>& mappings);
    bool configureTxPDO(uint8_t nodeId, const std::vector<PDOMapping>& mappings);
    
    // PDO 数据访问
    void setRxPDOData(uint8_t nodeId, uint8_t pdoIndex, const uint8_t* data, size_t len);
    void getTxPDOData(uint8_t nodeId, uint8_t pdoIndex, uint8_t* data, size_t len);
    
    // 诊断信息
    uint8_t getErrorRegister();
    std::string getStatusString();
    void printDiagnostics();
    
    // 回调设置
    void setNodeDiscoveryCallback(NodeDiscoveryCallback callback);
    void setErrorCallback(ErrorCallback callback);
    
    // 获取从站信息
    uint16_t getPosition() const { return position_; }
    bool isConfigured() const { return configured_; }

private:
    ec_master_t* master_;
    ec_slave_config_t* slaveConfig_;
    uint16_t position_;
    bool configured_;
    
    // 发现的节点列表
    std::map<uint8_t, CANopenNodeInfo> discoveredNodes_;
    
    // EDS 文件缓存
    std::map<uint8_t, std::vector<EDSObjectEntry>> edsCache_;
    
    // 回调函数
    NodeDiscoveryCallback nodeDiscoveryCallback_;
    ErrorCallback errorCallback_;
    
    // PDO 偏移量
    unsigned int offRxPdo_[128];  // 支持最多128个节点
    unsigned int offTxPdo_[128];
    
    // 内部辅助函数
    bool configureSlaveConfig();
    bool readEL6751SDO(uint16_t index, uint8_t subIndex, void* data, size_t* len);
    bool writeEL6751SDO(uint16_t index, uint8_t subIndex, const void* data, size_t len);
    std::string parseDataType(const std::string& edsType);
    uint16_t getDataTypeSize(const std::string& dataType);
    void logError(const std::string& message);
};

// EDS 文件解析器 (独立工具类)
class EDSParser {
public:
    EDSParser();
    ~EDSParser();
    
    // 解析 EDS 文件
    bool parse(const std::string& filename);
    
    // 获取解析结果
    std::string getDeviceName() const { return deviceName_; }
    std::string getVendorName() const { return vendorName_; }
    uint32_t getVendorId() const { return vendorId_; }
    uint32_t getProductCode() const { return productCode_; }
    
    // 获取对象字典条目
    std::vector<EDSObjectEntry> getObjectEntries() const { return entries_; }
    
    // 查找特定对象
    bool findObject(uint16_t index, uint8_t subIndex, EDSObjectEntry& entry) const;
    
    // 获取 PDO 映射
    std::vector<PDOMapping> getRxPDOMappings() const;
    std::vector<PDOMapping> getTxPDOMappings() const;
    
    // 获取错误信息
    std::string getLastError() const { return lastError_; }

private:
    std::string deviceName_;
    std::string vendorName_;
    uint32_t vendorId_;
    uint32_t productCode_;
    uint32_t revisionNumber_;
    std::vector<EDSObjectEntry> entries_;
    std::string lastError_;
    
    // 解析辅助函数
    std::string trim(const std::string& str);
    bool parseSection(const std::string& sectionName, 
                     std::map<std::string, std::string>& properties,
                     const std::vector<std::string>& lines, size_t& lineIndex);
    bool parseObjectEntry(const std::string& indexStr, 
                         const std::map<std::string, std::string>& properties);
};

#endif // EL6751_CONTROLLER_H
