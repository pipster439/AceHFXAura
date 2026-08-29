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

# Key mapping from Armoury Crate standard key table
# Let's inspect buttons
print(f"Total buttons in config: {len(buttons)}")
# Print samples:
samples = list(buttons.items())[:10]
for name, data in samples:
    print(name, "->", data.get('defaultKey'), data.get('button', {}).get('target_key'))
