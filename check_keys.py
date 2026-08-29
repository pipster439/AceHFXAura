import json

with open(r"g:\Aura\falchion_keymap.json", encoding="utf-8") as f:
    d = json.load(f)

ids = [18, 32, 33, 34, 92, 87, 86, 93, 1, 15, 22, 23, 11, 68, 125]
for k, v in d.items():
    lid = v.get("led_id")
    if lid in ids:
        print(f"LED_ID {lid:3d} -> Key: {k:10s} (Name: {v.get('name')}) col={v.get('col')}, row={v.get('row')}")
