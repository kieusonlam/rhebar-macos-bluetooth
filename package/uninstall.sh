#!/bin/bash
# Gỡ driver RHEBAR khỏi macOS (xóa các máy in RHEBAR_*, PPD và filter).
if [ "$EUID" -ne 0 ]; then exec sudo /bin/bash "$0" "$@"; fi

for q in $(lpstat -p 2>/dev/null | awk '/^printer RHEBAR_/{print $2}'); do
  echo "Xóa máy in $q"; lpadmin -x "$q"
done
rm -f /Library/Printers/PPDs/Contents/Resources/RHEBAR-*.ppd
rm -rf /Library/Printers/RHEBAR
echo "Đã gỡ driver RHEBAR."
