#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace aura {

/**
 * @brief 已验证的 ASUS HAL 驱动版本信息与安全特征
 */
struct HalVersionInfo {
    std::string sha256;                   // 小写 64-char 十六进制散列
    std::string file_version;             // PE 资源文件版本 (如 "1.3.46.0")
    DWORD rva_logger{0};                  // Logger::Log 相对虚拟地址
    DWORD rva_enable{0};                  // EnableLog 标志位相对虚拟地址
    std::vector<uint8_t> logger_prologue; // 关键原版机器指令序言
};

/**
 * @brief 兼容性 Gate 判定状态
 */
enum class HalGateStatus {
    Supported,          // 已受支持并通过全部签名校验
    FileNotFound,       // 磁盘文件不存在
    ReadError,          // 无法读取文件或计算散列
    UnsupportedVersion, // SHA-256 或版本号未在已验证支持列表中
    SignatureMismatch   // 内存中关键 runtime signature / patch target 校验失败
};

/**
 * @brief 兼容性 Gate 校验诊断结果
 */
struct HalGateResult {
    HalGateStatus status{HalGateStatus::UnsupportedVersion};
    std::string sha256;
    std::string file_version;
    std::string detail;
    const HalVersionInfo* matched_version{nullptr};

    bool IsSupported() const { return status == HalGateStatus::Supported; }
};

/**
 * @brief 获取集中管理的已验证 ASUS HAL 版本列表。
 * @note 严格遵守最小暴露原则：只记录当前仓库实际验证过的真实 DLL。
 */
const std::vector<HalVersionInfo>& GetVerifiedHalVersions();

/**
 * @brief 计算内存数据的 SHA-256 (返回 64 字节小写十六进制文本)
 */
std::string ComputeSha256(const uint8_t* data, size_t size);

/**
 * @brief 计算磁盘文件的 SHA-256 (返回 64 字节小写十六进制文本)
 */
std::string ComputeFileSha256(const std::wstring& file_path);

/**
 * @brief 读取 PE DLL 文件的 FileVersion 字符串 (如 "1.3.46.0")
 */
std::string ReadFileVersionString(const std::wstring& file_path);

/**
 * @brief 阶段 1 Gate：校验磁盘文件 SHA-256 与版本号是否匹配受支持表
 */
HalGateResult ValidateHalFileGate(const std::wstring& file_path);

/**
 * @brief 阶段 2 Gate：校验已加载内存模块的关键 runtime signature
 */
HalGateResult ValidateHalModuleGate(HMODULE hMod, const HalVersionInfo* matched_version);

/**
 * @brief 生成符合安全规约的标准化 fail-closed 告警提示信息
 */
std::string FormatHalGateError(const HalGateResult& result);

} // namespace aura
