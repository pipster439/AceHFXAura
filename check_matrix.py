with open(r'C:\Program Files (x86)\ASUS\ArmouryDevice\modules\keyboard\index.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

pos = text.find('setMatrixEffect:')
print(text[pos:pos+1500])
