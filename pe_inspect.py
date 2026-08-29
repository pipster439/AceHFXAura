data = open(r"C:\Program Files\ASUS\Aac_Keyboard\AacKbHal_x64.dll", "rb").read()

for s in [b"Set_L", b"Claymore", b"Falchion", b"Single", b"SINGLE"]:
    idx = 0
    found = 0
    while True:
        idx = data.find(s, idx)
        if idx == -1:
            break
        print(f"Found {s} at 0x{idx:X}")
        found += 1
        idx += len(s)
        if found > 5:
            break
