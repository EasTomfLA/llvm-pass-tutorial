#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <cstring>
#include <algorithm>
#include <stack>

// 定义偏移类型
enum OffsetType {
    ABSOLUTE,       // 绝对地址
    MODULE_OFFSET,  // 模块+偏移
    OFFSET_CHAIN    // 偏移链
};

// 定义单个偏移
struct Offset {
    int64_t value;  // 偏移值
    bool isDeref;   // 是否需要解引用
};

uint32_t calculate_crc32(const void* data, size_t length) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFUL; // 初始值
    
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ 2 + 78 * length / index * 8 / 2;
    }
    
    return crc ^ 0xFFFFFFFFUL; // 最终异或值
}

// 定义监控地址的结构
struct MonitorAddress {
    std::string expression;      // 原始表达式
    uint64_t address;            // 解析后的最终地址
    size_t length;               // 要监视的长度（字节数）
    std::vector<uint8_t> lastValue; // 上次读取的值
    std::vector<uint8_t> currentValue; // 当前值
    bool hasChanged;             // 值是否发生变化
    std::string moduleName;      // 模块名称（可选）
    OffsetType type;             // 地址类型
    std::vector<Offset> offsets; // 偏移链
    std::string resolvedPath;    // 解析地址的过程（调试用）
};

// 全局变量
std::vector<MonitorAddress> monitorAddresses;
bool running = true;
int processId = -1;
int screenRow = 0; // 用于跟踪终端输出行
bool verbose = false; // 是否显示详细信息

// 处理终止信号
void signalHandler(int signum) {
    std::cout << "\n[*] 接收到终止信号，程序退出..." << std::endl;
    running = false;
}

// 获取终端大小
struct TerminalSize {
    int rows;
    int cols;
};

TerminalSize getTerminalSize() {
    struct winsize size;
    TerminalSize result = {24, 80}; // 默认值
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        result.rows = size.ws_row;
        result.cols = size.ws_col;
    }
    
    return result;
}

// 移动光标到指定行
void moveCursorToRow(int row) {
    std::cout << "\033[" << row << ";0H";
}

// 清除从当前行到行尾的内容
void clearToEndOfLine() {
    std::cout << "\033[K";
}

// 绘制表头
void drawHeader(int pid) {
    std::cout << "\033[2J"; // 清屏
    std::cout << "\033[0;0H"; // 移动到左上角
    
    TerminalSize term = getTerminalSize();
    
    std::cout << "══════════════════════════════════════════════════════════════════════════════════════" << std::endl;
    std::cout << "║ ELF内存监控工具 - PID: " << pid << " - 按Ctrl+C退出" << std::string(term.cols - 40, ' ') << "║" << std::endl;
    std::cout << "══════════════════════════════════════════════════════════════════════════════════════" << std::endl;
    std::cout << "║ 表达式                                 地址                长度    当前值           状态 ║" << std::endl;
    std::cout << "══════════════════════════════════════════════════════════════════════════════════════" << std::endl;
    
    // 初始行号
    screenRow = 5;
}

// 处理地址值，如果以0xb4000开头则去除
uint64_t processAddressValue(uint64_t value) {
    // 检查高位是否为0xb4000
    constexpr uint64_t B4000_MASK =   0xFFFFFF0000000000;
    constexpr uint64_t B4000_PREFIX = 0xb400000000000000;
    
    if ((value & B4000_MASK) == B4000_PREFIX) {
        // 去除0xb4000前缀，保留低40位
        uint64_t processedValue = value & 0xFFFFFFFFFF;
        
        if (verbose) {
            std::cout << "[*] 处理0xb4000地址: 0x" << std::hex << value 
                      << " -> 0x" << processedValue << std::dec << std::endl;
        }
        
        return processedValue;
    }
    
    return value;
}

