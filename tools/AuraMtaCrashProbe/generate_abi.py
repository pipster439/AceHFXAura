"""Pinned canonical slot prefixes, without device or ownership method declarations."""
import hashlib, re, sys
from pathlib import Path
EXPECTED = '2672030e2439e99d2397fa8da9fa13a92cff08f42851cb13b69cd31ebec83efb'
def generate(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('Canonical ABI drift')
    text = data.decode('utf-8-sig')
    result = '#pragma once\n#include <windows.h>\n#include <oaidl.h>\n'
    result += re.search(r'DEFINE_GUID\(CLSID_AuraSdk,.*?\);', text, re.S)[0] + '\n'
    result += 'struct IAuraSyncDeviceCollection;\n'
    for name, methods in [('IAuraSyncDeviceCollection', ['get__NewEnum','get_Count']), ('IAuraSdk',['Enumerate'])]:
        m = re.search(r'(MIDL_INTERFACE\("[^"\n]+"\)\s*'+name+r'\s*: public IDispatch\s*\{\s*public:)(.*?)(\};)',text,re.S)
        declarations = re.findall(r'virtual\s+.*?= 0;',m[2],re.S)[:len(methods)]
        if [re.search(r'STDMETHODCALLTYPE\s+(\w+)\s*\(',d)[1] for d in declarations] != methods:
            raise ValueError('Prefix drift')
        result += m[1]+'\n'+'\n'.join(declarations)+'\n};\n'
    iid = re.search(r'MIDL_INTERFACE\("([^"\n]+)"\)\s*IAuraSdk2\s*: public IAuraSdk',text)[1]
    return result+'MIDL_INTERFACE("'+iid+'")\nVersion2Prefix : public IAuraSdk {};\n'
if __name__ == '__main__':
    Path(sys.argv[2]).write_text(generate(Path(sys.argv[1]).read_bytes()),encoding='utf-8')
