"""Test rastertotspl: sinh CUPS raster giả lập, chạy filter, giải mã BITMAP TSPL ra PNG để so."""
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def header(w, h, dpi, bpp, cspace, page_pt, ints=(), big_endian=False):
    e = ">" if big_endian else "<"
    b = bytearray(1796)
    put = lambda off, v: struct.pack_into(e + "I", b, off, v)
    put(276, dpi); put(280, dpi)
    put(340, 1)
    put(352, int(page_pt[0])); put(356, int(page_pt[1]))
    put(372, w); put(376, h)
    put(384, bpp); put(388, bpp)
    put(392, (w * bpp + 7) // 8)
    put(396, 0); put(400, cspace)
    put(420, 1)
    struct.pack_into(e + "ff", b, 428, *page_pt)
    for i, v in enumerate(ints):
        put(452 + 4 * i, v)
    return bytes(b)


def compress_line(line, unit):
    """CUPS raster v2 PackBits (pixel unit = 1 byte cho 1/8 bit)."""
    out = bytearray()
    px = [line[i:i + unit] for i in range(0, len(line), unit)]
    i = 0
    while i < len(px):
        j = i
        while j + 1 < len(px) and px[j + 1] == px[i] and j - i < 127:
            j += 1
        if j > i:
            out.append(j - i)
            out += px[i]
            i = j + 1
        else:
            k = i
            while k + 1 < len(px) and px[k + 1] != px[k] and k - i < 127:
                k += 1
            n = k - i + 1
            if n == 1:  # mã 257-1=256 không tồn tại -> dùng lặp 1 lần
                out.append(0)
                out += px[i]
                i += 1
                continue
            out.append(257 - n)
            for p in px[i:k + 1]:
                out += p
            i = k + 1
    return bytes(out)


def make_image(w, h):
    """Ảnh xám 8-bit: khung, sọc kiểu barcode, dải gradient."""
    img = [[255] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if x < 4 or y < 4 or x >= w - 4 or y >= h - 4:
                img[y][x] = 0
            elif h // 4 < y < h // 2 and (x // 6) % 3 == 0:
                img[y][x] = 0
            elif y > 2 * h // 3:
                img[y][x] = int(255 * x / w)
    return img


def raster(pages, version=2, big_endian=False):
    sync = {1: b"RaSt", 2: b"RaS2", 3: b"RaS3"}[version]
    out = bytearray(sync if big_endian else sync[::-1])
    for img, bpp, cspace, page_pt, ints in pages:
        h, w = len(img), len(img[0])
        hdr = header(w, h, 203, bpp, cspace, page_pt, ints, big_endian)
        out += hdr[:420] if version == 1 else hdr
        prev, rep, buf = None, 0, bytearray()
        for row in img:
            if bpp == 8:
                line = bytes(v if cspace == 0 else 255 - v for v in row)
            else:
                line = bytearray((w + 7) // 8)
                for x, v in enumerate(row):
                    if v < 128:
                        line[x >> 3] |= 0x80 >> (x & 7)
                if cspace == 0:
                    line = bytes(~c & 0xFF for c in line)
                line = bytes(line)
            if version != 2:
                out += line
                continue
            if line == prev and rep < 255:
                rep += 1
                continue
            if prev is not None:
                out.append(rep); out += compress_line(prev, 1)
            prev, rep = line, 0
        if version == 2:
            out.append(rep); out += compress_line(prev, 1)
    return bytes(out)


def parse_tspl(data):
    """Tách các BITMAP trong output -> list (cmds, width_bytes, height, bits)."""
    pages, i, cmds = [], 0, []
    while i < len(data):
        if data.startswith(b"BITMAP ", i):
            j = i + 7
            parts = []
            for _ in range(5):
                k = data.index(b",", j)
                parts.append(int(data[j:k])); j = k + 1
            wb, h = parts[2], parts[3]
            bits = data[j:j + wb * h]
            i = j + wb * h
            pages.append((cmds, wb, h, bits)); cmds = []
            continue
        k = data.index(b"\r\n", i)
        cmds.append(data[i:k].decode()); i = k + 2
    return pages, cmds


def png(path, wb, h, bits):
    rows = b"".join(b"\x00" + bits[y * wb:(y + 1) * wb] for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", wb * 8, h, 1, 0, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def expected_bits(img, wb):
    """BITMAP mong đợi khi Threshold: bit 0 = đen."""
    out = bytearray()
    for row in img:
        line = bytearray(b"\xff" * wb)
        for x, v in enumerate(row):
            if v < 128:
                line[x >> 3] &= ~(0x80 >> (x & 7)) & 0xFF
        out += line
    return bytes(out)


def main():
    exe, outdir = sys.argv[1], Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    w, h = 609, 1039  # 76x130mm @203dpi, w lẻ để thử bit đệm
    pt = (76 * 72 / 25.4, 130 * 72 / 25.4)
    img = make_image(w, h)
    wb = (w + 7) // 8
    exp = expected_bits(img, wb)
    cases = {
        "v2_gray8_W": raster([(img, 8, 0, pt, (5, 11, 1, 2))]),
        "v2_gray8_K_bigendian": raster([(img, 8, 3, pt, ())], big_endian=True),
        "v3_1bit_K": raster([(img, 1, 3, pt, ())], version=3),
        "v1_1bit_W": raster([(img, 1, 0, pt, ())], version=1),
        "v2_two_pages": raster([(img, 8, 0, pt, ()), (img, 8, 0, pt, ())]),
    }
    ok = True
    for name, data in cases.items():
        r = subprocess.run([exe, "1", "u", "t", "1", "", "-"][:6], input=data, capture_output=True)
        pages, tail = parse_tspl(r.stdout)
        good = r.returncode == 0 and pages and all(p[1] == wb and p[2] == h and p[3] == exp for p in pages)
        ok &= bool(good)
        print(f"{'OK ' if good else 'FAIL'} {name}: rc={r.returncode} pages={len(pages)} cmds={pages[0][0] if pages else ''}")
        if not good:
            print(r.stderr.decode(errors="replace"))
        if pages:
            png(outdir / f"{name}.png", *pages[0][1:])
    # dither + option override qua argv[5]
    r = subprocess.run([exe, "1", "u", "t", "1", "Halftone=Dither Speed=3 Density=12 MediaTracking=BlackMark"],
                       input=cases["v2_gray8_W"], capture_output=True)
    pages, _ = parse_tspl(r.stdout)
    good = r.returncode == 0 and len(pages) == 1 and "SPEED 3" in pages[0][0] and "DENSITY 12" in pages[0][0] \
        and any(c.startswith("BLINE") for c in pages[0][0])
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} dither+options: {pages[0][0] if pages else r.stderr}")
    if pages:
        png(outdir / "dither.png", *pages[0][1:])
    print("ALL OK" if ok else "SOME FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
