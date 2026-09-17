#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
Aura 独立发布包自动化构建脚本 (package_release.py)
=============================================================================
功能：
1. 编译最新的 Release 二进制文件 (aura_daemon.exe, aura_web_ui.exe)
2. 验证并准备所有可再分发资产 (web/index.html, calibrated_keymap.json)
3. 编译生成单一独立可执行文件 dist/Aura.exe（不内嵌 ASUS 专有 DLL）
4. 同时打包按 VERSION 命名的绿色便携 Zip 压缩包
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
VERSION_FILE = os.path.join(REPO_ROOT, "VERSION")

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
    # ASUS AacKbHal_x64.dll 是用户本机驱动资产，不复制、不内嵌、不打包。
    # daemon 运行时会从 ASUS 安装目录/注册路径定位，并在加载前执行 SHA-256 兼容性 Gate。
    print("[INFO] 公开发行包不包含 AacKbHal_x64.dll；运行时使用用户本机 ASUS 安装的已验证版本")

    # 1. 键位映射表
    keymap_path = os.path.join(REPO_ROOT, "calibrated_keymap.json")
    if not os.path.isfile(keymap_path):
        print("[FATAL] 缺少 calibrated_keymap.json！")
        sys.exit(1)
    print(f"[OK] calibrated_keymap.json ({os.path.getsize(keymap_path):,} bytes)")

    # 2. 前端单页打包产物
    web_html = os.path.join(REPO_ROOT, "web", "index.html")
    if not os.path.isfile(web_html):
        print("[*] 正在构建前端 React 单页面应用...")
        frontend_dir = os.path.join(REPO_ROOT, "frontend")
        run_cmd("npm run build", cwd=frontend_dir)
    print(f"[OK] web/index.html ({os.path.getsize(web_html):,} bytes)")

    # 3. Plugin SDK 公共头文件 (供单文件发行版原生光效发布)
    sdk_files = [
        ("engine", "effect.h"),
        ("engine", "plugin_interface.h"),
        ("aura", "aura_types.h"),
        ("aura", "keymap.h"),
    ]
    for subdir, fname in sdk_files:
        hp = os.path.join(REPO_ROOT, "include", subdir, fname)
        if not os.path.isfile(hp):
            print(f"[FATAL] 缺少 Plugin SDK 头文件: include/{subdir}/{fname}！")
            sys.exit(1)
        print(f"[OK] include/{subdir}/{fname} ({os.path.getsize(hp):,} bytes)")


def parse_rc_version(version_str):
    """
    解析版本字符串中的数值部分为 Windows RC 资源所需的 4 字段逗号分隔格式 (例如 '0,1,0,0')。
    """
    cleaned = version_str.lstrip("v").split("-")[0]
    parts = cleaned.split(".")
    nums = []
    for p in parts:
        try:
            nums.append(int(p))
        except ValueError:
            nums.append(0)
    while len(nums) < 4:
        nums.append(0)
    return ",".join(str(n) for n in nums[:4])


def resolve_release_version(cli_version=None):
    """
    单一版本来源解析：
    始终读取仓库根目录 VERSION（发行版本单一事实源）。
    保留 --version 仅为兼容旧命令；覆盖值必须与 VERSION 一致，否则拒绝打包。
    """
    try:
        with open(VERSION_FILE, "r", encoding="utf-8") as f:
            version = f.read().strip()
    except OSError as exc:
        raise RuntimeError(f"无法读取版本单一事实源 {VERSION_FILE}: {exc}") from exc
    if not version:
        raise RuntimeError(f"版本文件为空: {VERSION_FILE}")
    canonical = version if version.startswith("v") else f"v{version}"
    if cli_version and cli_version.strip():
        requested = cli_version.strip()
        requested = requested if requested.startswith("v") else f"v{requested}"
        if requested != canonical:
            raise RuntimeError(
                f"--version {requested} 与 VERSION 中的 {canonical} 不一致；"
                "请先更新 VERSION，避免发行版本漂移"
            )
    return canonical


