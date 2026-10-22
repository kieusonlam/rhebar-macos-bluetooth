"""Máy in TSPL giả lập trên pty, dùng để test rhebar-btbridge với lệnh in nhiều trang.

Mặc định bắt chước máy in bộ đệm nhỏ: trong lúc đang in một trang (PRINT_S giây) chỉ
nhận thêm BUF_LIMIT byte, phần vượt bị rơi. --nodrop: bộ đệm đủ lớn, không rơi byte nào.
Trả lời <ESC>!? bằng 1 byte trạng thái (0x20 = đang in) trừ khi chạy với --silent.

Usage: python3 sim_printer.py <link-path> <out-file> [--silent] [--nodrop]
"""
import os
import sys
import time
import tty
import select

PRINT_S = 1.0
BUF_LIMIT = 2048
QUERY = b"\x1b!?"


def main():
    link, out, silent = sys.argv[1], sys.argv[2], "--silent" in sys.argv
    nodrop = "--nodrop" in sys.argv
    master, slave = os.openpty()
    tty.setraw(slave)
    if os.path.lexists(link):
        os.unlink(link)
    os.symlink(os.ttyname(slave), link)

    kept = bytearray()      # dữ liệu máy in thực sự nhận được (không tính lệnh hỏi)
    pending = b""
    printing_until = 0.0
    taken_while_printing = 0
    pages = 0
    line = bytearray()
    with open(out + ".ready", "w"):
        pass
    while True:
        r, _, _ = select.select([master], [], [], 0.05)
        if not r:
            continue
        data = pending + os.read(master, 65536)
        pending = b""
        # tách lệnh hỏi trạng thái (có thể bị cắt giữa 2 lần đọc)
        while True:
            k = data.find(QUERY)
            if k < 0:
                break
            busy = time.monotonic() < printing_until
            if not silent:
                os.write(master, b"\x20" if busy else b"\x00")
            data = data[:k] + data[k + 3:]
        for cut in (2, 1):
            if data.endswith(QUERY[:cut]):
                pending, data = data[-cut:], data[:-cut]
                break
        for b in data:
            now = time.monotonic()
            if now < printing_until and not nodrop:
                if taken_while_printing >= BUF_LIMIT:
                    continue  # tràn bộ đệm -> rơi byte
                taken_while_printing += 1
            kept.append(b)
            if b == 0x0A:
                s = bytes(line).rstrip(b"\r")
                p = s.rfind(b"PRINT")
                if p >= 0 and all(c in b" ,0123456789" for c in s[p + 5:]):
                    pages += 1
                    printing_until = time.monotonic() + PRINT_S
                    taken_while_printing = 0
                    with open(out, "wb") as f:
                        f.write(kept)
                    with open(out + ".pages", "w") as f:
                        f.write(str(pages))
                line.clear()
            else:
                line.append(b)
                if len(line) > 64:
                    del line[:-32]


if __name__ == "__main__":
    main()
