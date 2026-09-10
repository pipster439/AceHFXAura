#include "aura/aura_adapter.h"
#include "aura/keymap.h"
#include "monitor/foreground_monitor.h"
#include "config/rule_engine.h"
#include "engine/effect_engine.h"
#include "supervisor/web_supervisor.h"
#include "gsi/gsi_adapter.h"
#include "utils/logger.h"
#include "utils/system_info.h"

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <windows.h>
#include <objbase.h>

namespace {

std::atomic<bool> g_running{true};
std::string g_state_file = ".daemon_running";

BOOL WINAPI ConsoleCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || 
        signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        LOG_INFO("接收到系统中断信号 (" + std::to_string(signal) + ")，正在请求优雅退出...");
        g_running.store(false, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::cout << "=========================================================\n"
              << " ROG FALCHION ACE HFX 硬件级单键 RGB 核心守护进程\n"
              << "=========================================================\n"
              << "用法: aura_daemon.exe [选项]\n\n"
              << "选项:\n"
              << "  --dry-run                 启动虚拟推流模式 (不挂载底层驱动，验证监控与规则)\n"
              << "  --test-init <次数>        对底层驱动执行 N 次初始化/释放循环内存安全压力测试\n"
              << "  --test-stability <分钟>   持续以 25 FPS 推流指定时长进行稳定性硬性验收\n"
              << "  --config <文件路径>       指定配置文件路径 (默认: config.json)\n"
              << "  --keymap <文件路径>       指定权威键位表文件路径 (默认: calibrated_keymap.json)\n"
              << "  --log-level <级别>        设置日志级别 (debug|info|warn|error，默认: info)\n"
              << "  --help                    显示本帮助信息\n"
              << "=========================================================\n";
}

// RAII 守卫管理 COM 生命周期：确保与 CoInitializeEx 成对释放，并在提前返回时自动清理
struct ComScope {
    HRESULT hr;
    explicit ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
    bool ok() const { return SUCCEEDED(hr); }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;
};

} // namespace