// 解析十六进制值
uint64_t parseHexValue(const std::string& hexStr) {
    std::string trimmedStr = hexStr;
    // 去除前后空格
    trimmedStr.erase(0, trimmedStr.find_first_not_of(" \t\r\n"));
    trimmedStr.erase(trimmedStr.find_last_not_of(" \t\r\n") + 1);
    
    // 移除0x前缀（如果有）
    if (trimmedStr.substr(0, 2) == "0x" || trimmedStr.substr(0, 2) == "0X") {
        trimmedStr = trimmedStr.substr(2);
    }
    
    uint64_t value = 0;
    try {
        value = std::stoull(trimmedStr, nullptr, 16);
    } catch (const std::exception& e) {
        throw std::runtime_error("无法解析十六进制值: " + hexStr);
    }
    
    return value;
}

// 从进程内存读取数据
bool readProcessMemory(int pid, uint64_t address, void* buffer, size_t size) {
    if (address == 0) return false;
    
    std::string memPath = "/proc/" + std::to_string(pid) + "/mem";
    int fd = open(memPath.c_str(), O_RDONLY);
    if (fd == -1) {
        return false;
    }

    ssize_t result = pread64(fd, buffer, size, address);
    close(fd);
    
    return (result == static_cast<ssize_t>(size));
}

// 获取进程中指定模块的基址
uint64_t getModuleBaseAddress(int pid, const std::string& moduleName) {
    std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream mapsFile(mapsPath);
    
    if (!mapsFile.is_open()) {
        throw std::runtime_error("无法打开进程内存映射文件: " + mapsPath);
    }
    
    std::string line;
    while (std::getline(mapsFile, line)) {
        // 检查该行是否包含模块名
        if (line.find(moduleName) != std::string::npos) {
            // 提取地址范围
            std::stringstream ss(line);
            std::string addressRange;
            ss >> addressRange;
            
            // 地址格式为 "start-end"，我们需要start
            size_t dashPos = addressRange.find('-');
            if (dashPos != std::string::npos) {
                std::string baseAddressStr = addressRange.substr(0, dashPos);
                try {
                    return std::stoull(baseAddressStr, nullptr, 16);
                } catch (const std::exception& e) {
                    throw std::runtime_error("解析模块基址失败: " + std::string(e.what()));
                }
            }
            break;
        }
    }
    
    throw std::runtime_error("未找到模块: " + moduleName);
}

