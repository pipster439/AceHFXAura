import json

with open(r'g:\Aura\falchion_keymap.json', encoding='utf-8') as f:
    d = json.load(f)

# Group keys by row
# Note: In falchion_keymap.json, let's see how keys are grouped
keys_list = list(d.items())
# First 15 are Row 1
row1 = keys_list[0:15]
# Next 15 are Row 2
row2 = keys_list[15:30]
# Next 14 are Row 3
row3 = keys_list[30:44]
# Next 14 are Row 4
row4 = keys_list[44:58]
# Next 10 are Row 5
row5 = keys_list[58:68]

print("=== ROW 1 ===")
for k, v in row1: print(k, v['led_id'], end=' | ')
print("\n=== ROW 2 ===")
for k, v in row2: print(k, v['led_id'], end=' | ')
print("\n=== ROW 3 ===")
for k, v in row3: print(k, v['led_id'], end=' | ')
print("\n=== ROW 4 ===")
for k, v in row4: print(k, v['led_id'], end=' | ')
print("\n=== ROW 5 ===")
for k, v in row5: print(k, v['led_id'], end=' | ')
print()
