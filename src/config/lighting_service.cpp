#include "config/lighting_service.h"
#include "utils/logger.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace aura {


// ========================================================
// 64-bit FNV-1a 哈希与版本计算
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
// 动画周期共享元数据
// ========================================================
bool IsPeriodSupported(const std::string& effect_type) {
    static const std::unordered_set<std::string> kPeriodEffects = {
        "breathing",
        "color_cycle",
        "wave",
        "reactive",
        "ripple",
        "starry_night",
        "quicksand",
        "current",
        "raindrop"
    };
    return kPeriodEffects.find(effect_type) != kPeriodEffects.end();
}

uint64_t GetEffectDefaultPeriod(const std::string& effect_type) {
    if (effect_type == "breathing") return 3000;
    if (effect_type == "color_cycle") return 3500;
    if (effect_type == "wave") return 3500;
    if (effect_type == "quicksand") return 3500;
    if (effect_type == "reactive") return 2500;
    if (effect_type == "ripple") return 2500;
    if (effect_type == "starry_night") return 2500;
    if (effect_type == "raindrop") return 2500;
    if (effect_type == "current") return 2000;
    return 2500;
}

uint64_t ResolveEffectivePeriodMs(const std::string& effect_type, const nlohmann::json& pval, const std::string& /*pname*/) {
    if (!IsPeriodSupported(effect_type)) {
        return 0;
    }
    uint64_t def_period = GetEffectDefaultPeriod(effect_type);

    if (pval.contains("period_ms")) {
        const auto& period_val = pval["period_ms"];
        if (period_val.is_number_unsigned()) {
            uint64_t u = period_val.get<uint64_t>();
            if (u < 33) {
                return def_period < 33 ? 33 : def_period;
            }
            return u;
        } else if (period_val.is_number_integer()) {
            int64_t s = period_val.get<int64_t>();
            if (s >= 33) {
                return static_cast<uint64_t>(s);
            }
            return def_period < 33 ? 33 : def_period;
        }
        return def_period < 33 ? 33 : def_period;
    }

    if (pval.contains("speed_index")) {
        const auto& sval = pval["speed_index"];
        if (sval.is_number()) {
            double sv = sval.get<double>();
            if (std::isfinite(sv)) {
                int s = static_cast<int>(std::round(sv));
                if (s == 0) return 5500;
                if (s == 1) return 3200;
                if (s == 2) return 1600;
            }
        }
    }

    return def_period;
}

// ========================================================
// 亮度规范化
// ========================================================
double NormalizeBrightnessToRatio(const nlohmann::json& pval) {
    if (!pval.contains("brightness")) {
        return 1.0;
    }
    const auto& bval = pval["brightness"];
    if (!bval.is_number()) {
        return 1.0;
    }
    double v = bval.get<double>();
    if (!std::isfinite(v)) {
        return 1.0;
    }
    if (v <= 0.0) {
        return 0.0;
    }
    if (v <= 1.0001) {
        return std::clamp(v, 0.0, 1.0);
    }
    return std::clamp(v / 255.0, 0.0, 1.0);
}

// ========================================================
// LightingControlService 实现
// ========================================================
LightingControlService::LightingControlService(std::filesystem::path config_path)
    : config_path_(std::move(config_path)) {
    // 默认使用 Windows 原生原子替换 MoveFileExW
    file_replacer_ = [](const std::wstring& tmp, const std::wstring& target) {
        return MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    };
}

bool LightingControlService::ReadRawConfigFile(std::string& out_content, std::string& out_revision) const {
    std::ifstream file(config_path_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    out_content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out_revision = ComputeFileRevision(out_content);
    return true;
}

LightingControlService::OpResult LightingControlService::GetProfileList(
    std::vector<ProfileSummary>& out_profiles, std::string& out_revision) {
    std::string content;
    if (!ReadRawConfigFile(content, out_revision)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Internal Error", "Configuration root missing 'profiles' object", ""};
        }

        for (auto& [pname, pval] : root["profiles"].items()) {
            if (pval.is_object()) {
                ProfileSummary ps;
                ps.name = pname;
                ps.type = pval.value("type", "static");
                out_profiles.push_back(std::move(ps));
            }
        }
        return {200, "", "", out_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("JSON parsing error: ") + e.what(), ""};
    }
}

