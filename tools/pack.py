"""Đóng gói RHEBAR-macOS-driver.zip (tên entry dùng '/', giữ quyền thực thi cho Mac)."""
import sys
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

root = Path(__file__).resolve().parent.parent
out = root / "build" / "RHEBAR-macOS-driver.zip"
top = "RHEBAR-macOS-driver"

files = [
    (root / "package/install.sh", "install.sh", 0o755),
    (root / "package/uninstall.sh", "uninstall.sh", 0o755),
    (root / "package/HUONG-DAN.txt", "HUONG-DAN.txt", 0o644),
    (root / "build/bin/rastertotspl-aarch64", "bin/rastertotspl-aarch64", 0o755),
    (root / "build/bin/rastertotspl-x86_64", "bin/rastertotspl-x86_64", 0o755),
    (root / "src/rastertotspl.c", "src/rastertotspl.c", 0o644),
]
files += [(p, f"PPDs/{p.name}", 0o644) for p in sorted((root / "build/PPDs").glob("*.ppd"))]

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for src, name, mode in files:
        data = src.read_bytes()
        if name.endswith(".sh"):
            data = data.replace(b"\r\n", b"\n")
        info = zipfile.ZipInfo(f"{top}/{name}", date_time=(2026, 10, 22, 0, 0, 0))
        info.create_system = 3  # Unix -> giữ mode
        info.external_attr = (0o100000 | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
print(f"{out} ({out.stat().st_size // 1024} KB, {len(files)} files)")
