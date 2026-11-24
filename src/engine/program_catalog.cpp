#include "engine/program_catalog.h"
#include "engine/effect_program.h"
#include "config/config_writer_util.h"
#include "aura/hal_compat.h"
#include <fstream>
#include <regex>
#include <set>

namespace aura {
namespace {
using Json = nlohmann::json;
constexpr size_t MaxCatalogBytes = 2 * 1024 * 1024, MaxRequestBytes = 240 * 1024, MaxReceipts = 1024;
bool Id(const std::string& id) { return std::regex_match(id, std::regex("^[a-f0-9]{32}$")); }
std::string Hash(const std::string& text) {
    auto value = ComputeSha256(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    if (value.size() != 64) throw std::runtime_error("Hash unavailable");
    return value;
}
void SafePath(const std::filesystem::path& path) {
    for (auto cursor = std::filesystem::absolute(path); !cursor.empty();) {
        const auto attributes = GetFileAttributesW(cursor.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Program store paths cannot be links");
        const auto next = cursor.parent_path(); if (next == cursor) break; cursor = next;
    }
}
class LockedPath {
    std::vector<HANDLE> handles_;
public:
    explicit LockedPath(const std::filesystem::path& path) {
        try {
            const auto absolute=std::filesystem::absolute(path);auto cursor=absolute.root_path();
            for(const auto& component:absolute.relative_path()){
                cursor/=component;
                if(!std::filesystem::exists(cursor))break;
                HANDLE handle=CreateFileW(cursor.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot pin program store path");
                handles_.push_back(handle);BY_HANDLE_FILE_INFORMATION info{};
                if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Program store paths cannot be links");
            }
        }catch(...){for(auto handle:handles_)CloseHandle(handle);throw;}
    }
    ~LockedPath(){for(auto handle:handles_)CloseHandle(handle);}
};
class WriterLease {
    HANDLE handle_{INVALID_HANDLE_VALUE};
public:
    explicit WriterLease(const std::filesystem::path& root){
        SafePath(root);std::filesystem::create_directories(root);LockedPath pinned(root);
        const auto path=root/".catalog.lock";SafePath(path);
        handle_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("Program catalog is busy");
        if(!GetFileInformationByHandle(handle_,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;throw std::runtime_error("Invalid catalog lock path");}
    }
    ~WriterLease(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
};
std::string Read(const std::filesystem::path& path, size_t limit) {
    LockedPath parents(path.parent_path());
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Program file open failed");
    try {
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||
            !GetFileSizeEx(file,&size)||size.QuadPart<0||static_cast<uint64_t>(size.QuadPart)>limit)throw std::runtime_error("Program file invalid or too large");
        std::string value(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0;
        if(!ReadFile(file,value.data(),static_cast<DWORD>(value.size()),&count,nullptr)||count!=value.size())throw std::runtime_error("Program file read failed");
        CloseHandle(file);return value;
    }catch(...){CloseHandle(file);throw;}
}
void AtomicWrite(const std::filesystem::path& path, const std::string& data, bool replace) {
    SafePath(path); std::filesystem::create_directories(path.parent_path());
    LockedPath parents(path.parent_path());
    static std::atomic<uint64_t> serial{0};
    const auto temporary = path.wstring() + L".pending-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial); SafePath(temporary);
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Program staging already exists or cannot be created");
    DWORD written = 0; const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
        written == data.size() && FlushFileBuffers(file); CloseHandle(file);
    if (!ok) { DeleteFileW(temporary.c_str()); throw std::runtime_error("Program staging flush failed"); }
    SafePath(path);SafePath(temporary);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        DeleteFileW(temporary.c_str()); throw std::runtime_error("Program atomic replacement failed");
    }
}
Json Parse(const std::string& body, size_t maximum) {
    if (body.size() > maximum) throw std::runtime_error("Request exceeds capacity");
    std::vector<std::set<std::string>> keys;
    return Json::parse(body, [&keys](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 80) throw std::runtime_error("Request nesting exceeds capacity");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::object_end) keys.pop_back();
        else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("Duplicate request field");
        return true;
    });
}
void Exact(const Json& value, std::initializer_list<const char*> required, std::initializer_list<const char*> optional = {}) {
    if (!value.is_object()) throw std::runtime_error("Object required");
    for (const char* key : required) if (!value.contains(key)) throw std::runtime_error("Required field missing");
    for (const auto& field : value.items()) {
        bool allowed = false; for (auto key : required) allowed |= field.key() == key; for (auto key : optional) allowed |= field.key() == key;
        if (!allowed) throw std::runtime_error("Unknown field");
    }
}
ProgramCatalog::Result Error(int status, const std::string& code, const std::string& message) {
    return {status, {{"status", "error"}, {"error", code}, {"message", message}}};
}
bool Digest(const Json& value){return value.is_string()&&std::regex_match(value.get<std::string>(),std::regex("^[a-f0-9]{64}$"));}
bool Version(const Json& value){return value.is_string()&&std::regex_match(value.get<std::string>(),std::regex("^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$"));}
bool Name(const Json& value){return value.is_string()&&!value.get_ref<const std::string&>().empty()&&value.get_ref<const std::string&>().size()<=128&&
    std::none_of(value.get_ref<const std::string&>().begin(),value.get_ref<const std::string&>().end(),[](unsigned char c){return c<32||c==127;});}
std::string DiskRevision(const std::filesystem::path& path){return std::filesystem::exists(path)?Hash(Read(path,MaxCatalogBytes)):std::string("missing");}
void Source(const Json& source,const Json& program){
    Exact(source,{"name","json","publication"});
    if(!source["name"].is_string()||!std::regex_match(source["name"].get<std::string>(),std::regex("^[A-Za-z_][A-Za-z0-9_]{0,47}$"))||
       !source["json"].is_object()||source["publication"]!=program.at("publication"))throw std::runtime_error("Invalid source project");
    Exact(source["publication"],{"mode","fade_out_ms"});
    const std::regex secret_key("api[_ -]?key|credential|password|token|secret|diagnostic|compiler_path|applied_|published_|^(code|script|source_code|binary|dll|exe)$",std::regex::icase);
    const std::regex secret_value("(^|[^a-z0-9_])sk-[a-z0-9_-]{8,}|Bearer[[:space:]]+[^[:space:]]+|(api[_ -]?key|password|token|secret)[[:space:]]*[:=]|[a-z]:[\\/]|\\\\|/(home|Users|tmp|var)/|<script",std::regex::icase);
    std::function<void(const Json&,size_t)> scan=[&](const Json& value,size_t depth){
        if(depth>64)throw std::runtime_error("Source nesting exceeds capacity");
        if(value.is_string()){const auto& text=value.get_ref<const std::string&>();if(text.size()>128*1024||text.find('\0')!=std::string::npos||std::regex_search(text,secret_value))throw std::runtime_error("Sensitive or executable source value");}
        else if(value.is_object())for(const auto& item:value.items()){if(std::regex_search(item.key(),secret_key))throw std::runtime_error("Sensitive source field");scan(item.value(),depth+1);}
        else if(value.is_array())for(const auto& item:value)scan(item,depth+1);
    };scan(source,0);if(source.dump().size()>128*1024)throw std::runtime_error("Source project exceeds capacity");
}
}
struct ProgramCatalog::Command {
    std::function<bool()> action;
    std::mutex mutex; std::promise<bool> reply; bool cancelled{false}, done{false}, result{false};
};
ProgramCatalog::ProgramCatalog(std::filesystem::path root, PluginManager& manager)
    : root_(std::filesystem::absolute(std::move(root))), manager_(manager),
      catalog_({{"schema_version", 1}, {"revision", 0}, {"entries", Json::object()}, {"receipts", Json::array()}}) {
    try {
        SafePath(root_); const auto path = root_ / "catalog.json";disk_revision_="missing";
        if (!std::filesystem::exists(path)) return;
        const auto bytes=Read(path,MaxCatalogBytes);disk_revision_=Hash(bytes);auto value = Parse(bytes, MaxCatalogBytes);
        Exact(value, {"schema_version", "revision", "entries", "receipts"});
        if (!value["schema_version"].is_number_unsigned() || value["schema_version"] != 1 || !value["revision"].is_number_unsigned() || !value["entries"].is_object() || value["entries"].size() > 100 || !value["receipts"].is_array() || value["receipts"].size() > MaxReceipts) throw std::runtime_error("Unsupported program catalog");
        for (const auto& item : value["entries"].items()) {
            if (!Id(item.key())) throw std::runtime_error("Invalid program identity");
            Exact(item.value(), {"name", "active", "enabled", "removed", "versions"});
            const auto& entry = item.value();
            if (!Name(entry["name"])||!Digest(entry["active"])||!entry["enabled"].is_boolean() || !entry["removed"].is_boolean() ||(entry["enabled"]==true&&entry["removed"]==true)|| !entry["versions"].is_array() || entry["versions"].empty() || entry["versions"].size() > 20) throw std::runtime_error("Invalid program versions");
            bool active = false;
            std::set<std::string> hashes,labels;
            for (const auto& version : entry["versions"]) {
                Exact(version, {"hash", "version", "inputs","source_hash"});
                const auto hash = version["hash"].get<std::string>();
                Exact(version["inputs"],{"keyboard","cs2_gsi"});
                if (!Digest(version["hash"])||!Version(version["version"])||(!version["source_hash"].is_null()&&!Digest(version["source_hash"]))||
                    !version["inputs"]["keyboard"].is_boolean()||!version["inputs"]["cs2_gsi"].is_boolean()||
                    !hashes.insert(hash).second||!labels.insert(version["version"].get<std::string>()).second) throw std::runtime_error("Invalid program content metadata");
                active |= version["hash"] == entry["active"];
            }
            if (!active) throw std::runtime_error("Missing active version");
        }
        if(value["revision"]!=value["receipts"].size())throw std::runtime_error("Catalog revision/receipt count mismatch");
        std::set<std::string> operations;uint64_t receipt_revision=0;
        for(const auto& receipt:value["receipts"]){
            Exact(receipt,{"operation_id","digest","result"});const auto operation=receipt["operation_id"].get<std::string>();
            const auto& result=receipt["result"];Exact(result,{"status","operation_id","id","plugin_name","revision","active","enabled","removed","running_instances"});
            if(!Id(operation)||!operations.insert(operation).second||!Digest(receipt["digest"])||result["operation_id"]!=operation||
                result["status"]!="ok"||!result["id"].is_string()||!Id(result["id"].get<std::string>())||
                result["plugin_name"]!="ir_"+result["id"].get<std::string>()||!result["revision"].is_number_unsigned()||
                result["revision"]!=++receipt_revision||!value["entries"].contains(result["id"].get<std::string>())||!Digest(result["active"])||!result["enabled"].is_boolean()||!result["removed"].is_boolean()||
                (result["enabled"]==true&&result["removed"]==true)||
                !result["running_instances"].is_string())throw std::runtime_error("Invalid persisted receipt");
        }
        catalog_ = std::move(value);
    } catch (...) { diagnostic_ = "Program catalog invalid or unreadable; original files retained. Mutations disabled."; }
}
ProgramCatalog::Result ProgramCatalog::List() const {
    std::lock_guard<std::mutex> lock(mutex_); Json entries = Json::array();
    for (const auto& item : catalog_["entries"].items()) {
        auto record = item.value(); record["id"] = item.key(); record["plugin_name"] = "ir_" + item.key(); record["execution_tier"] = "restricted_ir_v1";
        const auto generation = manager_.GetGeneration(record["plugin_name"]);
        record["loaded"] = bool(generation); record["generation"] = generation ? generation->generation_id : 0;
        entries.push_back(std::move(record));
    }
    return {200, {{"schema_version", 1}, {"revision", catalog_["revision"]}, {"entries", entries}, {"diagnostic", diagnostic_}, {"receipt_count", catalog_["receipts"].size()}}};
}
void ProgramCatalog::Write(const Json& value) {
    const auto text = value.dump(2); if (text.size() > MaxCatalogBytes) throw std::runtime_error("Catalog capacity exceeded");
    const auto revision=Hash(text);
    if (before_write_) before_write_();
    if(DiskRevision(root_/"catalog.json")!=disk_revision_)throw std::runtime_error("Catalog changed externally");
    AtomicWrite(root_ / "catalog.json", text, true);disk_revision_=revision;
}
ProgramCatalog::Result ProgramCatalog::Receipt(const std::string& operation_id) const {
    if(!Id(operation_id))return Error(422,"invalid_operation_id","Operation ID must be 32 lowercase hex characters");
    std::lock_guard<std::mutex> lock(mutex_);for(const auto& receipt:catalog_["receipts"])if(receipt["operation_id"]==operation_id)return {200,receipt["result"]};
    return Error(404,"receipt_not_found","No durable result exists for this operation ID");
}
ProgramCatalog::Result ProgramCatalog::ExecuteText(const std::string& text){
    if(text.size()>MaxRequestBytes)return Error(413,"capacity","Request exceeds capacity");
    try{return Execute(Parse(text,MaxRequestBytes));}catch(...){return Error(422,"invalid_request","Malformed or duplicate request fields");}
}
ProgramCatalog::Result ProgramCatalog::Execute(const Json& request) {
    try {
        if(!request.is_object()||!request.contains("operation")||!request["operation"].is_string())return Error(422,"invalid_request","Operation required");
        const auto operation=request["operation"].get<std::string>();
        if(operation=="install"||operation=="update")Exact(request,{"operation","operation_id","expected_revision","id","name","version","program"},{"source"});
        else if(operation=="rollback")Exact(request,{"operation","operation_id","expected_revision","id","hash"});
        else Exact(request,{"operation","operation_id","expected_revision","id"});
        const auto op = request["operation"].get<std::string>(), id = request["id"].get<std::string>(), operation_id = request["operation_id"].get<std::string>();
        if (!Id(id) || !Id(operation_id) || !request["expected_revision"].is_number_unsigned()) return Error(422, "invalid_request", "Invalid ID or revision");
        if (!std::set<std::string>{"install", "update", "enable", "disable", "uninstall", "rollback"}.count(op)) return Error(422, "invalid_request", "Unsupported catalog action");
        std::lock_guard<std::mutex> writer(writer_mutex_);
        if(request.dump().size()>MaxRequestBytes)return Error(413,"capacity","Request exceeds capacity");
        const auto digest = Hash(request.dump()); std::shared_ptr<const EffectProgram> compiled;
        {std::lock_guard<std::mutex> lock(mutex_);
            if(!diagnostic_.empty())return Error(409,"catalog_unavailable",diagnostic_);
            for(const auto& receipt:catalog_["receipts"])if(receipt["operation_id"]==operation_id)
                return receipt["digest"]==digest?Result{200,receipt["result"]}:Error(409,"idempotency_conflict","Operation ID belongs to different request");
            if(catalog_["receipts"].size()>=MaxReceipts)return Error(409,"capacity","Receipt capacity reached; no idempotency records were evicted");}
        if (op == "install" || op == "update") {
            const auto name = request.at("name").get<std::string>(), version = request.at("version").get<std::string>();
            if (!Name(request["name"])||!Version(request["version"])) return Error(422, "invalid_metadata", "Invalid program name or version");
            const auto program = request.at("program").dump();
            if (request.dump().size() > MaxRequestBytes) return Error(413, "capacity", "Program package exceeds 240 KiB");
            compiled = EffectProgram::Parse(program);
            if (request.contains("source"))Source(request["source"],request["program"]);
        }
        { std::lock_guard<std::mutex> lock(mutex_);
          if (!diagnostic_.empty()) return Error(409, "catalog_unavailable", diagnostic_);
          for (const auto& receipt : catalog_["receipts"]) if (receipt["operation_id"] == operation_id)
              return receipt["digest"] == digest ? Result{200, receipt["result"]} : Error(409, "idempotency_conflict", "Operation ID belongs to a different request"); }
        WriterLease lease(root_);
        if(DiskRevision(root_/"catalog.json")!=disk_revision_)return Error(409,"catalog_changed","Catalog changed outside this instance; reload before retrying");
        return Commit(request, compiled, digest);
    } catch (const EffectProgramException& error) { return Error(422, error.Error().code, error.Error().message); }
      catch (...) { return Error(422, "invalid_request", "Program catalog request is invalid"); }
}
ProgramCatalog::Result ProgramCatalog::Commit(const Json& request, const std::shared_ptr<const EffectProgram>& program, const std::string& digest) {
    Json next; { std::lock_guard<std::mutex> lock(mutex_); next = catalog_; }
    const auto id = request["id"].get<std::string>(), op = request["operation"].get<std::string>();
    for (const auto& receipt : next["receipts"]) if (receipt["operation_id"] == request["operation_id"])
        return receipt["digest"] == digest ? Result{200, receipt["result"]} : Error(409, "idempotency_conflict", "Operation ID already used");
    if (next["revision"] != request["expected_revision"]) return Error(409, "revision_conflict", "Reload the catalog and review changes");
    auto& entries = next["entries"]; const bool exists = entries.contains(id);
    if (op == "install" ? exists : !exists) return Error(409, "identity_conflict", "Program install/update identity conflict");
    if (!exists && entries.size() >= 100) return Error(409, "capacity", "Program catalog limit reached");
    if(next["revision"].get<uint64_t>()==UINT64_MAX)return Error(409,"capacity","Revision exhausted");
    const auto alias = "ir_" + id; auto previous = manager_.GetGeneration(alias); PluginManager::PreparedReload prepared;
    PluginManager::ProgramRemoval removal;bool published = false, disabled = false;
    try {
        if (op == "install" || op == "update") {
            const auto content = request["program"].dump(), hash = Hash(content);
            auto& record = entries[id];
            if (!exists) record = {{"name", request["name"]}, {"active", hash}, {"enabled", false}, {"removed", false}, {"versions", Json::array()}};
            if (record["removed"] == true) return Error(409, "uninstalled", "Restore a retained version explicitly before updating");
            const Json source_hash=request.contains("source")?Json(Hash(request["source"].dump())):Json(nullptr);
            auto& versions = record["versions"]; bool known = false; for (auto& version : versions) {
                if(version["hash"]==hash){known=true;if(version["version"]!=request["version"]||version["source_hash"]!=source_hash)return Error(409,"immutable_metadata_conflict","Existing content hash has different version/source metadata");}
                else if(version["version"]==request["version"])return Error(409,"version_conflict","Version label already belongs to different content");
            }
            if (!known && versions.size() >= 20) return Error(409, "capacity", "Twenty versions retained; use a new package identity");
            const auto file = root_ / id / (hash + ".aurair");
            if (std::filesystem::exists(file)) { if (Read(file, EffectProgram::MaxJsonBytes) != content) throw std::runtime_error("Immutable content mismatch"); }
            else AtomicWrite(file, content, false);
            if (request.contains("source")) { const auto source = root_ / id / (hash + ".source.json");
                if(std::filesystem::exists(source)){if(Read(source,128*1024)!=request["source"].dump())throw std::runtime_error("Immutable source mismatch");}
                else AtomicWrite(source, request["source"].dump(), false); }
            if (!known) versions.push_back({{"hash", hash}, {"version", request["version"]},{"source_hash",source_hash}, {"inputs", {{"keyboard", program->UsesKeyboard()}, {"cs2_gsi", program->UsesGsi()}}}});
            record["name"] = request["name"]; record["active"] = hash;
        } else if (op == "rollback") {
            if(!Digest(request.at("hash")))return Error(422,"invalid_hash","Expected retained SHA-256 hash");
            const auto hash = request.at("hash").get<std::string>(); bool known = false;
            for (const auto& version : entries[id]["versions"]) known |= version["hash"] == hash;
            if (!known) return Error(422, "version_missing", "Rollback version not retained");
            entries[id]["active"] = hash; entries[id]["removed"] = false;
        } else if (op == "enable") { if (entries[id]["removed"] == true) return Error(409, "uninstalled", "Restore a retained version before enabling"); entries[id]["enabled"] = true; }
        else { entries[id]["enabled"] = false; if (op == "uninstall") entries[id]["removed"] = true; }
        auto& record = entries[id];
        if (record["enabled"] == true) {
            const auto hash = record["active"].get<std::string>(); const auto file = root_ / id / (hash + ".aurair");
            const auto content = Read(file, EffectProgram::MaxJsonBytes); if (Hash(content) != hash) throw std::runtime_error("Program integrity failed");
            const auto compiled=EffectProgram::Parse(content);
            const auto metadata=std::find_if(record["versions"].begin(),record["versions"].end(),[&](const Json& version){return version["hash"]==hash;});
            if(metadata==record["versions"].end()||(*metadata)["inputs"]["keyboard"]!=compiled->UsesKeyboard()||(*metadata)["inputs"]["cs2_gsi"]!=compiled->UsesGsi())throw std::runtime_error("Program capability mismatch");
            if(!(*metadata)["source_hash"].is_null()){const auto source=Read(root_/id/(hash+".source.json"),128*1024);
                if(Hash(source)!=(*metadata)["source_hash"])throw std::runtime_error("Source integrity failed");Source(Parse(source,128*1024),Parse(content,EffectProgram::MaxJsonBytes));}
            prepared = manager_.PrepareProgram(alias,compiled,file);
            if (!prepared || !Publish([this, prepared] { return manager_.PublishReload(prepared); })) return Error(409, "publication_failed", "Previous running generation retained");
            published = true;
        } else if (previous) {
            removal=manager_.PrepareProgramRemoval(alias,previous->generation_id);
            if (!removal||!Publish([this, removal] { return manager_.UnregisterProgram(removal); })) return Error(409, "publication_conflict", "Program generation changed");
            disabled = true;
        }
        next["revision"] = next["revision"].get<uint64_t>() + 1;
        Json result = {{"status", "ok"}, {"operation_id", request["operation_id"]}, {"id", id}, {"plugin_name", alias},
            {"revision", next["revision"]}, {"active", record["active"]}, {"enabled", record["enabled"]}, {"removed", record["removed"]},
            {"running_instances", "Previously admitted generations remain pinned until their normal lifecycle ends"}};
        next["receipts"].push_back({{"operation_id", request["operation_id"]}, {"digest", digest}, {"result", result}});
        Write(next); { std::lock_guard<std::mutex> lock(mutex_); catalog_ = std::move(next); }
        return {200, result};
    } catch (...) {
        bool restored = true;
        if (published) restored = Publish([this, prepared] { return manager_.RollbackProgram(prepared); });
        else if (disabled) restored=Publish([this,removal]{return manager_.RestoreProgram(removal);});
        if (!restored) { std::lock_guard<std::mutex> lock(mutex_); diagnostic_ = "Catalog persistence failed and generation recovery conflicted; review runtime status before further mutations"; }
        return Error(500, restored ? "persistence_failed" : "recovery_conflict", restored ? "Catalog operation failed; previous catalog and generation restored" : diagnostic_);
    }
}
bool ProgramCatalog::Publish(std::function<bool()> action) {
    auto command = std::make_shared<Command>(); command->action = std::move(action);
    auto reply = command->reply.get_future();
    { std::lock_guard<std::mutex> lock(mutex_); if (queue_.size() >= 16) return false; queue_.push_back(command); }
    if (reply.wait_for(std::chrono::seconds(2)) == std::future_status::ready) return reply.get();
    std::lock_guard<std::mutex> lock(command->mutex);
    if (command->done) return command->result; command->cancelled = true; return false;
}
bool ProgramCatalog::ApplyAtFrameBoundary() {
    std::shared_ptr<Command> command;
    { std::lock_guard<std::mutex> lock(mutex_); if (queue_.empty()) return false; command = queue_.front(); queue_.pop_front(); }
    std::lock_guard<std::mutex> lock(command->mutex);
    try { command->result = !command->cancelled && command->action(); } catch (...) { command->result = false; }
    command->done = true; command->reply.set_value(command->result); return command->result;
}
void ProgramCatalog::RestoreEnabled() {
    std::lock_guard<std::mutex> writer(writer_mutex_);Json catalog;
    {std::lock_guard<std::mutex> lock(mutex_);if(!diagnostic_.empty())return;catalog=catalog_;}
    std::vector<PluginManager::PreparedReload> prepared,published;
    try {
      for (const auto& item : catalog["entries"].items()) if (item.value()["enabled"] == true && item.value()["removed"] == false) {
            const auto hash = item.value()["active"].get<std::string>(); const auto file = root_ / item.key() / (hash + ".aurair");
            const auto content = Read(file, EffectProgram::MaxJsonBytes); if (Hash(content) != hash) throw std::runtime_error("Integrity");
            const auto compiled=EffectProgram::Parse(content);
            const auto& versions=item.value()["versions"];auto metadata=std::find_if(versions.begin(),versions.end(),[&](const Json& v){return v["hash"]==hash;});
            if(metadata==versions.end()||(*metadata)["inputs"]["keyboard"]!=compiled->UsesKeyboard()||(*metadata)["inputs"]["cs2_gsi"]!=compiled->UsesGsi())throw std::runtime_error("Capability metadata mismatch");
            if(!(*metadata)["source_hash"].is_null()){const auto source=Read(root_/item.key()/(hash+".source.json"),128*1024);if(Hash(source)!=(*metadata)["source_hash"])throw std::runtime_error("Source hash mismatch");Source(Parse(source,128*1024),Parse(content,EffectProgram::MaxJsonBytes));}
            auto candidate=manager_.PrepareProgram("ir_"+item.key(),compiled,file);if(!candidate)throw std::runtime_error("Preparation");prepared.push_back(candidate);
      }
      for(const auto& candidate:prepared){if(!manager_.PublishReload(candidate))throw std::runtime_error("Publication");published.push_back(candidate);}
    }catch(...){
      bool recovered=true;for(auto it=published.rbegin();it!=published.rend();++it)recovered=manager_.RollbackProgram(*it)&&recovered;
      std::lock_guard<std::mutex> lock(mutex_);diagnostic_=recovered?"Installed program restore failed; no partial startup publication retained":"Installed program restore and recovery conflicted; runtime review required";
    }
}
void ProgramCatalog::RegisterRoutes(httplib::Server& server) {
    server.Get("/api/programs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!IsAllowedLoopbackHost(req.get_header_value("Host"))) { res.status = 403; return; }
        if(req.params.size()>1 || (!req.params.empty()&&!req.has_param("operation_id"))){res.status=422;return;}
        const auto result = req.has_param("operation_id")?Receipt(req.get_param_value("operation_id")):List(); res.status = result.status; res.set_header("Cache-Control", "no-store"); res.set_content(result.body.dump(), "application/json");
    });
    server.Post("/api/programs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!IsAllowedLoopbackHost(req.get_header_value("Host")) || !ValidateLoopbackOriginAndReferer(req.get_header_value("Origin"), req.get_header_value("Referer"))) { res.status = 403; return; }
        if (!IsJsonContentType(req.get_header_value("Content-Type"))) { res.status = 415; return; }
        if (req.body.size() > MaxRequestBytes) { res.status = 413; return; }
        try { auto result = ExecuteText(req.body); res.status = result.status; res.set_content(result.body.dump(), "application/json"); }
        catch (...) { res.status = 422; res.set_content(R"({"error":"invalid_request"})", "application/json"); }
    });
}
}
