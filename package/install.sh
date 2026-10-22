#!/bin/bash
# Cài driver máy in nhãn RHEBAR (TSPL) cho macOS.
# Chạy: mở Terminal, kéo file này vào cửa sổ Terminal (hoặc gõ: bash install.sh), Enter.
set -e
cd "$(dirname "$0")"

if [ "$(uname)" != "Darwin" ]; then
  echo "Script này chỉ chạy trên macOS."; exit 1
fi

if [ "$EUID" -ne 0 ]; then
  echo "Cần quyền quản trị — nhập mật khẩu đăng nhập Mac khi được hỏi."
  exec sudo /bin/bash "$0" "$@"
fi

case "$(uname -m)" in
  arm64) BIN=bin/rastertotspl-aarch64 ;;
  x86_64) BIN=bin/rastertotspl-x86_64 ;;
  *) echo "CPU không hỗ trợ: $(uname -m)"; exit 1 ;;
esac

FILTER_DIR=/Library/Printers/RHEBAR/Filters
PPD_DIR=/Library/Printers/PPDs/Contents/Resources

echo "==> Cài filter ($(uname -m))"
xattr -cr . 2>/dev/null || true
mkdir -p "$FILTER_DIR"
install -o root -g wheel -m 755 "$BIN" "$FILTER_DIR/rastertotspl"

echo "==> Cài PPD"
mkdir -p "$PPD_DIR"
for f in PPDs/*.ppd; do
  install -o root -g wheel -m 644 "$f" "$PPD_DIR/$(basename "$f")"
done

# Danh sách model
MODELS=()
for f in PPDs/*.ppd; do
  m=$(basename "$f" .ppd); MODELS+=("${m#RHEBAR-}")
done

echo
echo "==> Tìm máy in USB đang cắm..."
URIS=()
while IFS= read -r line; do
  URIS+=("${line#direct }")
done < <(lpinfo -v 2>/dev/null | grep '^direct usb://' || true)

if [ ${#URIS[@]} -eq 0 ]; then
  echo "Không thấy máy in USB nào. Driver đã cài xong."
  echo "Cắm máy in + bật nguồn, rồi chạy lại script này, hoặc thêm thủ công:"
  echo "  Cài đặt hệ thống > Máy in & Máy quét > Thêm máy in > Sử dụng: Chọn phần mềm > tìm 'RHEBAR'."
  exit 0
fi

echo "Các máy in USB tìm thấy:"
for i in "${!URIS[@]}"; do echo "  $((i+1))) ${URIS[$i]}"; done
if [ ${#URIS[@]} -eq 1 ]; then
  URI=${URIS[0]}
else
  read -r -p "Chọn số máy in: " n; URI=${URIS[$((n-1))]}
fi

# Đoán model từ URI
GUESS=""
for m in "${MODELS[@]}"; do
  key=$(echo "$m" | sed 's/-/[-_ %20]*/g')
  if echo "$URI" | grep -qiE "$key([^A-Za-z0-9]|$)"; then
    if [ ${#m} -gt ${#GUESS} ]; then GUESS=$m; fi
  fi
done

echo
echo "Các model:"
for i in "${!MODELS[@]}"; do printf "  %2d) %s\n" "$((i+1))" "${MODELS[$i]}"; done
if [ -n "$GUESS" ]; then
  read -r -p "Model của bạn [Enter = $GUESS]: " n
else
  read -r -p "Chọn số model (xem tem dưới đáy máy in): " n
fi
if [ -z "$n" ] && [ -n "$GUESS" ]; then MODEL=$GUESS; else MODEL=${MODELS[$((n-1))]}; fi
[ -z "$MODEL" ] && { echo "Lựa chọn không hợp lệ."; exit 1; }

QUEUE="RHEBAR_$(echo "$MODEL" | tr -c 'A-Za-z0-9\n' '_')"
echo "==> Tạo máy in '$QUEUE' (model $MODEL)"
lpadmin -p "$QUEUE" -E -v "$URI" -P "$PPD_DIR/RHEBAR-$MODEL.ppd" \
  -D "RHEBAR $MODEL" -o printer-is-shared=false 2>&1 | grep -vi "deprecated" || true
cupsenable "$QUEUE" 2>/dev/null || true
cupsaccept "$QUEUE" 2>/dev/null || true

echo
echo "Xong. Máy in '$QUEUE' đã sẵn sàng."
echo "Khi in: chọn đúng khổ nhãn ở 'Kích thước giấy', tỷ lệ 100%."
