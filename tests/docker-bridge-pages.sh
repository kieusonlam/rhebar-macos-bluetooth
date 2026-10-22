# Test lệnh in nhiều trang qua cầu nối với máy in giả lập (sim_printer.py).
#   docker run --rm -v "$PWD/build:/w" -v "$PWD/tests:/t" python:3.12-slim bash /t/docker-bridge-pages.sh [bridge-binary]
# Thoát 0 nếu mọi trường hợp đều nhận đủ trang và đúng từng byte.
BR=${1:-/w/btbridge-linux}
cp "$BR" /tmp/br && chmod +x /tmp/br
fail=0

make_job() {  # $1 = số trang, $2 = glued (PRINT dính sau BITMAP)
  python3 - "$1" "$2" <<'EOF'
import os, sys, random
random.seed(1)
n, glued = int(sys.argv[1]), sys.argv[2] == "glued"
out = bytearray()
for i in range(n):
    out += b"SIZE 76 mm,130 mm\r\nGAP 2 mm,0 mm\r\nSPEED 4\r\nDENSITY 8\r\nCLS\r\n"
    out += b"BITMAP 0,0,76,1039,0," + bytes(random.getrandbits(8) for _ in range(76 * 1039))
    out += (b"PRINT 1,1\r\n" if glued else b"\r\nPRINT 1,1\r\n")
sys.stdout.buffer.write(out)
EOF
}

run_case() {  # $1 tên, $2 số trang, $3 glued|normal, $4 cờ máy giả lập, $5 cờ cầu nối
  local name=$1 n=$2 dev=/tmp/cu.SIM-$1 port=$((9300 + RANDOM % 500))
  rm -f /tmp/sim-$name*
  python3 /t/sim_printer.py $dev /tmp/sim-$name $4 & local sp=$!
  while [ ! -f /tmp/sim-$name.ready ]; do sleep 0.1; done
  /tmp/br $dev $port $5 2>/tmp/br-$name.log & local bp=$!
  sleep 0.5
  make_job $n $3 > /tmp/job-$name
  python3 -c "
import socket,sys
s=socket.create_connection(('127.0.0.1',$port)); s.sendall(open('/tmp/job-$name','rb').read()); s.close()"
  # chờ cầu nối báo xong job
  for _ in $(seq 120); do grep -q "job done" /tmp/br-$name.log && break; sleep 0.5; done
  sleep 1.5
  local got=$(cat /tmp/sim-$name.pages 2>/dev/null || echo 0)
  if [ "$got" = "$n" ] && cmp -s /tmp/sim-$name /tmp/job-$name; then
    echo "OK   $name: $got/$n trang, khớp từng byte ($(grep -c . /tmp/br-$name.log) dòng log)"
  else
    echo "FAIL $name: $got/$n trang"; fail=1
  fi
  kill $bp $sp 2>/dev/null; wait $bp $sp 2>/dev/null
}

# mặc định: gửi liền, máy in bộ đệm lớn (như máy thật in từ Windows)
run_case stream-status 3 normal "--nodrop" ""
run_case stream-silent 3 normal "--nodrop --silent" ""
run_case stream-glued 3 glued "--nodrop" ""
# --pace: máy in bộ đệm nhỏ, rơi dữ liệu nếu gửi trong lúc đang in
run_case pace-status 3 normal "" --pace
run_case pace-silent 3 normal "--silent" --pace
run_case pace-glued 3 glued "" --pace
cat /tmp/br-stream-status.log /tmp/br-stream-silent.log
exit $fail