LightingControlService::OpResult LightingControlService::GetProfileDetail(
    const std::string& name, ProfileDetail& out_detail) {
    std::string content;
    std::string rev;
    if (!ReadRawConfigFile(content, rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Internal Error", "Configuration root missing 'profiles' object", ""};
        }

        if (!root["profiles"].contains(name) || !root["profiles"][name].is_object()) {
            return {404, "Not Found", "Profile not found: " + name, rev};
        }

        const auto& pval = root["profiles"][name];
        ProfileDetail d;
        d.name = name;
        d.type = pval.value("type", "static");
        d.brightness = NormalizeBrightnessToRatio(pval);

        int root_fps = 25;
        if (root.contains("fps") && root["fps"].is_number()) {
            root_fps = std::clamp(root["fps"].get<int>(), 10, 100);
        }

        if (pval.contains("fps") && pval["fps"].is_number()) {
            d.fps = std::clamp(pval["fps"].get<int>(), 10, 100);
            d.fps_inherited = false;
        } else {
            d.fps = root_fps;
            d.fps_inherited = true;
        }

        d.supports_period = IsPeriodSupported(d.type);
        if (d.supports_period) {
            d.has_period = true;
            d.period_ms = ResolveEffectivePeriodMs(d.type, pval, name);
        } else {
            d.has_period = false;
            d.period_ms = 0;
        }

        d.revision = rev;
        out_detail = d;
        return {200, "", "", rev};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("JSON parsing error: ") + e.what(), ""};
    }
}

LightingControlService::OpResult LightingControlService::UpdateProfile(
    const std::string& name, const PatchInput& patch, std::string& out_new_revision) {
    if (patch.expected_revision.empty()) {
        return {400, "Validation Error", "Missing required field 'expected_revision'", ""};
    }

    // 1. 获取跨进程 Windows Named Mutex 互斥锁
    NamedConfigLock lock(L"Local\\AceHFXAuraConfigWriteMutex", 5000);
    if (!lock.IsAcquired()) {
        return {500, "Internal Error", "Failed to acquire cross-process configuration lock", ""};
    }

    // 2. 锁内重新读取最新文件内容与版本
    std::string content;
    std::string current_rev;
    if (!ReadRawConfigFile(content, current_rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    // 3. 版本指纹比对 (FNV-1a 64-bit 严格一致性，防止竞态冲突与静默覆盖)
    if (current_rev != patch.expected_revision) {
        return {409, "Conflict", "Configuration has been modified externally", current_rev};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Internal Error", "Configuration root missing 'profiles' object", current_rev};
        }

        if (!root["profiles"].contains(name) || !root["profiles"][name].is_object()) {
            return {404, "Not Found", "Profile not found: " + name, current_rev};
        }

        auto& prof = root["profiles"][name];
        std::string effect_type = prof.value("type", "static");
        bool supports_period = IsPeriodSupported(effect_type);

        // 4. 执行真正 Sparse Patch：仅修改显式提供的字段，保留其余所有既有字段
        if (patch.has_brightness) {
            if (!std::isfinite(patch.brightness) || patch.brightness < 0.0 || patch.brightness > 1.0) {
                return {400, "Validation Error", "Field 'brightness' must be a valid number in [0.0, 1.0]", current_rev};
            }
            prof["brightness"] = patch.brightness;
        }

        if (patch.has_fps) {
            if (patch.fps < 10 || patch.fps > 100) {
                return {400, "Validation Error", "Field 'fps' must be an integer between 10 and 100", current_rev};
            }
            prof["fps"] = patch.fps;
        }

        if (patch.has_period_ms) {
            if (!supports_period) {
                return {400, "Validation Error", "Effect type '" + effect_type + "' does not support 'period_ms'", current_rev};
            }
            if (patch.period_ms < 33) {
                return {400, "Validation Error", "Field 'period_ms' must be at least 33ms", current_rev};
            }
            prof["period_ms"] = patch.period_ms;
        }

        // 5. 格式化输出 (保持 2 格缩进)
        std::string formatted = root.dump(2);

        // 6. 写入临时文件
        std::filesystem::path tmp_path = config_path_.wstring() + L".tmp";
        {
            std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) {
                return {500, "Internal Error", "Failed to create temporary configuration file", current_rev};
            }
            f.write(formatted.data(), formatted.size());
            f.flush();
            if (!f.good()) {
                f.close();
                std::error_code ec;
                std::filesystem::remove(tmp_path, ec);
                return {500, "Internal Error", "Failed to write temporary configuration data", current_rev};
            }
        }

        // 7. 原子替换覆盖 (通过 file_replacer_ 支持测试接缝)
        if (!file_replacer_(tmp_path.wstring(), config_path_.wstring())) {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return {500, "Internal Error", "Failed to atomically replace configuration file", current_rev};
        }

        out_new_revision = ComputeFileRevision(formatted);
        return {200, "", "Profile updated", out_new_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("Failed to mutate configuration: ") + e.what(), current_rev};
    }
}

