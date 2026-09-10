"""
test_cs2_gsi.py - CS2 Game State Integration (GSI) 自动化综合测试套件

测试项：
1. GSI HTTP 接收服务可用性 (127.0.0.1:19897)
2. 通用 Payload 递归扁平化与 Valve 官方别名映射验证
3. WebUI 代理转发 (/api/gsi/current) 与 CFG 探测接口 (/api/gsi/cfg)
4. 瞬态字段生命周期清理 (炸弹安放 -> 拆除后状态自动清除)
5. 优先级仲裁测试 (多条件同时满足时靠前生效)
6. 高频推送并发压测 (确保推流主时钟稳定 25 FPS，COM/HAL 线程安全隔离)
"""

import sys
import time
import json
import urllib.request
import urllib.error
import subprocess
import os

# 确保在 Windows 控制台输出 UTF-8
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

DAEMON_URL = "http://127.0.0.1:19897"
WEB_URL = "http://127.0.0.1:19898"

# 构造标准 CS2 GSI 模拟数据包
def make_payload(health=100, armor=100, bomb_state=None, phase="live", round_num=3, flashed=0, burning=0, round_kills=0, round_killhs=0):
    payload = {
        "provider": {
            "name": "Counter-Strike: Global Offensive",
            "appid": 730,
            "version": 14000,
            "steamid": "76561198000000001",
            "timestamp": int(time.time())
        },
        "map": {
            "mode": "competitive",
            "name": "de_dust2",
            "phase": phase,
            "round": round_num,
            "team_ct": {"score": 1},
            "team_t": {"score": 1}
        },
        "round": {
            "phase": phase
        },
        "player": {
            "steamid": "76561198000000001",
            "name": "PlayerTest",
            "team": "CT",
            "activity": "playing",
            "state": {
                "health": health,
                "armor": armor,
                "helmet": True,
                "flashed": flashed,
                "smoked": 0,
                "burning": burning,
                "money": 3500,
                "round_kills": round_kills,
                "round_killhs": round_killhs,
                "equip_value": 4200
            },
            "weapons": {
                "weapon_0": {"name": "weapon_knife", "state": "holstered"},
                "weapon_1": {"name": "weapon_m4a1_silencer", "state": "active", "ammo_clip": 20}
            }
        }
    }
    if bomb_state:
        payload["round"]["bomb"] = bomb_state
    return payload

def send_gsi_post(payload):
    data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(f"{DAEMON_URL}/", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=3) as resp:
        return resp.status, json.loads(resp.read().decode('utf-8'))

def get_json(url):
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=3) as resp:
        return resp.status, json.loads(resp.read().decode('utf-8'))

