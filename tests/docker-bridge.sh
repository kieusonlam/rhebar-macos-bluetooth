# Test cầu nối Bluetooth: giả lập cổng /dev/cu.* bằng FIFO, gửi 2 lệnh in, so từng byte.
# Cần build/out-IP-802.tspl và build/out-BY-498.tspl (sinh từ docker-cups-e2e.sh).
#   docker run --rm -v "$PWD/build:/w" -v "$PWD/tests:/t" debian:bookworm-slim bash /t/docker-bridge.sh
set -e
cp /w/btbridge-linux /tmp/br && chmod +x /tmp/br
mkfifo /tmp/cu.BY-482BT-E220
(while true; do cat /tmp/cu.BY-482BT-E220; done) > /tmp/recv &
/tmp/br /tmp/cu.BY-482BT-E220 9190 2>/tmp/br.log &
sleep 1
for f in /w/out-IP-802.tspl /w/out-BY-498.tspl; do
  exec 3<>/dev/tcp/127.0.0.1/9190; cat $f >&3; exec 3>&-; sleep 3
done
cat /w/out-IP-802.tspl /w/out-BY-498.tspl > /tmp/expect
cmp /tmp/recv /tmp/expect && echo "BYTES MATCH $(wc -c < /tmp/recv)"
# cổng chưa tồn tại lúc đầu -> bridge phải chờ rồi gửi khi cổng xuất hiện
/tmp/br /tmp/cu.LATE 9191 2>>/tmp/br.log &
sleep 1
(exec 3<>/dev/tcp/127.0.0.1/9191; echo "HELLO" >&3; exec 3>&-) &
sleep 3; mkfifo /tmp/cu.LATE; timeout 15 cat /tmp/cu.LATE > /tmp/late || true
grep -q HELLO /tmp/late && echo "LATE DEVICE OK"
cat /tmp/br.log
