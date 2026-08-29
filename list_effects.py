import re

with open("C:/Program Files (x86)/ASUS/ArmouryDevice/View/7038/service.js", "r", encoding="utf-8", errors="ignore") as f:
    text = f.read()

pos = text.find("effectList:")
end = text.find("capsFunctionList:", pos)
chunk = text[pos:end]

for m in re.finditer(r'id:"(\d+)",text:"([^"]+)",className:"([^"]+)"', chunk):
    print(f"Effect ID {m.group(1)}: {m.group(3)}")