// ========================================================
// HTTP 安全防御检查 (127.0.0.1:19897 写接口防御)
// ========================================================
bool LightingControlService::ValidatePatchRequestSecurity(const httplib::Request& req, httplib::Response& res) {
    // 1. Host 校验：必须为环回地址
    std::string host = req.get_header_value("Host");
    bool host_ok = (host == "127.0.0.1" || host == "localhost" || host == "[::1]" ||
                    host.rfind("127.0.0.1:", 0) == 0 ||
                    host.rfind("localhost:", 0) == 0 ||
                    host.rfind("[::1]:", 0) == 0);
    if (!host_ok) {
        res.status = 403;
        res.set_content(R"json({"status":"error","error":"Forbidden","message":"Host must be loopback"})json", "application/json; charset=utf-8");
        return false;
    }

    // 2. Content-Type 校验：必须为 application/json
    std::string ct = req.get_header_value("Content-Type");
    if (ct.find("application/json") == std::string::npos) {
        res.status = 415;
        res.set_content(R"json({"status":"error","error":"Unsupported Media Type","message":"Content-Type must be application/json"})json", "application/json; charset=utf-8");
        return false;
    }

    // 3. Origin / Referer 校验：防跨站伪造
    // WinUI 原生 HttpClient 默认无 Origin，允许通过；若存在必须为合法环回地址
    std::string origin = req.get_header_value("Origin");
    if (!origin.empty()) {
        bool origin_ok = (origin.rfind("http://127.0.0.1", 0) == 0 || origin.rfind("http://localhost", 0) == 0);
        if (!origin_ok) {
            res.status = 403;
            res.set_content(R"json({"status":"error","error":"Forbidden","message":"Untrusted Origin rejected"})json", "application/json; charset=utf-8");
            return false;
        }
    }

    std::string referer = req.get_header_value("Referer");
    if (!referer.empty()) {
        bool referer_ok = (referer.rfind("http://127.0.0.1", 0) == 0 || referer.rfind("http://localhost", 0) == 0);
        if (!referer_ok) {
            res.status = 403;
            res.set_content(R"json({"status":"error","error":"Forbidden","message":"Untrusted Referer rejected"})json", "application/json; charset=utf-8");
            return false;
        }
    }

    return true;
}

