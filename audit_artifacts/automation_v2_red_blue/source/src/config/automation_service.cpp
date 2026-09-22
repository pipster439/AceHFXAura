#include "config/automation_service.h"
#include "utils/logger.h"

#include <algorithm>
#include <cctype>

namespace aura {

// ========================================================
// 进程名规范化工具函数
// 规则：Trim -> 获取文件名 -> 小写 -> 若无后缀补齐 .exe
// ========================================================
std::string CanonicalizeProcessName(const std::string& input) {
    if (input.empty()) return "";

    // 1. Trim leading / trailing whitespaces
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    if (start >= input.size()) return "";

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    std::string trimmed = input.substr(start, end - start);

    // 2. 获取文件名 (剥离可能存在的目录路径)
    std::string filename;
    try {
        filename = std::filesystem::path(trimmed).filename().string();
    } catch (...) {
        filename = trimmed;
    }
    if (filename.empty()) return "";

    // 3. 转为小写 ASCII
    for (char& c : filename) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // 4. 检查扩展名：
    // - 无扩展名 -> 自动追加 .exe
    // - .exe 扩展名 -> 保持小写
    // - 其他扩展名 (如 .dll, .bat 等) -> 拒绝 (返回空字符串)
    static const std::string kExeSuffix = ".exe";
    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos) {
        filename += kExeSuffix;
    } else {
        std::string ext = filename.substr(dot_pos);
        if (ext != kExeSuffix) {
            return "";
        }
    }

    // 5. 校验：不能仅为 ".exe"
    if (filename == kExeSuffix) {
        return "";
    }

    return filename;
}

// ========================================================
// AutomationControlService 实现
// ========================================================
AutomationControlService::AutomationControlService(std::filesystem::path config_path)
    : config_path_(std::move(config_path)) {
}

bool AutomationControlService::ValidateWriteRequestSecurity(const httplib::Request& req, httplib::Response& res) {
    // 1. Host 校验：必须为环回地址
    if (!IsAllowedLoopbackHost(req.get_header_value("Host"))) {
        res.status = 403;
        res.set_content(R"json({"status":"error","error":"Forbidden","message":"Host header rejected"})json", "application/json; charset=utf-8");
        return false;
    }

    // 2. 强制 Content-Type: application/json
    if (!IsJsonContentType(req.get_header_value("Content-Type"))) {
        res.status = 415;
        res.set_content(R"json({"status":"error","error":"Unsupported Media Type","message":"Content-Type must be application/json"})json", "application/json; charset=utf-8");
        return false;
    }

    // 3. Origin / Referer 校验：防跨站伪造 (允许无 Origin/Referer 的原生 WinUI 客户端)
    std::string origin = req.get_header_value("Origin");
    std::string referer = req.get_header_value("Referer");
    if (!ValidateLoopbackOriginAndReferer(origin, referer)) {
        res.status = 403;
        res.set_content(R"json({"status":"error","error":"Forbidden","message":"Untrusted Origin or Referer rejected"})json", "application/json; charset=utf-8");
        return false;
    }

    return true;
}

AutomationControlService::OpResult AutomationControlService::GetRules(
    std::vector<AutomationRule>& out_rules, std::string& out_revision) {
    std::string content;
    if (!ReadRawConfigFile(config_path_, content, out_revision)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("rules") || !root["rules"].is_array()) {
            return {500, "Internal Error", "Configuration root missing 'rules' array", ""};
        }

        for (size_t i = 0; i < root["rules"].size(); ++i) {
            const auto& item = root["rules"][i];
            if (item.is_object() && item.contains("process") && item["process"].is_string()) {
                AutomationRule r;
                r.index = static_cast<int>(i);
                r.process = item["process"].get<std::string>();
                r.profile = item.value("profile", "");
                out_rules.push_back(std::move(r));
            }
        }
        return {200, "", "", out_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("JSON parsing error: ") + e.what(), ""};
    }
}

