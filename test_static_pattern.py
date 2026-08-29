import urllib.request
import json
import socket

def find_framework_base():
    ports_to_test = [8803, 1042, 8804, 8802]
    for p in ports_to_test:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.2)
            res = s.connect_ex(('127.0.0.1', p))
            s.close()
            if res == 0:
                return f"http://127.0.0.1:{p}"
        except Exception:
            pass
    return "http://127.0.0.1:8803"

base_url = find_framework_base()
url = f"{base_url}/type/2/model/7038/lighting"

# Test: effectID 0 (Statics) with Pattern
payload = {
    "profileID": "3",
    "isPreview": True,
    "data": {
        "switchStatus": True,
        "keyboard": {
            "effectID": "0",       # Statics
            "colorType": "Pattern",
            "backgroundType": "Off",
            "backgroundColor": {"key": "1", "r": "0", "g": "0", "b": "0"},
            "singleMulti": "0",
            "brightness": "100",
            "analogEffect": "0",
            "speed": "0",
            "direction": "-1",
            "random": "-1",
            "width": "-1",
            "pattern": {
                "key": "1",
                "separateNumber": 4,
                "separates": [
                    {"key": "1", "location": "0",  "r": "0",   "g": "255", "b": "0"},     # Green
                    {"key": "2", "location": "30", "r": "255", "g": "0",   "b": "0"},     # Red
                    {"key": "3", "location": "65", "r": "0",   "g": "150", "b": "255"},   # Blue
                    {"key": "4", "location": "95", "r": "255", "g": "255", "b": "0"}      # Yellow
                ]
            }
        }
    }
}

req_data = json.dumps(payload).encode('utf-8')
req = urllib.request.Request(url, data=req_data, headers={'Content-Type': 'application/json'}, method='PUT')

print(f"[*] Sending Static + Pattern to {url}...")
try:
    with urllib.request.urlopen(req, timeout=3) as resp:
        print(f"[+] Status: {resp.status}, Response: {resp.read().decode('utf-8')}")
except Exception as e:
    print(f"[-] Error: {e}")