void LightingControlService::RegisterRoutes(httplib::Server& svr) {
    // 1. GET /api/lighting/profiles - 获取轻量 Profile 列表
    svr.Get("/api/lighting/profiles", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<ProfileSummary> profiles;
        std::string revision;
        auto op = GetProfileList(profiles, revision);
        if (op.http_status != 200) {
            res.status = op.http_status;
            nlohmann::json err = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["revision"] = revision;
        j["profiles"] = nlohmann::json::array();
        for (const auto& p : profiles) {
            j["profiles"].push_back({
                {"name", p.name},
                {"type", p.type}
            });
        }
        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 2. GET /api/lighting/profiles/:name - 获取指定方案详情
    svr.Get(R"(/api/lighting/profiles/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        ProfileDetail detail;
        auto op = GetProfileDetail(name, detail);
        if (op.http_status != 200) {
            res.status = op.http_status;
            nlohmann::json err = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            if (!op.current_revision.empty()) {
                err["current_revision"] = op.current_revision;
            }
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["revision"] = detail.revision;
        j["profile"] = {
            {"name", detail.name},
            {"type", detail.type},
            {"brightness", detail.brightness},
            {"fps", detail.fps},
            {"fps_inherited", detail.fps_inherited},
            {"supports_period", detail.supports_period}
        };
        if (detail.supports_period) {
            j["profile"]["period_ms"] = detail.period_ms;
        } else {
            j["profile"]["period_ms"] = nullptr;
        }

        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 3. PATCH /api/lighting/profiles/:name - 严格白名单与原子修改
    svr.Patch(R"(/api/lighting/profiles/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidatePatchRequestSecurity(req, res)) {
            return;
        }

        std::string name = req.matches[1];
        PatchInput patch;

        try {
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Request body must be a JSON object"})json", "application/json; charset=utf-8");
                return;
            }

            // 严格白名单字段校验：禁止修改除 expected_revision, brightness, period_ms, fps 以外的任何字段
            static const std::unordered_set<std::string> kAllowedKeys = {
                "expected_revision", "brightness", "period_ms", "fps"
            };
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (kAllowedKeys.find(it.key()) == kAllowedKeys.end()) {
                    res.status = 400;
                    nlohmann::json err = {
                        {"status", "error"},
                        {"error", "Validation Error"},
                        {"message", "Modifying field '" + it.key() + "' is not permitted via Lighting API v1"}
                    };
                    res.set_content(err.dump(), "application/json; charset=utf-8");
                    return;
                }
            }

            if (!j.contains("expected_revision") || !j["expected_revision"].is_string()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required string field 'expected_revision'"})json", "application/json; charset=utf-8");
                return;
            }
            patch.expected_revision = j["expected_revision"].get<std::string>();

            if (j.contains("brightness")) {
                if (!j["brightness"].is_number()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'brightness' must be a numeric ratio in [0.0, 1.0]"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_brightness = true;
                patch.brightness = j["brightness"].get<double>();
            }

            if (j.contains("fps")) {
                if (!j["fps"].is_number_integer()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'fps' must be an integer between 10 and 100"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_fps = true;
                patch.fps = j["fps"].get<int>();
            }

            if (j.contains("period_ms")) {
                if (!j["period_ms"].is_number_unsigned()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'period_ms' must be an unsigned integer >= 33"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_period_ms = true;
                patch.period_ms = j["period_ms"].get<uint64_t>();
            }
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"error", "Validation Error"},
                {"message", std::string("Malformed JSON payload: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        std::string new_rev;
        auto op = UpdateProfile(name, patch, new_rev);
        res.status = op.http_status;
        if (op.http_status == 200) {
            nlohmann::json ok_j = {
                {"status", "ok"},
                {"api_version", 1},
                {"message", op.message},
                {"revision", new_rev}
            };
            res.set_content(ok_j.dump(), "application/json; charset=utf-8");
        } else {
            nlohmann::json err_j = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            if (!op.current_revision.empty()) {
                err_j["current_revision"] = op.current_revision;
            }
            res.set_content(err_j.dump(), "application/json; charset=utf-8");
        }
    });
}

} // namespace aura
