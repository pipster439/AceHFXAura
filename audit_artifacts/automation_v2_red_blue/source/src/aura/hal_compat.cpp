#include "aura/hal_compat.h"
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

namespace aura {

const std::vector<HalVersionInfo>& GetVerifiedHalVersions() {
    // 集中管理已验证的 ASUS HAL 驱动版本。
    // 严格遵循真实证据原则：当前仓库仅对 ASUS ROG Falchion Ace HFX 原厂配套的
    // AacKbHal_x64.dll (v1.3.46.0) 进行了硬件反汇编与接口实测验证。
    // 任何未识别版本或篡改散列必须 fail closed，严禁假装支持更广版本。
    static const std::vector<HalVersionInfo> s_versions = {
        {
            "52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04",
            "1.3.46.0",
            0x7ABE0,    // RVA Logger::Log
            0x1CB85C,   // RVA EnableLog
            { 0x40, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xEF, 0xFF }
        }
    };
    return s_versions;
}

std::string ComputeSha256(const uint8_t* data, size_t size) {
    if (!data && size > 0) return "";

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    if (size > 0 && !CryptHashData(hHash, data, static_cast<DWORD>(size), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    BYTE hash[32];
    DWORD hash_len = sizeof(hash);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hash_len, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    std::ostringstream oss;
    for (int i = 0; i < 32; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string ComputeFileSha256(const std::wstring& file_path) {
    HANDLE hFile = CreateFileW(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return "";
    }

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        return "";
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return "";
    }

    constexpr DWORD BUF_SIZE = 64 * 1024;
    std::vector<BYTE> buffer(BUF_SIZE);
    DWORD bytesRead = 0;
    bool success = true;

    while (ReadFile(hFile, buffer.data(), BUF_SIZE, &bytesRead, nullptr) && bytesRead > 0) {
        if (!CryptHashData(hHash, buffer.data(), bytesRead, 0)) {
            success = false;
            break;
        }
    }
    CloseHandle(hFile);

    if (!success) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    BYTE hash[32];
    DWORD hash_len = sizeof(hash);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hash_len, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    std::ostringstream oss;
    for (int i = 0; i < 32; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string ReadFileVersionString(const std::wstring& file_path) {
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(file_path.c_str(), &dummy);
    if (size == 0) return "";

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(file_path.c_str(), 0, size, data.data())) return "";

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT len = 0;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&pFileInfo), &len) && pFileInfo) {
        WORD major = HIWORD(pFileInfo->dwFileVersionMS);
        WORD minor = LOWORD(pFileInfo->dwFileVersionMS);
        WORD build = HIWORD(pFileInfo->dwFileVersionLS);
        WORD rev   = LOWORD(pFileInfo->dwFileVersionLS);
        return std::to_string(major) + "." + std::to_string(minor) + "." +
               std::to_string(build) + "." + std::to_string(rev);
    }
    return "";
}

HalGateResult ValidateHalFileGate(const std::wstring& file_path) {
    HalGateResult res;
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        res.status = HalGateStatus::FileNotFound;
        res.detail = "DLL 文件不存在";
        return res;
    }

    res.sha256 = ComputeFileSha256(file_path);
    if (res.sha256.empty()) {
        res.status = HalGateStatus::ReadError;
        res.detail = "无法计算 DLL 散列";
        return res;
    }
    res.file_version = ReadFileVersionString(file_path);

    const auto& verified = GetVerifiedHalVersions();
    for (const auto& v : verified) {
        if (_stricmp(res.sha256.c_str(), v.sha256.c_str()) == 0) {
            res.status = HalGateStatus::Supported;
            res.matched_version = &v;
            return res;
        }
    }

    res.status = HalGateStatus::UnsupportedVersion;
    res.detail = "DLL SHA-256 (" + res.sha256 + ") 未在受支持的已验证版本表中";
    return res;
}

static bool ReadModuleBytesSafe(const void* src, void* dst, size_t size) {
    if (!src || !dst || size == 0) return false;
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

HalGateResult ValidateHalModuleGate(HMODULE hMod, const HalVersionInfo* matched_version) {
    HalGateResult res;
    if (!hMod || !matched_version) {
        res.status = HalGateStatus::ReadError;
        res.detail = "模块句柄或匹配版本为空";
        return res;
    }

    res.sha256 = matched_version->sha256;
    res.file_version = matched_version->file_version;

    const uint8_t* base = reinterpret_cast<const uint8_t*>(hMod);

    IMAGE_DOS_HEADER dos{};
    if (!ReadModuleBytesSafe(base, &dos, sizeof(dos))) {
        res.status = HalGateStatus::ReadError;
        res.detail = "读取 PE DOS 头发生访问违规异常";
        return res;
    }

    if (dos.e_magic != IMAGE_DOS_SIGNATURE) {
        res.status = HalGateStatus::SignatureMismatch;
        res.detail = "PE DOS 头签名无效 (非 MZ)";
        return res;
    }

    if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000000) {
        res.status = HalGateStatus::SignatureMismatch;
        res.detail = "PE NT 头偏移 (e_lfanew) 异常越界";
        return res;
    }

    IMAGE_NT_HEADERS nt{};
    if (!ReadModuleBytesSafe(base + dos.e_lfanew, &nt, sizeof(nt))) {
        res.status = HalGateStatus::SignatureMismatch;
        res.detail = "读取 PE NT 头发生内存越界异常";
        return res;
    }

    if (nt.Signature != IMAGE_NT_SIGNATURE) {
        res.status = HalGateStatus::SignatureMismatch;
        res.detail = "PE NT 头签名无效 (非 PE00)";
        return res;
    }

    // 校验关键 runtime signature：检查 Logger::Log 入口处的指令序言
    const auto& expected_sig = matched_version->logger_prologue;
    if (nt.OptionalHeader.SizeOfImage > 0 &&
        matched_version->rva_logger + expected_sig.size() > nt.OptionalHeader.SizeOfImage) {
        res.status = HalGateStatus::SignatureMismatch;
        res.detail = "Logger::Log RVA 超出 PE 镜像内存边界";
        return res;
    }

    std::vector<uint8_t> actual_code(expected_sig.size(), 0);
    if (!ReadModuleBytesSafe(base + matched_version->rva_logger, actual_code.data(), actual_code.size())) {
        res.status = HalGateStatus::SignatureMismatch;
        res.detail = "读取关键 runtime signature 内存发生越界异常";
        return res;
    }

    // 允许原版函数序言 (40 55 57 41...) 或已应用防崩补丁的 0xC3 (ret)
    bool is_clean_prologue = (memcmp(actual_code.data(), expected_sig.data(), expected_sig.size()) == 0);
    bool is_already_patched = (actual_code[0] == 0xC3);

    if (!is_clean_prologue && !is_already_patched) {
        res.status = HalGateStatus::SignatureMismatch;
        std::ostringstream rva_hex;
        rva_hex << "0x" << std::hex << std::uppercase << matched_version->rva_logger;
        res.detail = "关键 runtime signature (Logger::Log RVA " + rva_hex.str() + ") 校验不匹配";
        return res;
    }

    res.status = HalGateStatus::Supported;
    res.matched_version = matched_version;
    return res;
}

std::string FormatHalGateError(const HalGateResult& result) {
    std::ostringstream oss;
    oss << "Unsupported ASUS HAL version\n"
        << "SHA-256: " << (result.sha256.empty() ? "(unknown)" : result.sha256) << "\n"
        << "FileVersion: " << (result.file_version.empty() ? "(unknown)" : result.file_version) << "\n"
        << "Hardware control disabled for safety.\n"
        << "Reason: " << result.detail;
    return oss.str();
}

} // namespace aura