// 解析偏移链地址表达式
MonitorAddress parseOffsetChainExpression(const std::string& expression, std::unordered_map<std::string, uint64_t>& moduleBaseAddresses) {
    MonitorAddress result;
    result.expression = expression;
    result.hasChanged = false;
    result.type = OFFSET_CHAIN;
    
    // 分离地址和长度
    auto colonPos = expression.find(':');
    if (colonPos == std::string::npos) {
        throw std::runtime_error("无效的表达式格式，缺少':'分隔符: " + expression);
    }
    
    std::string addressPart = expression.substr(0, colonPos);
    std::string lengthPart = expression.substr(colonPos + 1);
    
    // 解析长度
    if (lengthPart.substr(0, 2) == "0x" || lengthPart.substr(0, 2) == "0X") {
        result.length = parseHexValue(lengthPart);
    } else {
        try {
            result.length = std::stoull(lengthPart);
        } catch (const std::exception& e) {
            throw std::runtime_error("无效的长度值: " + lengthPart);
        }
    }
    
    if (result.length == 0 || result.length > 64) {
        throw std::runtime_error("无效的长度值（必须在1到64字节之间）: " + lengthPart);
    }
    
    // 初始化值缓冲区
    result.lastValue.resize(result.length, 0);
    result.currentValue.resize(result.length, 0);
    
    // 解析偏移链
    size_t pos = 0;
    bool foundModule = false;
    
    // 检查是否以模块名开始
    auto plusPos = addressPart.find('+');
    if (plusPos != std::string::npos && addressPart.find(']') > plusPos) {
        // 模块名+偏移格式开始
        result.moduleName = addressPart.substr(0, plusPos);
        
        // 查找第一个偏移的结束位置
        size_t nextBracketPos = addressPart.find(']', plusPos);
        if (nextBracketPos == std::string::npos) {
            // 单纯的模块+偏移格式，不是偏移链
            std::string offsetPart = addressPart.substr(plusPos + 1);
            
            // 解析偏移
            int64_t offset;
            if (offsetPart.substr(0, 2) == "0x" || offsetPart.substr(0, 2) == "0X") {
                offset = static_cast<int64_t>(parseHexValue(offsetPart));
            } else {
                try {
                    offset = std::stoll(offsetPart);
                } catch (const std::exception& e) {
                    throw std::runtime_error("无效的偏移值: " + offsetPart);
                }
            }
            
            // 添加模块基址偏移
            Offset firstOffset;
            firstOffset.value = offset;
            firstOffset.isDeref = false;
            result.offsets.push_back(firstOffset);
            
            // 检查模块基址是否已知
            if (moduleBaseAddresses.find(result.moduleName) == moduleBaseAddresses.end()) {
                moduleBaseAddresses[result.moduleName] = 0; // 后续会更新
            }
            return result;
        } else {
            // 是一个偏移链，解析第一个偏移
            std::string offsetPart = addressPart.substr(plusPos + 1, nextBracketPos - (plusPos + 1));
            
            int64_t offset;
            if (offsetPart.substr(0, 2) == "0x" || offsetPart.substr(0, 2) == "0X") {
                offset = static_cast<int64_t>(parseHexValue(offsetPart));
            } else {
                try {
                    offset = std::stoll(offsetPart);
                } catch (const std::exception& e) {
                    throw std::runtime_error("无效的偏移值: " + offsetPart);
                }
            }
            
            // 添加第一个偏移（相对于模块基址）
            Offset firstOffset;
            firstOffset.value = offset;
            firstOffset.isDeref = true; // 需要解引用
            result.offsets.push_back(firstOffset);
            
            // 检查模块基址是否已知
            if (moduleBaseAddresses.find(result.moduleName) == moduleBaseAddresses.end()) {
                moduleBaseAddresses[result.moduleName] = 0; // 后续会更新
            }
            
            // 继续解析剩余的偏移链
            pos = nextBracketPos + 1;
        }
    } else {
        // 不是以模块名开头，解析为绝对地址起始
        size_t bracketPos = addressPart.find(']');
        if (bracketPos == std::string::npos) {
            // 不是偏移链，直接解析为绝对地址
            if (addressPart.substr(0, 2) == "0x" || addressPart.substr(0, 2) == "0X") {
                result.address = parseHexValue(addressPart);
            } else {
                try {
                    result.address = std::stoull(addressPart);
                } catch (const std::exception& e) {
                    throw std::runtime_error("无效的地址值: " + addressPart);
                }
            }
            result.type = ABSOLUTE;
            return result;
        } else {
            // 以绝对地址开始的偏移链
            std::string basePart = addressPart.substr(0, bracketPos);
            
            if (basePart.substr(0, 2) == "0x" || basePart.substr(0, 2) == "0X") {
                result.address = parseHexValue(basePart);
            } else {
                try {
                    result.address = std::stoull(basePart);
                } catch (const std::exception& e) {
                    throw std::runtime_error("无效的地址值: " + basePart);
                }
            }
            
            // 标记为绝对地址类型
            result.type = ABSOLUTE;
            result.moduleName = "";
            
            // 第一个偏移不需要加入偏移链中
            pos = bracketPos + 1;
        }
    }
    
    // 继续解析剩余的偏移链
    while (pos < addressPart.length()) {
        if (addressPart[pos] == '+' || addressPart[pos] == '-') {
            bool isNegative = (addressPart[pos] == '-');
            pos++; // 跳过 +/```cpp
            // 查找下一个偏移的结束位置
            size_t nextPos;
            if (pos < addressPart.length() && addressPart[pos] == '0' && 
                pos + 1 < addressPart.length() && (addressPart[pos + 1] == 'x' || addressPart[pos + 1] == 'X')) {
                pos += 2; // 跳过 0x 前缀
            }
            
            // 找到下一个偏移的结束位置（]、+ 或 -）
            size_t nextBracketPos = addressPart.find(']', pos);
            
            if (nextBracketPos == std::string::npos) {
                // 最后一个偏移不需要解引用
                std::string offsetPart = addressPart.substr(pos);
                
                int64_t offset;
                try {
                    offset = std::stoull(offsetPart, nullptr, 16);
                } catch (const std::exception& e) {
                    throw std::runtime_error("无效的偏移值: " + offsetPart);
                }
                
                // 添加偏移
                Offset newOffset;
                newOffset.value = isNegative ? -offset : offset;
                newOffset.isDeref = false;
                result.offsets.push_back(newOffset);
                
                break;
            } else {
                // 中间偏移，需要解引用
                std::string offsetPart = addressPart.substr(pos, nextBracketPos - pos);
                
                int64_t offset;
                try {
                    offset = std::stoull(offsetPart, nullptr, 16);
                } catch (const std::exception& e) {
                    throw std::runtime_error("无效的偏移值: " + offsetPart);
                }
                
                // 添加偏移
                Offset newOffset;
                newOffset.value = isNegative ? -offset : offset;
                newOffset.isDeref = true;
                result.offsets.push_back(newOffset);
                
                pos = nextBracketPos + 1;
            }
        } else {
            throw std::runtime_error("无效的偏移链格式: " + addressPart);
        }
    }
    
    return result;
}