int main(int argc, char* argv[]) {
    // 0. 初始化 COM 单线程套间 (RAII 守卫置于 main 顶部，先于所有局部对象声明以保证 LIFO 逆序析构)
    ComScope com;

    // 初始化日志系统
    aura::Logger::Instance().Init("aura_daemon.log");

    // 检查 COM 初始化结果 (含 RPC_E_CHANGED_MODE 模式冲突处理)
    if (!com.ok()) {
        std::ostringstream hr_oss;
        hr_oss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(com.hr);
        if (com.hr == RPC_E_CHANGED_MODE) {
            LOG_ERROR("FATAL: COM 初始化失败: 当前线程已被初始化为与 STA 不兼容的并发模式 (RPC_E_CHANGED_MODE, " 
                      + hr_oss.str() + ")，守护进程无法继续运行。");
        } else {
            LOG_ERROR("FATAL: COM 初始化失败 (错误码: " + hr_oss.str() + ")，守护进程无法继续运行。");
        }
        return 1;
    }

    // 1. 预先设置 DLL 搜索路径并加载华硕 HAL (必须在创建驱动实例前完成，确保其依赖项正常解析)
    SetDllDirectoryW(L"C:\\Program Files\\ASUS\\Aac_Keyboard");
    HMODULE hHalPreload = LoadLibraryW(L"C:\\Program Files\\ASUS\\Aac_Keyboard\\AacKbHal_x64.dll");

    // 2. 解析命令行参数
    bool dry_run = false;
    int test_init_count = 0;
    double test_stability_minutes = 0.0;
    std::string config_path = "config.json";
    std::string keymap_path = "calibrated_keymap.json";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--test-init" && i + 1 < argc) {
            test_init_count = std::stoi(argv[++i]);
        } else if (arg == "--test-stability" && i + 1 < argc) {
            test_stability_minutes = std::stod(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--keymap" && i + 1 < argc) {
            keymap_path = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            std::string level_str = argv[++i];
            for (auto& c : level_str) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (level_str == "debug") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Debug);
            } else if (level_str == "info") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Info);
            } else if (level_str == "warn" || level_str == "warning") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Warn);
            } else if (level_str == "error") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Error);
            } else {
                std::cerr << "未知的日志级别: " << level_str << " (支持: debug, info, warn, error)\n";
            }
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
    }

    // 3. 单实例保护 (Named Mutex)
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Local\\RogFalchionAceHfxDaemonMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LOG_ERROR("FATAL: 检测到已有另一个 ROG Falchion Ace HFX 守护进程正在运行，拒绝重复启动！");
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 4. 注册控制台中断处理器 (Ctrl+C)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // 5. 压力测试独立分支
    if (test_init_count > 0) {
        aura::AuraAdapter test_adapter(false);
        bool test_ok = test_adapter.RunInitStressTest(static_cast<size_t>(test_init_count));
        if (hMutex) CloseHandle(hMutex);
        return test_ok ? 0 : 1;
    }

    // 6. 检测异常退出标记并维护状态文件
    bool had_abnormal_exit = false;
    {
        std::ifstream check_file(g_state_file);
        if (check_file.is_open()) {
            had_abnormal_exit = true;
            LOG_WARN("【异常退出兜底】检测到上一次进程未正常退出（存在残留标记文件 " + g_state_file + 
                     "），将在连接后执行强制复位！");
        }
    }

    // 写入当前运行标记 (PID + 时间戳)
    {
        std::ofstream state_out(g_state_file, std::ios::trunc);
        if (state_out.is_open()) {
            state_out << "PID=" << GetCurrentProcessId() << "\n"
                      << "StartTime=" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n";
        }
    }

    // 7. 加载权威键位表与配置文件
    aura::Keymap keymap;
    if (!keymap.LoadFromJson(keymap_path)) {
        LOG_ERROR("FATAL: 无法加载键位表: " + keymap_path);
        DeleteFileA(g_state_file.c_str());
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    aura::RuleEngine rule_engine;
    if (!rule_engine.LoadConfig(config_path)) {
        LOG_WARN("加载配置文件失败: " + config_path + "，将使用内部预设规则");
    }

    // 8. 挂载华硕底层驱动适配器 (在主线程完全拥有，杜绝多线程 COM 激活冲突)
    if (had_abnormal_exit) {
        LOG_INFO("检测到上次非正常退出，正在等待华硕底层驱动通道就绪 (3 秒)...");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    aura::AuraAdapter adapter(dry_run);
    if (!adapter.Initialize(&keymap)) {
        LOG_WARN("底层驱动初次挂载未就绪，将在推流循环中自动重连");
    }

    // 异常退出强制复位兜底
    if (had_abnormal_exit) {
        adapter.ForceReset();
    }

    // 9. 启动 CS2 GSI 适配器 (严格监听 127.0.0.1:19897)
    // 核心线程隔离纪律：网络 I/O 线程仅在内存中维护 GsiState 键值字典，严禁直接触碰 COM/HAL
    aura::GsiAdapter gsi_adapter;
    if (!gsi_adapter.Start(19897)) {
        LOG_WARN("CS2 GSI 适配器监听 127.0.0.1:19897 失败 (可能端口已被占用)");
    } else {
        LOG_INFO("[+] CS2 GSI 接收服务已启动，监听 http://127.0.0.1:19897/");
    }

    // 10. 构建效果引擎
    aura::EffectEngine effect_engine;
    std::shared_ptr<const aura::Profile> initial_profile = rule_engine.MatchProfile("", &gsi_adapter.GetState());
    std::string current_active_profile_name = initial_profile ? initial_profile->name : "(None)";
    effect_engine.SetActiveProfile(initial_profile);

    // 11. 启动网页配置服务后台监护器 (默认桌面状态自动拉起，由独立工线程异步监护)
    aura::WebUiSupervisor web_supervisor;
    web_supervisor.StartSupervisor(std::filesystem::path(config_path), 19898);

    // 12. 启动前台窗口监控线程 (WinEventHook 专属消息线程)
    // 线程纪律：WinEventHook 回调运行在监控线程，只做【最小化通知】——把前台进程名写入
    // GsiState（该写入已由 GsiState::mutex_ 保护）。回调内【不】做规则匹配、不切换方案、
    // 不触碰效果引擎与网页监护器，也【不】读写任何主循环的局部变量，从而消除跨线程数据竞争。
    // 所有决策统一收敛到主循环单点执行（见下方 25FPS 循环）。
    aura::ForegroundMonitor monitor;

    monitor.SetCallback([&gsi_adapter](const std::string& proc_name, HWND /*hwnd*/) {
        gsi_adapter.GetState().SetForegroundProcess(proc_name);
    });

    if (!monitor.Start()) {
        LOG_ERROR("启动前台窗口监控失败");
    }

    // 13. 主循环：25 FPS (40ms) 定时时钟推流与配置热重载
    LOG_INFO("[+] ROG FALCHION ACE HFX 守护进程正在运行中 (按 Ctrl+C 优雅退出)...");

    aura::FrameBuffer frame_buf;
    constexpr int TARGET_FPS = 25;
    constexpr std::chrono::milliseconds FRAME_TIME(1000 / TARGET_FPS); // 40ms

    auto loop_start = std::chrono::steady_clock::now();
    auto next_tick = loop_start;
    auto last_stat_time = loop_start;

    uint64_t total_frames = 0;
    uint64_t frames_since_stat = 0;
    uint64_t current_minute = 0;
    // 主循环独占：上次已按【前台进程名】处理过的值，用于驱动网页服务抑制状态
    // （仅主循环访问，故无需加锁；这正是 R2 把决策单点化后获得的简化）
    std::string last_proc_seen = "__UNSET__";

    while (g_running.load(std::memory_order_acquire)) {
        // 主线程统一评估当前前台进程与 GSI 状态驱动的灯效方案
        // (严格遵循主线程独占 COM/HAL 纪律，绝不在网络线程执行硬件调用)
        std::string cur_proc = monitor.GetCurrentProcessName();
        gsi_adapter.GetState().SetForegroundProcess(cur_proc);
        std::shared_ptr<const aura::Profile> matched = rule_engine.MatchProfile(cur_proc, &gsi_adapter.GetState());
        std::string prof_name = matched ? matched->name : "(None)";
        if (prof_name != current_active_profile_name) {
            current_active_profile_name = prof_name;
            effect_engine.SetActiveProfile(matched);
            LOG_INFO("灯效方案动态切换 -> [" + prof_name + "] (前台: " + 
                     (cur_proc.empty() ? "桌面/未知" : cur_proc) + 
                     (gsi_adapter.GetState().IsActive() ? ", GSI在线" : "") + ")");
        }

        // 网页服务抑制状态：必须按【前台进程名】变化来更新，不能挂在【方案名】变化上。
        // 原因：不同进程可能映射到同一个方案（例如 code.exe 与 devenv.exe 都 → coding），
        // 此时方案名不变；若只在方案切换时更新，就会漏掉 suppress_web_ui 标记的变化，
        // 导致该开的服务没开、该停的没停。
        // SetSuppressed 内部为原子交换，且仅在值真正变化时才 notify，重复调用无副作用。
        if (cur_proc != last_proc_seen) {
            last_proc_seen = cur_proc;
            bool suppress = rule_engine.ShouldSuppressWebUi(cur_proc);
            web_supervisor.SetSuppressed(suppress);
            LOG_INFO("前台进程变更 -> [" + (cur_proc.empty() ? "桌面/未知" : cur_proc) + "]" +
                     (suppress ? " (网页服务已抑制)" : ""));
        }

        // 零分配计算当前帧
        effect_engine.Tick(frame_buf, keymap);

        // 推流至硬件
        if (adapter.IsConnected()) {
            adapter.PushFrame(frame_buf);
        } else {
            adapter.CheckReconnect();
        }

        total_frames++;
        frames_since_stat++;

        // 每秒 (25 帧) 检查一次配置文件热重载
        if (total_frames % TARGET_FPS == 0) {
            if (rule_engine.CheckAndReload()) {
                std::string cur_proc_now = monitor.GetCurrentProcessName();
                std::shared_ptr<const aura::Profile> reload_matched = rule_engine.MatchProfile(cur_proc_now, &gsi_adapter.GetState());
                current_active_profile_name = reload_matched ? reload_matched->name : "(None)";
                effect_engine.SetActiveProfile(reload_matched);

                bool suppress = rule_engine.ShouldSuppressWebUi(cur_proc_now);
                web_supervisor.SetSuppressed(suppress);

                LOG_INFO("配置热重载生效，当前活跃方案更新为: [" + current_active_profile_name + "]" + 
                         (suppress ? " (网页服务已抑制)" : ""));
            }
        }

        // 统计与长效稳定性监控 (每 60 秒打印一次)
        auto now = std::chrono::steady_clock::now();
        auto stat_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stat_time).count();
        if (stat_elapsed_ms >= 60000) {
            current_minute++;
            double instant_fps = static_cast<double>(frames_since_stat) / (static_cast<double>(stat_elapsed_ms) / 1000.0);
            auto total_elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - loop_start).count();
            double avg_fps = total_elapsed_sec > 0 ? (static_cast<double>(total_frames) / static_cast<double>(total_elapsed_sec)) : 0.0;

            aura::ProcessMemoryStats mem{};
            aura::GetCurrentProcessMemory(mem);

            std::string mode_str = dry_run ? "[Dry-Run]" : "[Hardware]";
            LOG_INFO(mode_str + " [推流稳定性报告 第 " + std::to_string(current_minute) + " 分钟] " +
                     "累计帧数: " + std::to_string(total_frames) + 
                     ", 瞬时FPS: " + std::to_string(instant_fps).substr(0, 5) + 
                     ", 平均FPS: " + std::to_string(avg_fps).substr(0, 5) + 
                     ", 物理内存WorkingSet: " + aura::FormatBytes(mem.working_set_bytes) + 
                     ", 提交内存PrivateBytes: " + aura::FormatBytes(mem.private_bytes));

            last_stat_time = now;
            frames_since_stat = 0;

            // 若处于稳定性验收测试模式且达到指定分钟数
            if (test_stability_minutes > 0.0 && current_minute >= static_cast<uint64_t>(test_stability_minutes)) {
                LOG_INFO("【稳定性验收达成】已完成预设的 " + std::to_string(test_stability_minutes) + 
                         " 分钟持续推流测试！正在退出测试循环...");
                g_running.store(false, std::memory_order_release);
                break;
            }
        }

        // 对齐 40ms 节拍
        next_tick += FRAME_TIME;
        auto sleep_dur = next_tick - std::chrono::steady_clock::now();
        if (sleep_dur > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_dur);
        } else {
            next_tick = std::chrono::steady_clock::now();
        }
    }

    // 14. 优雅停机与资源回收
    LOG_INFO("正在停止网页配置服务监护器...");
    web_supervisor.Shutdown();

    LOG_INFO("正在停止 CS2 GSI 接收服务...");
    gsi_adapter.Stop();

    LOG_INFO("正在停止前台监控线程...");
    monitor.Stop();

    LOG_INFO("正在安全关闭 Aura 硬件连接...");
    adapter.Shutdown();

    // 正常优雅退出：清除运行状态标记文件
    DeleteFileA(g_state_file.c_str());
    LOG_INFO("[+] 运行状态标记已清除 (.daemon_running 已删除)");

    if (hMutex) {
        CloseHandle(hMutex);
    }

    LOG_INFO("[+] 守护进程已优雅退出，所有资源已安全释放。");
    return 0;
}