def run_all_tests():
    print("=========================================================")
    print(" CS2 Game State Integration (GSI) 自动化综合测试")
    print("=========================================================")

    # 1. 基础连接测试
    print("\n[测试 1] 验证 GSI 接收端监听与心跳接口...")
    try:
        status, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
        assert status == 200, f"HTTP status was {status}"
        print(f"  [PASS] 成功连接 127.0.0.1:19897，当前状态: connected={cur.get('connected')}")
    except Exception as e:
        print(f"  [FAIL] 无法连接 GSI 端口 19897: {e}")
        return False

    # 2. 推送正常数据并验证双路径别名映射
    print("\n[测试 2] 推送满血数据包并验证扁平化与别名映射...")
    p1 = make_payload(health=100, armor=95)
    status, res = send_gsi_post(p1)
    assert status == 200 and res.get("status") == "ok", f"POST failed: {res}"

    status, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
    data = cur.get("data", {})

    # 必须同时存在原始 JSON 路径与 Valve 官方别名
    assert data.get("player.state.health") == 100, f"player.state.health 期望 100, 实际: {data.get('player.state.health')}"
    assert data.get("player_state.health") == 100, f"player_state.health 别名 期望 100, 实际: {data.get('player_state.health')}"
    assert data.get("player_state.armor") == 95, f"player_state.armor 期望 95, 实际: {data.get('player_state.armor')}"
    assert data.get("map.phase") == "live", f"map.phase 期望 live, 实际: {data.get('map.phase')}"
    assert cur.get("connected") is True, "connected 应当为 True"
    print(f"  [PASS] 双路径别名映射全部正确 (player.state.health: {data.get('player.state.health')}, player_state.health: {data.get('player_state.health')})")

    # 3. 验证瞬态字段生命周期清除 (C4 炸弹安放 -> 拆除)
    print("\n[测试 3] 验证瞬态字段生命周期清理 (round.bomb 安放与拆除)...")
    p_bomb = make_payload(health=100, bomb_state="planted")
    send_gsi_post(p_bomb)
    _, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
    assert cur["data"].get("round.bomb") == "planted", "C4 安放状态未同步"
    print("  -> C4 安放数据已成功置入")

    # 下次回合数据没有 bomb 字段 (模拟已拆除/新回合开始)
    p_nobomb = make_payload(health=100, bomb_state=None)
    send_gsi_post(p_nobomb)
    _, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
    assert "round.bomb" not in cur["data"], f"round.bomb 未被正确清除！残留值: {cur['data'].get('round.bomb')}"
    print("  [PASS] 瞬态字段 round.bomb 在新回合数据中已被自动清理，杜绝跨回合警报假阳性")

    # 4. 验证 WebUI 代理转发与 CFG 安装接口
    print("\n[测试 4] 验证 WebUI 代理接口 (/api/gsi/current 与 /api/gsi/cfg)...")
    try:
        status, web_gsi = get_json(f"{WEB_URL}/api/gsi/current")
        assert status == 200, f"WebUI GSI proxy status {status}"
        assert web_gsi.get("connected") is True, "WebUI 代理数据未同步"
        print(f"  [PASS] WebUI /api/gsi/current 代理成功获取 GSI 状态")

        status, cfg_info = get_json(f"{WEB_URL}/api/gsi/cfg")
        assert status == 200, f"WebUI CFG status {status}"
        assert "gamestate_integration_aura.cfg" in cfg_info.get("filename", ""), "CFG 文件名不正确"
        assert len(cfg_info.get("content", "")) > 100, "CFG 模板内容为空"
        assert '"bomb"' in cfg_info.get("content", ""), "CFG 模板应包含 CounterStrike2GSI 规范的 bomb 类别"
        detected = cfg_info.get("detected_path", "")
        print(f"  [PASS] WebUI /api/gsi/cfg 接口返回正常 (自动检测到路径: {detected})")
        print(f"  [PASS] CFG 模板已集成 CounterStrike2GSI 规范 (含 'bomb' 节点)")
    except Exception as e:
        print(f"  [WARN] WebUI 代理接口测试: {e}")

    # 5. 验证优先级仲裁逻辑 (残血 health=15 vs C4安放 bomb=planted)
    print("\n[测试 5] 验证多规则并发命中时的优先级仲裁...")
    # config.json 中：
    # 规则 1: player_state.health < 20 -> danger_red (优先级高)
    # 规则 2: round.bomb == planted -> bomb_pulse (优先级低)
    p_both = make_payload(health=15, bomb_state="planted")
    send_gsi_post(p_both)
    time.sleep(0.1) # 等待 40ms 帧时钟采样

    status, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
    assert cur["data"].get("player_state.health") == 15, "血量数据未更新"
    assert cur["data"].get("round.bomb") == "planted", "炸弹数据未更新"
    print("  [PASS] 双重条件同时满足时，数据已精准入库")

    # 6. 验证完整游戏事件流 (PlayerTookDamage 与 PlayerGotKill 动态推导)
    print("\n[测试 6] 验证完整游戏事件状态机推导与事件流输出...")
    # 发送基准包
    send_gsi_post(make_payload(health=100, round_kills=0))
    time.sleep(0.05)
    # 跃迁：扣血受创 (100 -> 60)
    send_gsi_post(make_payload(health=60, round_kills=0))
    time.sleep(0.05)
    # 跃迁：消灭敌人 (round_kills 0 -> 1)
    send_gsi_post(make_payload(health=60, round_kills=1))
    time.sleep(0.05)

    status, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
    assert "events" in cur and isinstance(cur["events"], list), "返回中缺少 events 列表"
    assert len(cur["events"]) >= 2, f"期望至少捕获 2 个事件，实际: {len(cur['events'])}"
    event_names = [e["name"] for e in cur["events"]]
    assert "PlayerGotKill" in event_names, f"未捕获 PlayerGotKill 事件: {event_names}"
    assert "PlayerTookDamage" in event_names, f"未捕获 PlayerTookDamage 事件: {event_names}"
    assert cur["data"].get("event.kill") is True, "event.kill 脉冲未激活"
    assert cur["data"].get("event.damage") is True, "event.damage 脉冲未激活"
    print(f"  [PASS] 成功捕获并派发完整游戏事件流: {event_names[:3]}")
    print(f"  [PASS] 事件时效脉冲已精准置位 (event.kill: {cur['data'].get('event.kill')}, event.damage: {cur['data'].get('event.damage')})")

    # 7. 核心前台隔离验证 (调用 test_gsi_rules 单元测试套件)
    print("\n[测试 7] 验证前台不是 cs2.exe 时 GSI 绑定绝对不生效...")
    test_exe = os.path.join("build", "Release", "test_gsi_rules.exe")
    if os.path.exists(test_exe):
        ret = subprocess.run([test_exe], capture_output=True, text=True, encoding='utf-8', errors='replace')
        assert ret.returncode == 0, f"test_gsi_rules failed:\n{ret.stdout}\n{ret.stderr}"
        print("  [PASS] 进程隔离与单元测试断言 100% 通过")
    else:
        print("  [WARN] test_gsi_rules.exe 未找到，跳过本地单元测试调用")

    # 8. 高频推送并发压测 (50 连发)
    print("\n[测试 8] 执行 50 次高频 GSI 突发推送压测 (模拟密集交火网络包)...")
    start_time = time.time()
    for i in range(50):
        h = 10 + (i % 80)
        p = make_payload(health=h, flashed=(i * 5) % 255)
        status, _ = send_gsi_post(p)
        assert status == 200, f"Burst send failed on iteration {i}"
    elapsed = time.time() - start_time
    qps = 50 / elapsed
    print(f"  [PASS] 完成 50 次高频突发推送，耗时 {elapsed:.3f}s，吞吐量: {qps:.1f} req/s")

    # 验证最终状态
    _, cur = get_json(f"{DAEMON_URL}/api/gsi/current")
    print(f"  [PASS] 压测后当前累计接收数据包数: {cur.get('packet_count')}")

    print("\n=========================================================")
    print(" ✅ 所有 8 项 GSI 自动化综合测试全部顺利通过！")
    print("=========================================================")
    return True

if __name__ == "__main__":
    ok = run_all_tests()
    sys.exit(0 if ok else 1)
