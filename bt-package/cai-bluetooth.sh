#!/bin/bash
# Thêm máy in nhãn RHEBAR/Beeprt qua Bluetooth trên macOS.
# Yêu cầu: đã cài RHEBAR-Driver_1.3.2.20.pkg và đã ghép đôi máy in trong Cài đặt > Bluetooth.
# Chạy: gõ "bash " vào Terminal, kéo file này vào, Enter.
# Tùy chọn --pace (thêm sau tên file): gửi từng trang, chờ in xong mới gửi tiếp —
# chỉ dùng nếu máy in vẫn bỏ trang ở chế độ mặc định.
# Chương trình cầu nối được nhúng sẵn cuối file: macOS chỉ cho Terminal đọc file được
# kéo-thả, không cho đọc các file khác trong Downloads/Desktop.
set -e
case "$0" in /*) SELF=$0 ;; *) SELF="$(pwd 2>/dev/null)/$0" ;; esac

[ "$(uname)" = "Darwin" ] || { echo "Script này chỉ chạy trên macOS."; exit 1; }
if [ "$EUID" -ne 0 ]; then
  echo "Cần quyền quản trị — nhập mật khẩu đăng nhập Mac khi được hỏi."
  cd /
  exec sudo /bin/bash "$SELF" "$@"
fi
cd /

PPD_DIR=/Library/Printers/PPDs/Contents/Resources
BRIDGE_DIR=/Library/Printers/RHEBAR/btbridge
BRIDGE=$BRIDGE_DIR/rhebar-btbridge

if ! ls "$PPD_DIR"/BY-*.ppd >/dev/null 2>&1 && ! ls "$PPD_DIR"/IP-*.ppd >/dev/null 2>&1; then
  echo "Chưa có driver RHEBAR. Cài RHEBAR-Driver_1.3.2.20.pkg trước rồi chạy lại."; exit 1
fi

# 1. Tìm cổng Bluetooth serial của máy in
DEVS=()
for d in /dev/cu.*; do
  case "$d" in
    *Bluetooth-Incoming-Port*|*debug-console*|*usbserial*|*usbmodem*|*wlan*|*MALS*|*SOC*|'/dev/cu.*') ;;
    *) DEVS+=("$d") ;;
  esac
done

if [ ${#DEVS[@]} -eq 0 ]; then
  cat <<'EOF'
Không thấy cổng Bluetooth nào của máy in (/dev/cu.*).
- Bật máy in, vào Cài đặt hệ thống > Bluetooth, ghép đôi với máy in (PIN thường là 0000 hoặc 1234), rồi chạy lại.
- Nếu đã ghép đôi mà vẫn không thấy: máy in chỉ có Bluetooth Low Energy (BLE),
  cầu nối này không dùng được — báo lại để làm bản BLE.
EOF
  exit 1
fi

echo "Cổng Bluetooth tìm thấy:"
for i in "${!DEVS[@]}"; do echo "  $((i+1))) ${DEVS[$i]}"; done
if [ ${#DEVS[@]} -eq 1 ]; then DEV=${DEVS[0]}; else
  read -r -p "Chọn số cổng của máy in: " n; DEV=${DEVS[$((n-1))]}
fi
[ -n "$DEV" ] || { echo "Lựa chọn không hợp lệ."; exit 1; }
NAME=${DEV#/dev/cu.}

# 2. Chọn PPD theo tên máy (vd BY-482BT-E220 -> BY-482BT.ppd)
PPD=""; PPD_M=""
for f in "$PPD_DIR"/*.ppd; do
  m=$(basename "$f" .ppd)
  case "$NAME" in
    "$m"|"$m"-*|"$m"_*) if [ ${#m} -gt ${#PPD_M} ]; then PPD_M=$m; PPD=$f; fi ;;
  esac
done
if [ -n "$PPD" ]; then
  read -r -p "Model: $PPD_M — Enter để đồng ý, hoặc gõ tên model khác: " ans
  [ -n "$ans" ] && PPD="$PPD_DIR/$ans.ppd"
else
  read -r -p "Gõ tên model (vd BY-482BT, IP-802BT): " ans
  PPD="$PPD_DIR/$ans.ppd"
fi
[ -f "$PPD" ] || { echo "Không có file PPD: $PPD"; exit 1; }

# 3. Cài cầu nối, mỗi máy in một cổng TCP riêng trên 127.0.0.1
SAFE=$(echo "$NAME" | tr -c 'A-Za-z0-9\n' '_')
LABEL=com.rhebar.btbridge.$SAFE
PLIST=/Library/LaunchDaemons/$LABEL.plist
PORT=""
if [ -f "$PLIST" ]; then
  PORT=$(/usr/libexec/PlistBuddy -c "Print :ProgramArguments:2" "$PLIST" 2>/dev/null || true)
  launchctl bootout system "$PLIST" 2>/dev/null || true
fi
if [ -z "$PORT" ]; then
  PORT=9190
  while grep -qs "<string>$PORT</string>" /Library/LaunchDaemons/com.rhebar.btbridge.*.plist; do PORT=$((PORT+1)); done
fi

EXTRA=""
if [ "$1" = "--pace" ]; then EXTRA="<string>--pace</string>"; echo "Chế độ: gửi từng trang (--pace)"; fi

case "$(uname -m)" in
  arm64) TAG=AARCH64 ;;
  *) TAG=X86_64 ;;
esac

# Tách chương trình cầu nối (base64) nhúng ở cuối file này
TMPBIN=$(mktemp /tmp/rhebar-btbridge.XXXXXX)
awk -v b="__BIN_${TAG}__" -v e="__END_${TAG}__" '$0==b{f=1;next} $0==e{f=0} f' "$SELF" | base64 --decode > "$TMPBIN" 2>/dev/null || true
if [ ! -s "$TMPBIN" ]; then
  rm -f "$TMPBIN"
  cat <<EOF

Không đọc được phần chương trình nhúng trong $SELF.
Cách sửa: Cài đặt hệ thống > Quyền riêng tư & Bảo mật > Truy cập toàn bộ ổ đĩa > bật Terminal,
thoát hẳn Terminal (Cmd+Q), mở lại và chạy lại.
EOF
  exit 1
fi
xattr -c "$TMPBIN" 2>/dev/null || true

mkdir -p "$BRIDGE_DIR"
launchctl bootout system "$PLIST" 2>/dev/null || true
rm -f "$BRIDGE"
if ! cp "$TMPBIN" "$BRIDGE" 2>/dev/null; then
  rm -f "$TMPBIN"
  echo; echo "Không ghi được vào $BRIDGE_DIR. Gửi kết quả lệnh sau để kiểm tra:"
  echo "  ls -ldO@ /Library/Printers/RHEBAR $BRIDGE_DIR"
  exit 1
fi
rm -f "$TMPBIN"
chown root:wheel "$BRIDGE"
chmod 755 "$BRIDGE"
if ! "$BRIDGE" 2>&1 | grep -q Usage; then
  echo; echo "macOS chặn chạy $BRIDGE. Gửi kết quả lệnh sau để kiểm tra:"
  echo "  ls -lO@ $BRIDGE; codesign -dv $BRIDGE"
  exit 1
fi

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array><string>$BRIDGE</string><string>$DEV</string><string>$PORT</string>$EXTRA</array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardErrorPath</key><string>/var/log/rhebar-btbridge.log</string>
</dict>
</plist>
EOF
chown root:wheel "$PLIST"; chmod 644 "$PLIST"
launchctl bootstrap system "$PLIST"

# 4. Tạo máy in
QUEUE="${SAFE}_Bluetooth"
echo "==> Tạo máy in $QUEUE (PPD: $(basename "$PPD"), cổng 127.0.0.1:$PORT)"
for URI in "socket://127.0.0.1:$PORT?waiteof=false" "socket://127.0.0.1:$PORT"; do
  OUT=$(lpadmin -p "$QUEUE" -E -v "$URI" -P "$PPD" -D "$NAME (Bluetooth)" -L "Bluetooth" 2>&1) || true
  lpstat -v "$QUEUE" >/dev/null 2>&1 && break
done
if ! lpstat -v "$QUEUE" >/dev/null 2>&1; then
  echo; echo "LỖI: không tạo được máy in. lpadmin báo:"
  echo "$OUT"
  echo "Gửi đoạn trên để kiểm tra. Cầu nối Bluetooth vẫn đã cài, không cần gỡ."
  exit 1
fi
cupsenable "$QUEUE" 2>/dev/null || true
cupsaccept "$QUEUE" 2>/dev/null || true
lpadmin -p "$QUEUE" -o printer-is-shared=false 2>/dev/null || true

if ! launchctl print "system/$LABEL" >/dev/null 2>&1; then
  echo "CẢNH BÁO: cầu nối $LABEL chưa chạy. Xem: tail /var/log/rhebar-btbridge.log"
fi

echo
lpstat -v "$QUEUE"
echo "Xong. Máy in '$NAME (Bluetooth)' đã được thêm."
echo "Lần in đầu Mac cần vài giây để kết nối Bluetooth. Nhật ký: /var/log/rhebar-btbridge.log"

exit 0
# ---- payload (base64), đừng sửa phía dưới ----