def build_single_exe(version):
    print(f"\n--- 步骤 3: 编译单一独立可执行文件 (Aura.exe - Release {version}) ---")
    os.makedirs(DIST_DIR, exist_ok=True)
    temp_dir = os.path.join(REPO_ROOT, "build", "launcher_pack")
    os.makedirs(temp_dir, exist_ok=True)

    # 准备资源打包源文件
    daemon_src = os.path.join(BUILD_RELEASE_DIR, "aura_daemon.exe")
    web_ui_src = os.path.join(BUILD_RELEASE_DIR, "aura_web_ui.exe")
    keymap_src = os.path.join(REPO_ROOT, "calibrated_keymap.json")
    cfg_src    = os.path.join(REPO_ROOT, "config.example.json")
    web_src    = os.path.join(REPO_ROOT, "web", "index.html")

    sdk_effect_src      = os.path.join(REPO_ROOT, "include", "engine", "effect.h")
    sdk_plugin_intf_src = os.path.join(REPO_ROOT, "include", "engine", "plugin_interface.h")
    sdk_types_src       = os.path.join(REPO_ROOT, "include", "aura", "aura_types.h")
    sdk_keymap_src      = os.path.join(REPO_ROOT, "include", "aura", "keymap.h")

    shutil.copy2(daemon_src,          os.path.join(temp_dir, "daemon.bin"))
    shutil.copy2(web_ui_src,          os.path.join(temp_dir, "web_ui.bin"))
    shutil.copy2(keymap_src,          os.path.join(temp_dir, "keymap.bin"))
    shutil.copy2(cfg_src,             os.path.join(temp_dir, "config.bin"))
    shutil.copy2(web_src,             os.path.join(temp_dir, "web.bin"))
    shutil.copy2(sdk_effect_src,      os.path.join(temp_dir, "sdk_effect.bin"))
    shutil.copy2(sdk_plugin_intf_src, os.path.join(temp_dir, "sdk_plugin_intf.bin"))
    shutil.copy2(sdk_types_src,       os.path.join(temp_dir, "sdk_aura_types.bin"))
    shutil.copy2(sdk_keymap_src,      os.path.join(temp_dir, "sdk_keymap.bin"))

    rc_ver_csv = parse_rc_version(version)
    # 生成 .rc 文件，同时注入共用的版本信息元数据
    rc_content = f"""
#define IDR_DAEMON             101
#define IDR_WEB_UI             102
#define IDR_KEYMAP             104
#define IDR_CONFIG_EX          105
#define IDR_WEB_HTML           106
#define IDR_SDK_EFFECT         110
#define IDR_SDK_PLUGIN_INTF    111
#define IDR_SDK_AURA_TYPES     112
#define IDR_SDK_KEYMAP         113

IDR_DAEMON             RCDATA "daemon.bin"
IDR_WEB_UI             RCDATA "web_ui.bin"
IDR_KEYMAP             RCDATA "keymap.bin"
IDR_CONFIG_EX          RCDATA "config.bin"
IDR_WEB_HTML           RCDATA "web.bin"
IDR_SDK_EFFECT         RCDATA "sdk_effect.bin"
IDR_SDK_PLUGIN_INTF    RCDATA "sdk_plugin_intf.bin"
IDR_SDK_AURA_TYPES     RCDATA "sdk_aura_types.bin"
IDR_SDK_KEYMAP         RCDATA "sdk_keymap.bin"

1 VERSIONINFO
FILEVERSION {rc_ver_csv}
PRODUCTVERSION {rc_ver_csv}
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "ROG Falchion Ace HFX Aura Lighting Controller"
            VALUE "FileVersion", "{version}"
            VALUE "InternalName", "Aura"
            VALUE "OriginalFilename", "Aura.exe"
            VALUE "ProductName", "Aura"
            VALUE "ProductVersion", "{version}"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
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


def build_portable_zip(version):
    print(f"\n--- 步骤 4: 生成绿色便携 Zip 分发包 (Release {version}) ---")
    zip_name = f"Aura-{version}-windows-x64.zip"
    zip_path = os.path.join(DIST_DIR, zip_name)

    readme_content = f"""# ROG Falchion Ace HFX - Aura Lighting Controller (Release {version})

