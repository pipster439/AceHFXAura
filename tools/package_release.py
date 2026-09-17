#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
Aura 独立发布包自动化构建脚本 (package_release.py)
=============================================================================
功能：
1. 编译最新的 Release 二进制文件 (aura_daemon.exe, aura_web_ui.exe)
2. 验证并准备所有依赖项 (AacKbHal_x64.dll, web/index.html, calibrated_keymap.json)
3. 编译生成单一独立可执行文件 dist/Aura.exe (内嵌全套双进程组件与驱动)
4. 同时打包标准的绿色便携 Zip 压缩包 dist/Aura-v1.0.0-windows-x64.zip
5. 输出校验信息与 GitHub Release 发布指引
=============================================================================
"""

import argparse
import json
import os
import sys
import shutil
import subprocess
import zipfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIST_DIR = os.path.join(REPO_ROOT, "dist")
BUILD_RELEASE_DIR = os.path.join(REPO_ROOT, "build", "Release")
DRIVERS_DIR = os.path.join(REPO_ROOT, "drivers")

def detect_vs_toolchain():
    """
    统一探测本机 Visual Studio 工具链与匹配的 CMake 生成器。
    按优先顺序：
    1. vswhere 查询 installationPath 与 installationVersion
       - 18.x -> Visual Studio 18 2026
       - 17.x -> Visual Studio 17 2022
       - 16.x -> Visual Studio 16 2019
    2. 已知候选安装路径与对应生成器探测
    3. 缺省安全回退
    """
    vswhere = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if os.path.isfile(vswhere):
        try:
            res = subprocess.run(
                [vswhere, "-latest", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-format", "json", "-utf8"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True
            )
            raw_out = res.stdout.decode('utf-8', errors='replace')
            data = json.loads(raw_out)
            if data and len(data) > 0:
                inst = data[0]
                inst_path = inst.get("installationPath", "")
                inst_ver = inst.get("installationVersion", "")
                vcvars = os.path.join(inst_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
                if os.path.isfile(vcvars):
                    major = inst_ver.split(".")[0] if inst_ver else ""
                    if major == "18":
                        generator = "Visual Studio 18 2026"
                    elif major == "17":
                        generator = "Visual Studio 17 2022"
                    elif major == "16":
                        generator = "Visual Studio 16 2019"
                    else:
                        generator = f"Visual Studio {major}" if major else "Visual Studio 17 2022"
                    return vcvars, generator
        except Exception as e:
            print(f"[WARN] vswhere 探测失败: {e}")

    # Fallback 遍历已知常见候选路径
    known_candidates = [
        # VS 2026 (v18)
        (r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 18 2026"),
        (r"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 18 2026"),
        (r"C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 18 2026"),
        # VS 2022 (v17) - 常用安装路径
        (r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 17 2022"),
        (r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 17 2022"),
        (r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 17 2022"),
        (r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 17 2022"),
        (r"C:\Program Files\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat", "Visual Studio 17 2022"),
    ]
    for cand, gen in known_candidates:
        if os.path.isfile(cand):
            return cand, gen

    # 兜底：优先选择最普遍的 VS 2022
    if os.path.isdir(r"C:\Program Files\Microsoft Visual Studio\18"):
        return (
            r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
            "Visual Studio 18 2026"
        )
    return (
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "Visual Studio 17 2022"
    )


VCVARS_BAT, VS_GENERATOR = detect_vs_toolchain()


def get_cached_generator(cache_file):
    if not os.path.exists(cache_file):
        return None
    try:
        with open(cache_file, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                    return line.split("=", 1)[1].strip()
    except Exception:
        pass
    return None


def run_cmd(cmd_str, cwd=REPO_ROOT):
    full_cmd = f'call "{VCVARS_BAT}" && {cmd_str}'
    print(f"[CMD] {cmd_str}")
    proc = subprocess.run(f'cmd /c "{full_cmd}"', cwd=cwd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout_str = proc.stdout.decode('utf-8')
    except UnicodeDecodeError:
        stdout_str = proc.stdout.decode('gbk', errors='replace')
    try:
        stderr_str = proc.stderr.decode('utf-8')
    except UnicodeDecodeError:
        stderr_str = proc.stderr.decode('gbk', errors='replace')

    if proc.returncode != 0:
        print(f"[ERROR] 执行失败 (退出码 {proc.returncode}): {cmd_str}")
        print(stderr_str)
        sys.exit(proc.returncode)
    return stdout_str


def ensure_binaries(clean=False):
    print("\n--- 步骤 1: 确保 C++ Release 目标产物已构建 ---")
    print(f"[*] Visual Studio 工具链: {VCVARS_BAT}")
    print(f"[*] CMake 生成器: {VS_GENERATOR}")
    daemon_exe = os.path.join(BUILD_RELEASE_DIR, "aura_daemon.exe")
    web_ui_exe = os.path.join(BUILD_RELEASE_DIR, "aura_web_ui.exe")

    build_dir = os.path.join(REPO_ROOT, "build")
    cache_file = os.path.join(build_dir, "CMakeCache.txt")

    if clean and os.path.exists(build_dir):
        print(f"[*] 用户指定 --clean，正在清理既有 build 目录: {build_dir}")
        shutil.rmtree(build_dir, ignore_errors=True)

    if not os.path.exists(build_dir):
        os.makedirs(build_dir, exist_ok=True)
        print(f"[*] 执行 CMake 初始化配置 (-G \"{VS_GENERATOR}\" -A x64)...")
        run_cmd(f'cmake -G "{VS_GENERATOR}" -A x64 ..', cwd=build_dir)
    elif not os.path.exists(cache_file):
        print(f"[*] build 目录存在但未生成缓存，执行 CMake 配置 (-G \"{VS_GENERATOR}\" -A x64)...")
        run_cmd(f'cmake -G "{VS_GENERATOR}" -A x64 ..', cwd=build_dir)
    else:
        # build 目录已存在且包含 CMakeCache.txt，直接读取缓存检验生成器匹配
        cached_gen = get_cached_generator(cache_file)
        if cached_gen:
            print(f"[*] 既有 build 缓存生成器: {cached_gen}")
            if cached_gen != VS_GENERATOR:
                print(f"[ERROR] build 目录当前的 CMake 生成器 ({cached_gen}) 与已探测到的 Visual Studio ({VS_GENERATOR}) 不一致！\n"
                      f"为避免破坏已有构建产物，未静默删除 build 目录。\n"
                      f"请手动清理 build 目录或指定匹配的生成器。")
                sys.exit(1)
            else:
                print(f"[PASS] CMake 生成器校验一致 ({cached_gen})")

    print("正在编译 Release 目标...")
    run_cmd(f'cmake --build . --config Release --target aura_daemon aura_web_ui', cwd=build_dir)

    if not os.path.isfile(daemon_exe) or not os.path.isfile(web_ui_exe):
        print("[FATAL] 编译产物不存在！")
        sys.exit(1)
    print(f"[OK] aura_daemon.exe ({os.path.getsize(daemon_exe):,} bytes)")
    print(f"[OK] aura_web_ui.exe ({os.path.getsize(web_ui_exe):,} bytes)")

    # 同步复制最新产物至仓库根目录 (若被占用则告警但不中断打包流程)
    try:
        shutil.copy2(daemon_exe, os.path.join(REPO_ROOT, "aura_daemon.exe"))
        shutil.copy2(web_ui_exe, os.path.join(REPO_ROOT, "aura_web_ui.exe"))
        print(f"[OK] 已同步最新二进制至仓库根目录: aura_daemon.exe, aura_web_ui.exe")
    except PermissionError as pe:
        print(f"[WARN] 无法更新根目录二进制 (文件正被后台进程占用，发布产物已在 dist/ 正常生成): {pe}")

    # 清理旧版运行时目录 (%LOCALAPPDATA%\Aura\runtime) 确保单文件运行取用最新释放资产
    runtime_dir = os.path.expandvars(r"%LOCALAPPDATA%\Aura\runtime")
    if os.path.exists(runtime_dir):
        print(f"[*] 正在清理旧版运行时缓存目录: {runtime_dir}")
        try:
            shutil.rmtree(runtime_dir, ignore_errors=True)
            print("[OK] 已清空旧版运行时缓存目录")
        except Exception as e:
            print(f"[WARN] 清理运行时缓存目录异常: {e}")


def ensure_assets():
    print("\n--- 步骤 2: 验证并准备资产文件 ---")
    # 1. 驱动 DLL
    hal_dll = os.path.join(DRIVERS_DIR, "AacKbHal_x64.dll")
    if not os.path.isfile(hal_dll):
        asus_default = r"C:\Program Files\ASUS\Aac_Keyboard\AacKbHal_x64.dll"
        if os.path.isfile(asus_default):
            os.makedirs(DRIVERS_DIR, exist_ok=True)
            shutil.copy2(asus_default, hal_dll)
            print(f"[+] 从华硕目录抓取 AacKbHal_x64.dll -> {hal_dll}")
        else:
            print("[FATAL] 缺少 AacKbHal_x64.dll！")
            sys.exit(1)
    print(f"[OK] AacKbHal_x64.dll ({os.path.getsize(hal_dll):,} bytes)")

    # 2. 键位映射表
    keymap_path = os.path.join(REPO_ROOT, "calibrated_keymap.json")
    if not os.path.isfile(keymap_path):
        print("[FATAL] 缺少 calibrated_keymap.json！")
        sys.exit(1)
    print(f"[OK] calibrated_keymap.json ({os.path.getsize(keymap_path):,} bytes)")

    # 3. 前端单页打包产物
    web_html = os.path.join(REPO_ROOT, "web", "index.html")
    if not os.path.isfile(web_html):
        print("[*] 正在构建前端 React 单页面应用...")
        frontend_dir = os.path.join(REPO_ROOT, "frontend")
        run_cmd("npm run build", cwd=frontend_dir)
    print(f"[OK] web/index.html ({os.path.getsize(web_html):,} bytes)")


def build_single_exe():
    print("\n--- 步骤 3: 编译单一独立可执行文件 (Aura.exe) ---")
    os.makedirs(DIST_DIR, exist_ok=True)
    temp_dir = os.path.join(REPO_ROOT, "build", "launcher_pack")
    os.makedirs(temp_dir, exist_ok=True)

    # 准备资源打包源文件
    daemon_src = os.path.join(BUILD_RELEASE_DIR, "aura_daemon.exe")
    web_ui_src = os.path.join(BUILD_RELEASE_DIR, "aura_web_ui.exe")
    hal_src    = os.path.join(DRIVERS_DIR, "AacKbHal_x64.dll")
    keymap_src = os.path.join(REPO_ROOT, "calibrated_keymap.json")
    cfg_src    = os.path.join(REPO_ROOT, "config.example.json")
    web_src    = os.path.join(REPO_ROOT, "web", "index.html")

    shutil.copy2(daemon_src, os.path.join(temp_dir, "daemon.bin"))
    shutil.copy2(web_ui_src, os.path.join(temp_dir, "web_ui.bin"))
    shutil.copy2(hal_src,    os.path.join(temp_dir, "hal.bin"))
    shutil.copy2(keymap_src, os.path.join(temp_dir, "keymap.bin"))
    shutil.copy2(cfg_src,    os.path.join(temp_dir, "config.bin"))
    shutil.copy2(web_src,    os.path.join(temp_dir, "web.bin"))

    # 生成 .rc 文件
    rc_content = """
