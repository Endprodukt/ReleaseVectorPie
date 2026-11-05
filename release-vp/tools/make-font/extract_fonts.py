"""extract_fonts.py — save the fonts in the Android APK's Unity data as .ttf.

    python extract_fonts.py <folder with the APK's assets/bin/Data files> <out folder>

Needs UnityPy. The game's font is AunchantedXspace (regular and bold).
"""
import os
import sys
import UnityPy

env = UnityPy.load(sys.argv[1])
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
for o in env.objects:
    if o.type.name != "Font":
        continue
    tree = o.read_typetree()
    data = tree.get("m_FontData") or []
    print(tree.get("m_Name"), len(data), "bytes", tree.get("m_FontNames"))
    if data:
        with open(os.path.join(out, tree["m_Name"] + ".ttf"), "wb") as f:
            f.write(bytes(b & 0xFF for b in data))