## 使用方法：
1. **单文件直接启动**：双击运行 `Aura.exe` 即可自动激活键盘灯效并开启后台监护。
2. **Web 配置界面**：启动后打开浏览器访问 `http://127.0.0.1:19898` 即可实时配置灯效方案与规则。
3. **ASUS 组件要求**：公开发行包不携带 ASUS 专有 DLL。请先安装 Armoury Crate / ASUS `Aac_Keyboard` 驱动包。

## 文件说明：
- `Aura.exe`: 整合单文件主程序 (已内嵌守护进程、网页配置服务与 Plugin SDK，不内嵌 ASUS DLL)。
- `config.example.json`: 配置文件模板 (若当前目录下无 config.json，启动时会自动生成)。
- `calibrated_keymap.json`: 68 键物理键位与硬件通道映射表。
- `include/`: Aura C++ Plugin SDK 运行时头文件 (供光效工作室原生发布编译，依赖本机 MSVC / C++ Build Tools)。

## ASUS HAL 说明：
Aura 运行时会从 ASUS 官方安装目录或注册路径定位 `AacKbHal_x64.dll`，并在加载前校验已验证的 SHA-256 与内存签名。缺失或版本不受支持时，硬件控制会安全失败；请从 ASUS 官方软件恢复驱动，不要从非官方来源下载 DLL。
"""
    readme_path = os.path.join(DIST_DIR, "README_RELEASE.md")
    with open(readme_path, "w", encoding="utf-8") as f:
        f.write(readme_content)

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(os.path.join(DIST_DIR, "Aura.exe"), "Aura.exe")
        zf.write(os.path.join(REPO_ROOT, "calibrated_keymap.json"), "calibrated_keymap.json")
        zf.write(os.path.join(REPO_ROOT, "config.example.json"), "config.example.json")
        for subdir, fname in [("engine", "effect.h"), ("engine", "plugin_interface.h"), ("aura", "aura_types.h"), ("aura", "keymap.h")]:
            rel = os.path.join("include", subdir, fname)
            zf.write(os.path.join(REPO_ROOT, rel), rel)
        zf.write(readme_path, "README.txt")

    print(f"[SUCCESS] 绿色便携 Zip 包已就绪: {zip_path} ({os.path.getsize(zip_path):,} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Aura 独立发布包自动化构建流水线")
    parser.add_argument("--version", type=str, default=None,
                        help="兼容旧命令的版本校验值；必须与仓库根目录 VERSION 一致")
    parser.add_argument("--skip-zip", action="store_true", help="跳过便携 Zip 包生成，仅输出独立单文件 Aura.exe")
    parser.add_argument("--clean", action="store_true", help="构建前清理既有 build 目录（用于切换生成器或纯净重构）")
    args = parser.parse_args()

    version = resolve_release_version(args.version)

    print("=========================================================")
    print(f" Aura 单文件独立发布包构建流水线 (Release: {version})")
    print("=========================================================")
    ensure_binaries(clean=args.clean)
    ensure_assets()
    build_single_exe(version)
    if not args.skip_zip:
        build_portable_zip(version)
    else:
        print("\n[*] 跳过便携 Zip 压缩包生成 (--skip-zip 已指定)")
    print("\n=========================================================")
    print(f" 全部构建任务顺利完成！发布产物位于 dist/ 目录 (Release: {version})：")
    for f in os.listdir(DIST_DIR):
        fp = os.path.join(DIST_DIR, f)
        print(f"  - {f:<35} ({os.path.getsize(fp):,} bytes)")
    print("=========================================================\n")


if __name__ == "__main__":
    main()