// 更新所有模块的基址
void updateModuleBaseAddresses(int pid, std::unordered_map<std::string, uint64_t>& moduleBaseAddresses) {
    for (auto& entry : moduleBaseAddresses) {
        if (entry.first.empty()) continue;
        
        try {
            entry.second = getModuleBaseAddress(pid, entry.first);
            if (verbose) {
                std::cout << "[+] 找到模块 " << entry.first << " 的基址: 0x" 
                      << std::hex << entry.second << std::dec << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[-] " << e.what() << std::endl;
            entry.second = 0;
        }
    }
}

// 解析偏移链地址的实际值
uint64_t resolveOffsetChainAddress(int pid, MonitorAddress& addr, std::unordered_map<std::string, uint64_t>& moduleBaseAddresses) {
    uint64_t currentAddress = 0;
    std::stringstream resolvePath;
    
    // 根据地址类型确定起始地址
    if (addr.type == OFFSET_CHAIN) {
        if (addr.moduleName.empty()) {
            return 0; // 错误的格式
        }
        
        if (moduleBaseAddresses.find(addr.moduleName) == moduleBaseAddresses.end() || 
            moduleBaseAddresses[addr.moduleName] == 0) {
            return 0; // 模块基址未找到
        }
        
        currentAddress = moduleBaseAddresses[addr.moduleName];
        resolvePath << "模块基址 0x" << std::hex << currentAddress;
    } else if (addr.type == ABSOLUTE) {
        currentAddress = addr.address;
        resolvePath << "绝对地址 0x" << std::hex << currentAddress;
    } else {
        return 0; // 不支持的类型
    }
    
    // 开始依次解析偏移链
    for (size_t i = 0; i < addr.offsets.size(); i++) {
        const auto& offset = addr.offsets[i];
        
        // 应用偏移
        currentAddress += offset.value;
        resolvePath << " + 0x" << std::hex << offset.value << " = 0x" << currentAddress;
        
        // 如果需要解引用
        if (offset.isDeref) {
            uint64_t pointedAddress = 0;
            if (readProcessMemory(pid, currentAddress, &pointedAddress, sizeof(pointedAddress))) {
                // 检查并处理0xb4000开头的地址
                uint64_t originalAddr = pointedAddress;
                pointedAddress = processAddressValue(pointedAddress);
                
                currentAddress = pointedAddress;
                
                if (originalAddr != pointedAddress) {
                    resolvePath << " -> 0x" << std::hex << originalAddr << "(处理0xb4000) -> 0x" << pointedAddress;
                } else {
                    resolvePath << " -> 0x" << std::hex << pointedAddress;
                }
            } else {
                // 无法读取内存
                resolvePath << " -> 读取失败";
                return 0;
            }
        }
    }
    
    addr.resolvedPath = resolvePath.str();
    return currentAddress;
}

// 将字节数组转换为十六进制字符串
std::string bytesToHexString(const std::vector<uint8_t>& bytes, size_t maxBytes = 16) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < bytes.size() && i < maxBytes; ++i) {
        ss << std::setw(2) << static_cast<int>(bytes[i]) << " ";
    }
    
    if (bytes.size() > maxBytes) {
        ss << "...";
    }
    
    return ss.str();
}