AutomationControlService::OpResult AutomationControlService::AddRule(
    const std::string& expected_revision,
    const AutomationRule& new_rule,
    AutomationRule& out_canonical_rule,
    int& out_index,
    std::string& out_new_revision) {
    if (expected_revision.empty()) {
        return {400, "Validation Error", "Missing required field 'expected_revision'", ""};
    }

    std::string can_proc = CanonicalizeProcessName(new_rule.process);
    if (can_proc.empty()) {
        return {400, "Validation Error", "Invalid or empty process name", ""};
    }

    if (new_rule.profile.empty()) {
        return {400, "Validation Error", "Missing required field 'profile'", ""};
    }

    // 1. 获取跨进程 Windows Named Mutex 互斥锁
    NamedConfigLock lock(kConfigWriteMutexName, 5000);
    if (!lock.IsAcquired()) {
        return {500, "Internal Error", "Failed to acquire cross-process configuration lock", ""};
    }

    // 2. 锁内重新读取最新文件内容与版本
    std::string content;
    std::string current_rev;
    if (!ReadRawConfigFile(config_path_, content, current_rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    // 3. 版本指纹比对 (乐观并发冲突检查)
    if (current_rev != expected_revision) {
        return {409, "revision_conflict", "Configuration has been modified externally", current_rev};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object()) {
            return {500, "Internal Error", "Configuration root is not a JSON object", current_rev};
        }

        // 4. 校验目标 Profile 存在性
        if (!root.contains("profiles") || !root["profiles"].is_object() || !root["profiles"].contains(new_rule.profile)) {
            return {400, "profile_not_found", "Profile '" + new_rule.profile + "' not found in configuration", current_rev};
        }

        if (!root.contains("rules") || !root["rules"].is_array()) {
            root["rules"] = nlohmann::ordered_json::array();
        }

        // 5. 进程重复冲突检测 (duplicate_process)
        for (const auto& item : root["rules"]) {
            if (item.is_object() && item.contains("process") && item["process"].is_string()) {
                if (CanonicalizeProcessName(item["process"].get<std::string>()) == can_proc) {
                    return {409, "duplicate_process", "Rule for process '" + can_proc + "' already exists", current_rev};
                }
            }
        }

        // 6. 追加规则
        nlohmann::ordered_json rule_obj = {
            {"process", can_proc},
            {"profile", new_rule.profile}
        };
        root["rules"].push_back(rule_obj);
        out_index = static_cast<int>(root["rules"].size()) - 1;
        out_canonical_rule = {can_proc, new_rule.profile, out_index};

        // 7. 格式化并原子写入
        std::string formatted = root.dump(2);
        if (!AtomicWriteConfigFile(config_path_, formatted, file_replacer_)) {
            return {500, "Internal Error", "Failed to atomically replace configuration file", current_rev};
        }

        out_new_revision = ComputeFileRevision(formatted);
        return {201, "", "Rule added", out_new_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("Failed to mutate configuration: ") + e.what(), current_rev};
    }
}

AutomationControlService::OpResult AutomationControlService::UpdateRule(
    int index,
    const std::string& expected_revision,
    const std::optional<std::string>& new_process,
    const std::optional<std::string>& new_profile,
    AutomationRule& out_canonical_rule,
    std::string& out_new_revision) {
    if (expected_revision.empty()) {
        return {400, "Validation Error", "Missing required field 'expected_revision'", ""};
    }

    if (!new_process.has_value() && !new_profile.has_value()) {
        return {400, "Validation Error", "Must specify 'process' or 'profile' to update", ""};
    }

    std::string can_proc;
    if (new_process.has_value()) {
        can_proc = CanonicalizeProcessName(*new_process);
        if (can_proc.empty()) {
            return {400, "Validation Error", "Invalid or empty process name", ""};
        }
    }

    if (new_profile.has_value() && new_profile->empty()) {
        return {400, "Validation Error", "Field 'profile' cannot be empty", ""};
    }

    // 1. 获取跨进程 Windows Named Mutex 互斥锁
    NamedConfigLock lock(kConfigWriteMutexName, 5000);
    if (!lock.IsAcquired()) {
        return {500, "Internal Error", "Failed to acquire cross-process configuration lock", ""};
    }

    // 2. 锁内重新读取最新文件内容与版本
    std::string content;
    std::string current_rev;
    if (!ReadRawConfigFile(config_path_, content, current_rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    // 3. 版本指纹比对
    if (current_rev != expected_revision) {
        return {409, "revision_conflict", "Configuration has been modified externally", current_rev};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("rules") || !root["rules"].is_array()) {
            return {404, "rule_not_found", "No rules found in configuration", current_rev};
        }

        if (index < 0 || index >= static_cast<int>(root["rules"].size())) {
            return {404, "rule_not_found", "Rule index " + std::to_string(index) + " out of bounds", current_rev};
        }

        auto& target_rule = root["rules"][index];
        if (!target_rule.is_object()) {
            return {500, "Internal Error", "Rule entry is malformed", current_rev};
        }

        // 4. 若修改 profile，校验其存在性
        if (new_profile.has_value()) {
            if (!root.contains("profiles") || !root["profiles"].is_object() || !root["profiles"].contains(*new_profile)) {
                return {400, "profile_not_found", "Profile '" + *new_profile + "' not found in configuration", current_rev};
            }
        }

        // 5. 若修改 process，检测与其他规则的冲突 (排除自身)
        if (new_process.has_value()) {
            for (size_t i = 0; i < root["rules"].size(); ++i) {
                if (static_cast<int>(i) == index) continue;
                const auto& item = root["rules"][i];
                if (item.is_object() && item.contains("process") && item["process"].is_string()) {
                    if (CanonicalizeProcessName(item["process"].get<std::string>()) == can_proc) {
                        return {409, "duplicate_process", "Rule for process '" + can_proc + "' already exists", current_rev};
                    }
                }
            }
        }

        // 6. 稀疏就地修改目标条目 (保留条目内部可能存在的既有其他字段)
        if (new_process.has_value()) {
            target_rule["process"] = can_proc;
        }
        if (new_profile.has_value()) {
            target_rule["profile"] = *new_profile;
        }

        out_canonical_rule.index = index;
        out_canonical_rule.process = target_rule.value("process", "");
        out_canonical_rule.profile = target_rule.value("profile", "");

        // 7. 格式化并原子替换
        std::string formatted = root.dump(2);
        if (!AtomicWriteConfigFile(config_path_, formatted, file_replacer_)) {
            return {500, "Internal Error", "Failed to atomically replace configuration file", current_rev};
        }

        out_new_revision = ComputeFileRevision(formatted);
        return {200, "", "Rule updated", out_new_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("Failed to mutate configuration: ") + e.what(), current_rev};
    }
}

AutomationControlService::OpResult AutomationControlService::DeleteRule(
    int index,
    const std::string& expected_revision,
    std::string& out_new_revision) {
    if (expected_revision.empty()) {
        return {400, "Validation Error", "Missing required field 'expected_revision'", ""};
    }

    // 1. 获取跨进程 Windows Named Mutex 互斥锁
    NamedConfigLock lock(kConfigWriteMutexName, 5000);
    if (!lock.IsAcquired()) {
        return {500, "Internal Error", "Failed to acquire cross-process configuration lock", ""};
    }

    // 2. 锁内重新读取最新文件内容与版本
    std::string content;
    std::string current_rev;
    if (!ReadRawConfigFile(config_path_, content, current_rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    // 3. 版本指纹比对
    if (current_rev != expected_revision) {
        return {409, "revision_conflict", "Configuration has been modified externally", current_rev};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("rules") || !root["rules"].is_array()) {
            return {404, "rule_not_found", "No rules found in configuration", current_rev};
        }

        if (index < 0 || index >= static_cast<int>(root["rules"].size())) {
            return {404, "rule_not_found", "Rule index " + std::to_string(index) + " out of bounds", current_rev};
        }

        // 4. 精确删除指定索引
        root["rules"].erase(root["rules"].begin() + index);

        // 5. 格式化并原子替换
        std::string formatted = root.dump(2);
        if (!AtomicWriteConfigFile(config_path_, formatted, file_replacer_)) {
            return {500, "Internal Error", "Failed to atomically replace configuration file", current_rev};
        }

        out_new_revision = ComputeFileRevision(formatted);
        return {200, "", "Rule deleted", out_new_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("Failed to mutate configuration: ") + e.what(), current_rev};
    }
}

void AutomationControlService::RegisterRoutes(httplib::Server& svr) {
    RegisterAuthoringRoutes(svr);
    // 1. GET /api/automation/rules - 获取应用规则列表
    svr.Get("/api/automation/rules", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<AutomationRule> rules;
        std::string revision;
        auto op = GetRules(rules, revision);
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
        j["rules"] = nlohmann::json::array();
        for (const auto& r : rules) {
            j["rules"].push_back({
                {"index", r.index},
                {"process", r.process},
                {"profile", r.profile}
            });
        }
        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 2. POST /api/automation/rules - 追加应用规则
    svr.Post("/api/automation/rules", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequestSecurity(req, res)) {
            return;
        }

        try {
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Request body must be a JSON object"})json", "application/json; charset=utf-8");
                return;
            }

            std::string exp_rev = j.value("expected_revision", "");
            if (exp_rev.empty()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required string field 'expected_revision'"})json", "application/json; charset=utf-8");
                return;
            }

            if (!j.contains("rule") || !j["rule"].is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required object field 'rule'"})json", "application/json; charset=utf-8");
                return;
            }

            const auto& r_obj = j["rule"];
            AutomationRule input_rule;
            input_rule.process = r_obj.value("process", "");
            input_rule.profile = r_obj.value("profile", "");

            AutomationRule canonical_rule;
            int new_index = -1;
            std::string new_rev;
            auto op = AddRule(exp_rev, input_rule, canonical_rule, new_index, new_rev);

            if (op.http_status != 201) {
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

            nlohmann::json resp;
            resp["status"] = "ok";
            resp["api_version"] = 1;
            resp["revision"] = new_rev;
            resp["index"] = new_index;
            resp["rule"] = {
                {"index", canonical_rule.index},
                {"process", canonical_rule.process},
                {"profile", canonical_rule.profile}
            };
            res.status = 201;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"error", "Validation Error"},
                {"message", std::string("JSON parsing error: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 3. PATCH /api/automation/rules/:index - 细粒度稀疏修改应用规则
    svr.Patch(R"(/api/automation/rules/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequestSecurity(req, res)) {
            return;
        }

        int index = -1;
        try {
            index = std::stoi(req.matches[1]);
        } catch (...) {
            res.status = 400;
            res.set_content(R"json({"status":"error","error":"Validation Error","message":"Invalid rule index in URL"})json", "application/json; charset=utf-8");
            return;
        }

        try {
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Request body must be a JSON object"})json", "application/json; charset=utf-8");
                return;
            }

            std::string exp_rev = j.value("expected_revision", "");
            if (exp_rev.empty()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required string field 'expected_revision'"})json", "application/json; charset=utf-8");
                return;
            }

            if (!j.contains("rule") || !j["rule"].is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required object field 'rule'"})json", "application/json; charset=utf-8");
                return;
            }

            const auto& r_obj = j["rule"];
            std::optional<std::string> new_process;
            std::optional<std::string> new_profile;

            if (r_obj.contains("process") && r_obj["process"].is_string()) {
                new_process = r_obj["process"].get<std::string>();
            }
            if (r_obj.contains("profile") && r_obj["profile"].is_string()) {
                new_profile = r_obj["profile"].get<std::string>();
            }

            AutomationRule canonical_rule;
            std::string new_rev;
            auto op = UpdateRule(index, exp_rev, new_process, new_profile, canonical_rule, new_rev);

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

            nlohmann::json resp;
            resp["status"] = "ok";
            resp["api_version"] = 1;
            resp["revision"] = new_rev;
            resp["index"] = index;
            resp["rule"] = {
                {"index", canonical_rule.index},
                {"process", canonical_rule.process},
                {"profile", canonical_rule.profile}
            };
            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"error", "Validation Error"},
                {"message", std::string("JSON parsing error: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 4. DELETE /api/automation/rules/:index - 精确索引删除应用规则
    svr.Delete(R"(/api/automation/rules/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequestSecurity(req, res)) {
            return;
        }

        int index = -1;
        try {
            index = std::stoi(req.matches[1]);
        } catch (...) {
            res.status = 400;
            res.set_content(R"json({"status":"error","error":"Validation Error","message":"Invalid rule index in URL"})json", "application/json; charset=utf-8");
            return;
        }

        try {
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Request body must be a JSON object"})json", "application/json; charset=utf-8");
                return;
            }

            std::string exp_rev = j.value("expected_revision", "");
            if (exp_rev.empty()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required string field 'expected_revision'"})json", "application/json; charset=utf-8");
                return;
            }

            std::string new_rev;
            auto op = DeleteRule(index, exp_rev, new_rev);

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

            nlohmann::json resp;
            resp["status"] = "ok";
            resp["api_version"] = 1;
            resp["revision"] = new_rev;
            resp["deleted_index"] = index;
            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"error", "Validation Error"},
                {"message", std::string("JSON parsing error: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });
}

} // namespace aura
