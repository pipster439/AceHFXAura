#include "config/automation_contract.h"
#include "config/config_writer_util.h"
#include <fstream>
#include <cstdio>
#include <cctype>

namespace aura {

// ========================================================
// NamedConfigLock 实现
// ========================================================
NamedConfigLock::NamedConfigLock(const wchar_t* name, DWORD timeout_ms) {
    hMutex_ = CreateMutexW(nullptr, FALSE, name);
    if (hMutex_) {
        DWORD wait_res = WaitForSingleObject(hMutex_, timeout_ms);
        acquired_ = (wait_res == WAIT_OBJECT_0 || wait_res == WAIT_ABANDONED);
    }
}

NamedConfigLock::~NamedConfigLock() {
    if (acquired_ && hMutex_) {
        ReleaseMutex(hMutex_);
    }
    if (hMutex_) {
        CloseHandle(hMutex_);
    }
}

// ========================================================
// 64-bit FNV-1a 内容哈希与版本计算
// ========================================================
uint64_t ComputeFnv1a64(const void* data, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ULL; // FNV_offset_basis
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(ptr[i]);
        hash *= 1099511628211ULL; // FNV_prime
    }
    return hash;
}

std::string FormatFnv1aHex(uint64_t hash) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

std::string ComputeFileRevision(const std::string& content) {
    uint64_t h = ComputeFnv1a64(content.data(), content.size());
    return FormatFnv1aHex(h);
}

// ========================================================
// 原子文件读取与替换工具
// ========================================================
bool DefaultFileReplacer(const std::wstring& tmp_path, const std::wstring& target_path) {
    return MoveFileExW(tmp_path.c_str(), target_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool ReadRawConfigFile(const std::filesystem::path& config_path,
                       std::string& out_content,
                       std::string& out_revision) {
    std::ifstream file(config_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    out_content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out_revision = ComputeFileRevision(out_content);
    return true;
}

bool AtomicWriteConfigFile(const std::filesystem::path& config_path,
                           const std::string& content,
                           FileReplacerFunc replacer) {
    ValidateAutomationContainers(nlohmann::json::parse(content));
    std::filesystem::path tmp_path = config_path.wstring() + L".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            return false;
        }
        f.write(content.data(), content.size());
        f.flush();
        if (!f.good()) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
    }

    auto actual_replacer = replacer ? replacer : DefaultFileReplacer;
    if (!actual_replacer(tmp_path.wstring(), config_path.wstring())) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    return true;
}

// ========================================================
// HTTP 安全与格式校验工具实现
// ========================================================
bool IsAllowedLoopbackHost(const std::string& host_header) {
    if (host_header.empty()) {
        return false;
    }
    size_t first = host_header.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = host_header.find_last_not_of(" \t\r\n");
    std::string trimmed = host_header.substr(first, last - first + 1);

    std::string host_part;
    if (trimmed.front() == '[') {
        size_t close_bracket = trimmed.find(']');
        if (close_bracket == std::string::npos) return false;
        host_part = trimmed.substr(0, close_bracket + 1);
        std::string rest = trimmed.substr(close_bracket + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return false;
            std::string port_part = rest.substr(1);
            if (port_part.empty()) return false;
            for (char c : port_part) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
        }
    } else {
        size_t colon_pos = trimmed.find(':');
        if (colon_pos != std::string::npos) {
            host_part = trimmed.substr(0, colon_pos);
            std::string port_part = trimmed.substr(colon_pos + 1);
            if (port_part.empty()) return false;
            for (char c : port_part) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
        } else {
            host_part = trimmed;
        }
    }

    for (char& c : host_part) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return (host_part == "127.0.0.1" || host_part == "localhost" || host_part == "[::1]");
}

bool IsJsonContentType(const std::string& content_type) {
    if (content_type.empty()) {
        return false;
    }
    size_t semicolon = content_type.find(';');
    std::string media_type = (semicolon != std::string::npos) ? content_type.substr(0, semicolon) : content_type;

    size_t first = media_type.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = media_type.find_last_not_of(" \t\r\n");
    media_type = media_type.substr(first, last - first + 1);

    for (char& c : media_type) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return media_type == "application/json";
}

bool IsAllowedOriginOrRefererUrl(const std::string& raw_url) {
    size_t first = raw_url.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = raw_url.find_last_not_of(" \t\r\n");
    std::string url = raw_url.substr(first, last - first + 1);

    if (url.empty() || url == "null") {
        return false;
    }
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }

    std::string scheme = url.substr(0, scheme_end);
    for (char& c : scheme) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (scheme != "http" && scheme != "https") {
        return false;
    }

    size_t host_start = scheme_end + 3;
    size_t auth_end = url.find_first_of("/?#", host_start);
    std::string authority = (auth_end == std::string::npos)
        ? url.substr(host_start)
        : url.substr(host_start, auth_end - host_start);

    if (authority.empty() || authority.find('@') != std::string::npos) {
        return false;
    }
    return IsAllowedLoopbackHost(authority);
}

bool ValidateLoopbackOriginAndReferer(const std::string& origin, const std::string& referer) {
    if (!origin.empty()) {
        if (origin == "null" || !IsAllowedOriginOrRefererUrl(origin)) {
            return false;
        }
    }
    if (!referer.empty()) {
        if (!IsAllowedOriginOrRefererUrl(referer)) {
            return false;
        }
    }
    return true;
}

} // namespace aura