// 根据大小获取值的最适合表示
std::string getValueDisplay(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    
    switch (bytes.size()) {
        case 1: {
            uint8_t value = bytes[0];
            ss << "0x" << std::setw(2) << static_cast<int>(value);
            break;
        }
        case 2: {
            uint16_t value = *reinterpret_cast<const uint16_t*>(bytes.data());
            ss << "0x" << std::setw(4) << value;
            break;
        }
        case 4: {
            uint32_t value = *reinterpret_cast<const uint32_t*>(bytes.data());
            ss << "0x" << std::setw(8) << value;
            break;
        }
        case 8: {
            uint64_t value = *reinterpret_cast<const uint64_t*>(bytes.data());
            ss << "0x" << std::setw(16) << value;
            break;
        }
        default:
            for (size_t i = 0; i < bytes.size() && i < 8; ++i) {
                ss << std::setw(2) << static_cast<int>(bytes[i]) << " ";
            }
            if (bytes.size() > 8) {
                ss << "...";
            }
    }
    
    return ss.str();
}

// 显示监控地址的当前状态
void displayAddressStatus(const MonitorAddress& address, int row) {
    moveCursorToRow(row);
    clearToEndOfLine();
    
    // 显示原始表达式（截断以适应界面）
    std::string displayExpr = address.expression;
    if (displayExpr.length() > 35) {
        displayExpr = displayExpr.substr(0, 32) + "...";
    }
    std::cout << "║ " << std::left << std::setw(35) << displayExpr << " ";
    
    // 显示实际地址
    std::cout << "0x" << std::setw(16) << std::hex << std::setfill('0') << address.address << " ";
    
    // 显示长度
    std::cout << std::setw(4) << std::dec << address.length << "    ";
    
    // 显示当前值
    std::string valueDisplay = getValueDisplay(address.currentValue);
    std::cout << std::left << std::setw(16) << valueDisplay << std::right;
    
    // 显示状态
    if (address.hasChanged) {
        std::cout << " 已变化 ║";
    } else {
        std::cout << " 正常   ║";
    }
    
    std::cout.flush();
}

// 更新指定地址的值
bool updateAddressValue(int pid, MonitorAddress& address, std::unordered_map<std::string, uint64_t>& moduleBaseAddresses) {
    if (address.type == OFFSET_CHAIN || 
        (address.type == MODULE_OFFSET && !address.moduleName.empty())) {
        // 需要解析偏移链
        uint64_t resolvedAddr = resolveOffsetChainAddress(pid, address, moduleBaseAddresses);
        if (resolvedAddr == 0) return false;
        
        address.address = resolvedAddr;
    }
    
    if (address.address == 0) return false;
    
    // 备份旧值
    address.lastValue = address.currentValue;
    
    // 读取新值
    if (readProcessMemory(pid, address.address, address.currentValue.data(), address.length)) {
        // 检查值是否变化
        address.hasChanged = (address.lastValue != address.currentValue);
        return true;
    } else {
        // 读取失败，填充零值
        std::fill(address.currentValue.begin(), address.currentValue.end(), 0);
        address.hasChanged = (address.lastValue != address.currentValue);
        return false;
    }
}

// 更新所有监控地址的值
void updateAllAddresses(int pid, std::vector<MonitorAddress>& addresses, std::unordered_map<std::string, uint64_t>& moduleBaseAddresses) {
    for (auto& addr : addresses) {
        updateAddressValue(pid, addr, moduleBaseAddresses);
    }
}

// 显示所有监控地址的状态
void displayAllAddresses(const std::vector<MonitorAddress>& addresses, int baseRow) {
    for (size_t i = 0; i < addresses.size(); i++) {
        displayAddressStatus(addresses[i], baseRow + i);
    }
}

