with open(r'C:\Program Files (x86)\ASUS\ArmouryDevice\modules\keyboard\index.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
print("Matches:")
for m in re.findall(r'case"([^"]+)":', text):
    print(" ", m)
