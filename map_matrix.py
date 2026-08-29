import xml.etree.ElementTree as ET
import base64
import urllib.parse
import json

tree = ET.parse(r'C:\ProgramData\ASUS\Framework\keyboard\ROG FALCHION ACE HFX\fp_3_config_025121610291.xml')
root = tree.getroot()
file_data = root.find('.//file_data').text
b64_decoded = base64.b64decode(file_data).decode('utf-8')
url_decoded = urllib.parse.unquote(b64_decoded)
config = json.loads(url_decoded)

buttons = config.get('button', {}).get('keyboardButton', {})

# Filter only col < 50
print("=== All Primary Keys (col < 50) ===")
keys_list = []
for k, v in buttons.items():
    col, row = [int(x) for x in k.replace('keyfunction_', '').split('_')]
    if col < 50:
        def_key = v.get('defaultKey')
        led_id = col * 8 + row
        keys_list.append((col, row, led_id, def_key))

keys_list.sort(key=lambda x: (x[1], x[0])) # Sort by row, then col
for col, row, led_id, def_key in keys_list:
    print(f"row={row}, col={col:2d} -> LED_ID={led_id:3d} (defaultKey={def_key})")
