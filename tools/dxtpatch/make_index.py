#!/usr/bin/env python3
"""
Lists the ETC2 RGBA8 textures of the game's asset files for na_dxtpatch.c,
which turns them into DXT5 on the Vita (extras/dxt_index.txt).

Usage: make_index.py <folder with the joined asset files> <out.txt>

The folder holds the files of assets/bin/Data from the APK, with split
files (name.split0, .split1, ...) joined into one (name). Needs UnityPy.

Each line: file, offset and size of the image data, offset of the
m_TextureFormat field, width, height, mip count, CRC32 of the image data.
Offsets are in the joined file.
"""
import os
import struct
import sys
import zlib

import UnityPy

ETC2_RGBA8 = 47


def main():
    root, out_path = sys.argv[1], sys.argv[2]
    env = UnityPy.load(root)
    n = total = 0
    with open(out_path, 'w') as out:
        for obj in env.objects:
            if obj.type.name != 'Texture2D':
                continue
            t = obj.read()
            if int(t.m_TextureFormat) != ETC2_RGBA8:
                continue
            raw = obj.get_raw_data()
            img = bytes(t.image_data)
            pos = raw.find(img[:256])
            assert pos >= 0 and raw[pos:pos + len(img)] == img
            # m_TextureFormat follows m_Width, m_Height, m_CompleteImageSize
            # (and m_MipsStripped in newer versions)
            fp = raw.find(struct.pack('<ii', t.m_Width, t.m_Height))
            cand = [o for o in (fp + 12, fp + 16) if struct.unpack('<i', raw[o:o + 4])[0] == ETC2_RGBA8]
            assert len(cand) == 1
            name = obj.assets_file.name
            a_img, a_fmt = obj.byte_start + pos, obj.byte_start + cand[0]
            with open(os.path.join(root, name), 'rb') as f:
                f.seek(a_img)
                assert f.read(len(img)) == img, name
                f.seek(a_fmt)
                assert struct.unpack('<i', f.read(4))[0] == ETC2_RGBA8
            assert len(img) % 16 == 0
            out.write('%s %d %d %d %d %d %d %08x\n' % (name, a_img, len(img), a_fmt, t.m_Width, t.m_Height,
                                                       t.m_MipCount, zlib.crc32(img)))
            n += 1
            total += len(img)
    print('%d textures, %d KB' % (n, total // 1024))


if __name__ == '__main__':
    main()