#define IDR_DAEMON       101
#define IDR_WEB_UI       102
#define IDR_HAL_DLL      103
#define IDR_KEYMAP       104
#define IDR_CONFIG_EX    105
#define IDR_WEB_HTML     106

IDR_DAEMON       RCDATA "daemon.bin"
IDR_WEB_UI       RCDATA "web_ui.bin"
IDR_HAL_DLL      RCDATA "hal.bin"
IDR_KEYMAP       RCDATA "keymap.bin"
IDR_CONFIG_EX    RCDATA "config.bin"
IDR_WEB_HTML     RCDATA "web.bin"
"""
    rc_path = os.path.join(temp_dir, "launcher.rc")
    with open(rc_path, "w", encoding="utf-8") as f:
        f.write(rc_content)

    print("正在编译资源文件 (rc.exe)...")
    run_cmd(f'rc.exe /nologo /fo "{os.path.join(temp_dir, "launcher.res")}" "{rc_path}"', cwd=temp_dir)

    launcher_cpp = os.path.join(REPO_ROOT, "src", "launcher", "launcher_main.cpp")
    out_exe = os.path.join(DIST_DIR, "Aura.exe")

    print("正在编译并链接单文件启动器 (cl.exe)...")
    compile_cmd = (
        f'cl.exe /nologo /O2 /std:c++17 /EHsc /utf-8 '
        f'/DWIN32_LEAN_AND_MEAN /DNOMINMAX '
        f'"{launcher_cpp}" "{os.path.join(temp_dir, "launcher.res")}" '
        f'/Fe:"{out_exe}" '
        f'/link /SUBSYSTEM:CONSOLE user32.lib advapi32.lib shell32.lib'
    )
    run_cmd(compile_cmd, cwd=temp_dir)

    print(f"[SUCCESS] 独立单文件产物已就绪: {out_exe} ({os.path.getsize(out_exe):,} bytes)")
    root_exe = os.path.join(REPO_ROOT, "Aura.exe")
    try:
        shutil.copy2(out_exe, root_exe)
        print(f"[OK] 已同步单文件启动器至仓库根目录: Aura.exe ({os.path.getsize(root_exe):,} bytes)")
    except PermissionError as pe:
        print(f"[WARN] 无法更新根目录 Aura.exe (可能正处于运行状态): {pe}")
    return out_exe


def build_portable_zip():
    print("\n--- 步骤 4: 生成绿色便携 Zip 分发包 ---")
    zip_name = "Aura-v1.0.0-windows-x64.zip"
    zip_path = os.path.join(DIST_DIR, zip_name)

    readme_content = """# ROG Falchion Ace HFX - Aura Lighting Controller (Release v1.0.0)

