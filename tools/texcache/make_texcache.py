#!/usr/bin/env python3
"""
Builds the loader's texture cache on a PC from your own APK, so the Vita
never has to convert a texture (first start loads like later ones).

The loader converts ETC2 textures to DXT5 and keeps the result in
ux0:data/ninjaarashi/texcache/<crc32 of the ETC2 data>_<w>x<h>.dxt5. This
script does the same conversion with vitaGL's ETC2 decoder and stb_dxt.

Needs: Python 3 with UnityPy (pip install UnityPy), a C compiler.
Usage: tools/texcache/make_texcache.py <game.apk> <output dir>
Copy the output's texcache folder to ux0:data/ninjaarashi/.
"""
import os
import re
import subprocess
import sys
import tempfile
import zipfile
import zlib

import UnityPy

HERE = os.path.dirname(os.path.abspath(__file__))
VGL = os.path.join(HERE, "..", "..", "lib", "vitagl", "source", "utils")


def build_converter(tmp):
    inc = os.path.join(tmp, "inc")
    os.makedirs(inc)
    with open(os.path.join(inc, "shared.h"), "w") as f:
        f.write('#include <string.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include "eac_utils.h"\n'
                "#define vgl_fast_memcpy memcpy\n")
    with open(os.path.join(inc, "mem_utils.h"), "w") as f:
        f.write("#include <string.h>\n#define vgl_memset memset\n#define vgl_fast_memcpy memcpy\n")
    srcs = []
    for name in ("eac_utils.c", "etc_utils.c"):
        with open(os.path.join(VGL, name)) as f:
            code = f.read().replace('#include "../shared.h"', '#include "shared.h"')
        path = os.path.join(tmp, name)
        with open(path, "w") as f:
            f.write(code)
        srcs.append(path)
    for name in ("eac_utils.h", "stb_dxt.h"):
        with open(os.path.join(VGL, name)) as f, open(os.path.join(inc, name), "w") as g:
            g.write(f.read())
    exe = os.path.join(tmp, "conv")
    subprocess.run(["cc", "-O2", "-I" + inc, "-o", exe, os.path.join(HERE, "conv.c")] + srcs + ["-lm"], check=True)
    return exe


def main():
    apk, out = sys.argv[1], sys.argv[2]
    cache = os.path.join(out, "texcache")
    os.makedirs(cache, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        conv = build_converter(tmp)
        data_dir = os.path.join(tmp, "Data")
        os.makedirs(data_dir)
        # unpack assets/bin/Data, joining name.splitN files
        with zipfile.ZipFile(apk) as z:
            parts = {}
            for n in z.namelist():
                if not n.startswith("assets/bin/Data/") or n.endswith("/"):
                    continue
                rel = n[len("assets/bin/Data/"):]
                if "/" in rel and not rel.startswith("Resources/"):
                    continue
                m = re.match(r"(.*)\.split(\d+)$", rel)
                if m:
                    parts.setdefault(m.group(1), []).append((int(m.group(2)), n))
                else:
                    dst = os.path.join(data_dir, rel)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    with open(dst, "wb") as f:
                        f.write(z.read(n))
            for base, lst in parts.items():
                with open(os.path.join(data_dir, base), "wb") as f:
                    for _, n in sorted(lst):
                        f.write(z.read(n))
        env = UnityPy.load(data_dir)
        done = 0
        for obj in env.objects:
            if obj.type.name != "Texture2D":
                continue
            try:
                t = obj.read()
            except Exception:
                continue
            fmt, w, h = int(t.m_TextureFormat), t.m_Width, t.m_Height
            if fmt not in (34, 45, 47) or not (64 <= w <= 2048 and 64 <= h <= 2048):
                continue
            if w & (w - 1) or h & (h - 1):
                continue
            data = bytes(t.image_data)
            blocks = (w // 4) * (h // 4)
            if fmt == 47:
                etc = data[: w * h]
            else:  # ETC1 / ETC2 RGB: opaque EAC alpha block in front of each block, as the loader does
                rgb, b = data[: blocks * 8], bytearray(blocks * 16)
                for i in range(blocks):
                    b[i * 16] = 255
                    b[i * 16 + 8: i * 16 + 16] = rgb[i * 8: i * 8 + 8]
                etc = bytes(b)
            if len(etc) < w * h:
                continue
            name = "%08x_%dx%d.dxt5" % (zlib.crc32(etc[: w * h]) & 0xFFFFFFFF, w, h)
            dst = os.path.join(cache, name)
            if os.path.exists(dst):
                continue
            src = os.path.join(tmp, "in.etc")
            with open(src, "wb") as f:
                f.write(etc[: w * h])
            subprocess.run([conv, src, str(w), str(h), dst], check=True)
            done += 1
        print("%d textures written to %s" % (done, cache))


if __name__ == "__main__":
    main()
