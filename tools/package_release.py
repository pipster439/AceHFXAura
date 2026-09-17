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

import os
import sys
import shutil
import subprocess
import zipfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIST_DIR = os.path.join(REPO_ROOT, "dist")
BUILD_RELEASE_DIR = os.path.join(REPO_ROOT, "build", "Release")
DRIVERS_DIR = os.path.join(REPO_ROOT, "drivers")

def find_vcvars_bat():
    known_candidates = [
        r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\17\Professional\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\17\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    ]
    for cand in known_candidates:
        if os.path.isfile(cand):
            return cand

    vswhere = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if os.path.isfile(vswhere):
        try:
            res = subprocess.run(
                [vswhere, "-latest", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                capture_output=True, text=True, check=True
            )
            install_path = res.stdout.strip()
            if install_path:
                cand = os.path.join(install_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
                if os.path.isfile(cand):
                    return cand
        except Exception:
            pass

    return r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"


VCVARS_BAT = find_vcvars_bat()


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
        print(f"[ERROR] 执行失败 (Exit Code {proc.returncode}):\n{stderr_str}\n{stdout_str}")
        sys.exit(proc.returncode)
    return stdout_str


def ensure_binaries():
    print("\n--- 步骤 1: 确保 C++ Release 目标产物已构建 ---")
    daemon_exe = os.path.join(BUILD_RELEASE_DIR, "aura_daemon.exe")
    web_ui_exe = os.path.join(BUILD_RELEASE_DIR, "aura_web_ui.exe")

    build_dir = os.path.join(REPO_ROOT, "build")
    if not os.path.exists(build_dir):
        os.makedirs(build_dir, exist_ok=True)
        run_cmd(f'cmake -G "Visual Studio 18 2026" -A x64 ..', cwd=build_dir)

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
    print("=========================================================")
    print(" Aura 单文件独立发布包构建流水线 (GitHub Release)")
    print("=========================================================")
    ensure_binaries()
    ensure_assets()
    build_single_exe()
    build_portable_zip()
    print("\n=========================================================")
    print(" 全部构建任务顺利完成！发布产物位于 dist/ 目录：")
    for f in os.listdir(DIST_DIR):
        fp = os.path.join(DIST_DIR, f)
        print(f"  - {f:<35} ({os.path.getsize(fp):,} bytes)")
    print("=========================================================\n")


if __name__ == "__main__":
    main()