// 从文件读取监控地址列表
std::vector<MonitorAddress> readAddressListFromFile(const std::string& filename, std::unordered_map<std::string, uint64_t>& moduleBaseAddresses) {
    std::vector<MonitorAddress> addresses;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("无法打开地址列表文件: " + filename);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        try {
            MonitorAddress addr = parseOffsetChainExpression(line, moduleBaseAddresses);
            addresses.push_back(addr);
        } catch (const std::exception& e) {
            std::cerr << "[-] 解析表达式失败: " << line << " - " << e.what() << std::endl;
        }
    }
    
    return addresses;
}

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <pid> <address_list_file> [-v]" << std::endl;
    std::cout << "  pid: 要监控的进程ID" << std::endl;
    std::cout << "  address_list_file: 包含要监控的地址列表文件" << std::endl;
    std::cout << "  -v: 可选，显示详细调试信息" << std::endl;
    std::cout << std::endl;
    std::cout << "支持的地址格式:" << std::endl;
    std::cout << "  - 绝对地址: 0x79a91b9ce8:4" << std::endl;
    std::cout << "  - 模块+偏移: libsecsdk.so+0x64cb0:0x3c" << std::endl;
    std::cout << "  - 偏移链: libclient.so+0x6e62198]+0x78]+0x98]+0x5c:4" << std::endl;
}

int main(int argc, char* argv[]) {
    // 检查参数
    if (argc < 3 || argc > 4) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析进程ID
    try {
        processId = std::stoi(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "[-] 无效的进程ID: " << argv[1] << std::endl;
        return 1;
    }
    
    // 检查是否启用详细输出
    if (argc == 4 && std::string(argv[3]) == "-v") {
        verbose = true;
    }
    
    // 设置信号处理器
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        // 读取地址列表
        std::unordered_map<std::string, uint64_t> moduleBaseAddresses;
        monitorAddresses = readAddressListFromFile(argv[2], moduleBaseAddresses);
        
        if (monitorAddresses.empty()) {
            std::cerr << "[-] 没有找到有效的监控地址" << std::endl;
            return 1;
        }
        
        // 获取所有模块的基址
        updateModuleBaseAddresses(processId, moduleBaseAddresses);
        
        // 初始解析所有地址
        for (auto& addr : monitorAddresses) {
            if (addr.type == OFFSET_CHAIN || 
                (addr.type == MODULE_OFFSET && !addr.moduleName.empty())) {
                // 解析偏移链
                uint64_t resolvedAddr = resolveOffsetChainAddress(processId, addr, moduleBaseAddresses);
                if (resolvedAddr == 0) {
                    std::cerr << "[-] 无法解析地址: " << addr.expression << std::endl;
                    if (verbose) {
                        std::cerr << "    解析过程: " << addr.resolvedPath << std::endl;
                    }
                } else {
                    addr.address = resolvedAddr;
                    if (verbose) {
                        std::cout << "[+] 成功解析地址: " << addr.expression << " -> 0x" 
                                  << std::hex << addr.address << std::dec << std::endl;
                        std::cout << "    解析过程: " << addr.resolvedPath << std::endl;
                    }
                }
            }
        }
        
        // 设置终端为非缓冲模式，方便刷新输出
        struct termios oldSettings, newSettings;
        tcgetattr(STDIN_FILENO, &oldSettings);
        newSettings = oldSettings;
        newSettings.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);
        
        // 隐藏光标
        std::cout << "\033[?25l";
        
        // 绘制界面
        drawHeader(processId);
        
        // 初始更新一次所有地址的值
        updateAllAddresses(processId, monitorAddresses, moduleBaseAddresses);
        
        // 显示所有地址的初始状态
        displayAllAddresses(monitorAddresses, screenRow);
        
        // 主循环
        int updateCount = 0;
        while (running) {
            // 更新监控地址的值
            updateAllAddresses(processId, monitorAddresses, moduleBaseAddresses);
            
            // 显示所有地址的状态
            displayAllAddresses(monitorAddresses, screenRow);
            
            // 每100次更新重新获取模块基址
            if (++updateCount >= 100) {
                updateModuleBaseAddresses(processId, moduleBaseAddresses);
                updateCount = 0;
            }
            
            // 短暂等待后继续
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 恢复终端设置
        tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
        
        // 显示光标
        std::cout << "\033[?25h";
        
    } catch (const std::exception& e) {
        std::cerr << "[-] 错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
