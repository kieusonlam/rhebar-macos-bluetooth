"""Đóng gói RHEBAR-Bluetooth-macOS.zip (tên entry dùng '/', giữ quyền thực thi cho Mac)."""
import base64
import sys
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

root = Path(__file__).resolve().parent.parent
out = root / "build" / "RHEBAR-Bluetooth-macOS.zip"
top = "RHEBAR-Bluetooth-macOS"
files = [
    ("bt-package/cai-bluetooth.sh", "cai-bluetooth.sh", 0o755),
    ("bt-package/go-bluetooth.sh", "go-bluetooth.sh", 0o755),
    ("bt-package/HUONG-DAN-BLUETOOTH.txt", "HUONG-DAN-BLUETOOTH.txt", 0o644),
    ("src/btbridge.c", "src/btbridge.c", 0o644),
]


def installer():
    """cai-bluetooth.sh + chương trình cầu nối base64 nhúng cuối file (dòng 76 ký tự)."""
    text = (root / "bt-package/cai-bluetooth.sh").read_bytes().replace(b"\r\n", b"\n")
    for tag, binary in (("AARCH64", "rhebar-btbridge-aarch64"), ("X86_64", "rhebar-btbridge-x86_64")):
        b64 = base64.b64encode((root / "build/bin" / binary).read_bytes())
        lines = b"\n".join(b64[i:i + 76] for i in range(0, len(b64), 76))
        text += f"__BIN_{tag}__\n".encode() + lines + f"\n__END_{tag}__\n".encode()
    return text


with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for src, name, mode in files:
        data = installer() if name == "cai-bluetooth.sh" else (root / src).read_bytes()
        if name.endswith(".sh"):
            data = data.replace(b"\r\n", b"\n")
        info = zipfile.ZipInfo(f"{top}/{name}", date_time=(2026, 10, 22, 0, 0, 0))
        info.create_system = 3  # Unix -> giữ mode
        info.external_attr = (0o100000 | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
print(f"{out} ({out.stat().st_size // 1024} KB)")
