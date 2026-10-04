"""Extract exact read-only prefixes from the already reviewed canonical ABI."""
import hashlib
import re
from pathlib import Path

EXPECTED = "2672030e2439e99d2397fa8da9fa13a92cff08f42851cb13b69cd31ebec83efb"
PREFIXES = {
    "IAuraSdk": ["Enumerate"],
    "IAuraSyncDevice": ["get_Lights", "get_Type", "get_Name", "get_Width", "get_Height"],
    "IAuraSyncDeviceCollection": ["get__NewEnum", "get_Count", "get_Item"],
    "IAuraRgbLightCollection": ["get__NewEnum", "get_Count"],
}

def generate(source):
    if hashlib.sha256(source).hexdigest() != EXPECTED:
        raise ValueError("Canonical ABI drift requires review")
    text = source.decode("utf-8-sig")
    blocks = {}
    for name, methods in PREFIXES.items():
        match = re.search(r'(MIDL_INTERFACE\("[^"\n]+"\)\s*' + name + r'\s*: public IDispatch\s*\{\s*public:)(.*?)(\};)', text, re.S)
        if not match:
            raise ValueError(f"Missing canonical block {name}")
        declarations = re.findall(r'virtual\s+.*?= 0;', match[2], re.S)
        selected = declarations[:len(methods)]
        actual = [re.search(r'STDMETHODCALLTYPE\s+(\w+)\s*\(', item)[1] for item in selected]
        if actual != methods:
            raise ValueError(f"Canonical prefix mismatch {name}")
        blocks[name] = match[1] + "\n    " + "\n    ".join(selected) + "\n};\n"
    guid = re.search(r'DEFINE_GUID\(CLSID_AuraSdk,.*?\);', text, re.S)[0]
    output = "#pragma once\n#include <windows.h>\n#include <oaidl.h>\n" + guid + "\n"
    output += "\n".join("struct " + name + ";" for name in PREFIXES) + "\n"
    output += "\n".join(blocks.values())
    for name in sorted(PREFIXES, key=len, reverse=True):
        output = re.sub(r'\b' + name + r'\b', "ReadOnly" + name, output)
    # Version 2 inherits the exact same base prefix. Expose only that prefix, never its appended slot.
    version2 = re.search(r'MIDL_INTERFACE\("([^"\n]+)"\)\s*IAuraSdk2\s*: public IAuraSdk', text)
    if not version2:
        raise ValueError("Missing canonical inherited IID")
    output += '\nMIDL_INTERFACE("' + version2[1] + '")\nReadOnlyVersion2Prefix : public ReadOnlyIAuraSdk {};\n'
    return output

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        raise SystemExit("Expected canonical input and generated output from CMake")
    Path(sys.argv[2]).write_text(generate(Path(sys.argv[1]).read_bytes()), encoding="utf-8")