## 使用方法：
1. **单文件直接启动**：双击运行 `Aura.exe` 即可自动激活键盘灯效并开启后台监护。
2. **Web 配置界面**：启动后打开浏览器访问 `http://127.0.0.1:19898` 即可实时配置灯效方案与规则。
3. **免奥创独立使用**：本程序已内嵌华硕底层直通驱动，无须在本机安装或运行华硕奥创中心 (Armoury Crate)。

## 文件说明：
- `Aura.exe`: 整合单文件主程序 (已内嵌守护进程、网页配置服务与底层硬件驱动)。
- `config.example.json`: 配置文件模板 (若当前目录下无 config.json，启动时会自动生成)。
- `calibrated_keymap.json`: 68 键物理键位与硬件通道映射表。
"""
    readme_path = os.path.join(DIST_DIR, "README_RELEASE.md")
    with open(readme_path, "w", encoding="utf-8") as f:
        f.write(readme_content)

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(os.path.join(DIST_DIR, "Aura.exe"), "Aura.exe")
        zf.write(os.path.join(REPO_ROOT, "calibrated_keymap.json"), "calibrated_keymap.json")
        zf.write(os.path.join(REPO_ROOT, "config.example.json"), "config.example.json")
        zf.write(os.path.join(DRIVERS_DIR, "AacKbHal_x64.dll"), "drivers/AacKbHal_x64.dll")
        zf.write(readme_path, "README.txt")

    print(f"[SUCCESS] 绿色便携 Zip 包已就绪: {zip_path} ({os.path.getsize(zip_path):,} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Aura 独立发布包自动化构建流水线")
    parser.add_argument("--skip-zip", action="store_true", help="跳过便携 Zip 包生成，仅输出独立单文件 Aura.exe")
    parser.add_argument("--clean", action="store_true", help="构建前清理既有 build 目录（用于切换生成器或纯净重构）")
    args = parser.parse_args()

    print("=========================================================")
    print(" Aura 单文件独立发布包构建流水线 (GitHub Release)")
    print("=========================================================")
    ensure_binaries(clean=args.clean)
    ensure_assets()
    build_single_exe()
    if not args.skip_zip:
        build_portable_zip()
    else:
        print("\n[*] 跳过便携 Zip 压缩包生成 (--skip-zip 已指定)")
    print("\n=========================================================")
    print(" 全部构建任务顺利完成！发布产物位于 dist/ 目录：")
    for f in os.listdir(DIST_DIR):
        fp = os.path.join(DIST_DIR, f)
        print(f"  - {f:<35} ({os.path.getsize(fp):,} bytes)")
    print("=========================================================\n")


if __name__ == "__main__":
    main()
