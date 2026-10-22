#!/bin/bash
# Gỡ các máy in RHEBAR Bluetooth và cầu nối (không đụng tới driver chính thức).
if [ "$EUID" -ne 0 ]; then exec sudo /bin/bash "$0" "$@"; fi

for q in $(lpstat -v 2>/dev/null | awk -F'[ :]' '/socket:\/\/127\.0\.0\.1:91[0-9][0-9]/{print $3}'); do
  echo "Xóa máy in $q"; lpadmin -x "$q"
done
for p in /Library/LaunchDaemons/com.rhebar.btbridge.*.plist; do
  [ -f "$p" ] || continue
  launchctl bootout system "$p" 2>/dev/null; rm -f "$p"
done
rm -rf /Library/Printers/RHEBAR/btbridge
echo "Đã gỡ cầu nối Bluetooth."
