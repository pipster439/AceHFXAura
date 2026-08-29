#!/usr/bin/env python3
"""
ROG FALCHION ACE HFX 官方安全通道灯效控制工具
通过华硕本地 Framework HTTP 接口 (127.0.0.1:1042) 与官方硬件 HAL 通信，安全无损更改键盘灯光。
"""

import sys
import json
import urllib.request
import urllib.error

# Ensure UTF-8 output on Windows consoles
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

DEVICE_TYPE = "2"        # 2 = Keyboard
MODEL_NUMBER = "7038"    # ROG FALCHION ACE HFX

def find_framework_base():
    """动态寻找 asus_framework 当前监听的有效端口"""
    candidate_ports = [8803, 1042, 8804, 8805, 8802, 1041]
    for port in candidate_ports:
        try:
            url = f"http://127.0.0.1:{port}/type/{DEVICE_TYPE}/model/{MODEL_NUMBER}/deviceInfo"
            with urllib.request.urlopen(url, timeout=0.8) as resp:
                if resp.status == 200:
                    return f"http://127.0.0.1:{port}"
        except:
            pass
    return "http://127.0.0.1:8803"

COLOR_MAP = {
    "red": (255, 0, 0),
    "green": (0, 255, 0),
    "blue": (0, 0, 255),
    "white": (255, 255, 255),
    "yellow": (255, 255, 0),
    "cyan": (0, 255, 255),
    "purple": (180, 0, 255),
    "magenta": (255, 0, 255),
    "orange": (255, 120, 0),
    "pink": (255, 20, 147),
    "off": (0, 0, 0),
    "black": (0, 0, 0),
}

def get_device_info():
    """获取设备基本状态与当前配置索引"""
    base = find_framework_base()
    url = f"{base}/type/{DEVICE_TYPE}/model/{MODEL_NUMBER}/deviceInfo"
    req = urllib.request.Request(url)
    try:
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            profile_id = data.get("profileID", "3")
            is_alive = data.get("isDeviceAlive", False)
            fw_version = data.get("fwVersion", "未知")
            return profile_id, is_alive, fw_version
    except Exception as e:
        print(f"[-] 获取设备状态失败: {e}")
        return "3", False, "未知"

def set_color(r: int, g: int, b: int, brightness: int = 100):
    """下发静态单色灯效"""
    profile_id, is_alive, fw_ver = get_device_info()
    if not is_alive:
        print(f"[*] 提示: 键盘在线状态检测中 (Profile: {profile_id}, 固件: {fw_ver})")

    base = find_framework_base()
    url = f"{base}/type/{DEVICE_TYPE}/model/{MODEL_NUMBER}/lighting"

    # 规范化 RGB 范围与亮度
    r = max(0, min(255, int(r)))
    g = max(0, min(255, int(g)))
    b = max(0, min(255, int(b)))
    brightness = max(0, min(100, int(brightness)))

    payload = {
        "profileID": str(profile_id),
        "isPreview": False,
        "data": {
            "switchStatus": True if (r > 0 or g > 0 or b > 0) else False,
            "keyboard": {
                "effectID": "0",       # 0 = Static 静态纯色
                "colorType": "Single",
                "backgroundType": "Off",
                "backgroundColor": {"key": "1", "r": "0", "g": "0", "b": "0"},
                "singleMulti": "0",
                "brightness": str(brightness),
                "analogEffect": "0",
                "speed": "1",
                "direction": "-1",
                "random": "-1",
                "width": "-1",
                "pattern": {
                    "key": "-1",
                    "colorNumber": 1,
                    "singleColor": [
                        {"key": "1", "r": str(r), "g": str(g), "b": str(b)}
                    ]
                }
            }
        }
    }

    req_data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=req_data, method="PUT")
    req.add_header("Content-Type", "application/json")

    try:
        with urllib.request.urlopen(req, timeout=5.0) as resp:
            if resp.status == 200:
                print(f"[+] 成功设置键盘灯光颜色为 RGB({r}, {g}, {b}) [亮度: {brightness}%, Profile: {profile_id}]")
                return True
            else:
                print(f"[-] 服务器返回非 200 状态码: {resp.status}")
                return False
    except urllib.error.HTTPError as e:
        print(f"[-] HTTP 错误 {e.code}: {e.read().decode('utf-8', errors='ignore')}")
        return False
    except Exception as e:
        print(f"[-] 发送灯光控制命令失败: {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("ROG FALCHION ACE HFX 官方安全通道灯光控制工具")
        print("===============================================")
        print("用法:")
        print("  python set_rgb.py <颜色名>               (例: python set_rgb.py red)")
        print("  python set_rgb.py <R> <G> <B>            (例: python set_rgb.py 255 0 128)")
        print("  python set_rgb.py <R> <G> <B> [亮度0-100] (例: python set_rgb.py 0 255 255 80)")
        print("\n支持的预置颜色名:")
        print("  " + ", ".join(COLOR_MAP.keys()))
        sys.exit(1)

    arg1 = sys.argv[1].lower()
    if arg1 in COLOR_MAP:
        r, g, b = COLOR_MAP[arg1]
        brightness = int(sys.argv[2]) if len(sys.argv) >= 3 else 100
    else:
        if len(sys.argv) < 4:
            print(f"[-] 未知颜色名 '{arg1}'，请提供 R G B 三个数值。")
            sys.exit(1)
        try:
            r = int(sys.argv[1])
            g = int(sys.argv[2])
            b = int(sys.argv[3])
            brightness = int(sys.argv[4]) if len(sys.argv) >= 5 else 100
        except ValueError:
            print("[-] RGB 与亮度参数必须为整数数值！")
            sys.exit(1)

    set_color(r, g, b, brightness)

if __name__ == "__main__":
    main()
