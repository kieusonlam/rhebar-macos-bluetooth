# Test đầu-cuối: PDF -> CUPS -> rastertotspl -> TSPL trong Docker Linux.
# Chạy (sau tools/build.sh):
#   docker run --rm -v "$PWD/build:/w" -v "$PWD/tests:/t" debian:bookworm-slim bash /t/docker-cups-e2e.sh
apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends cups cups-filters ghostscript >/dev/null
set -e
mkdir -p /Library/Printers/RHEBAR/Filters
install -m 755 /w/rastertotspl-linux /Library/Printers/RHEBAR/Filters/rastertotspl
echo "== cupstestppd =="; cupstestppd -W translations /w/PPDs/*.ppd | grep -v ": PASS" || true
cupstestppd /w/PPDs/*.ppd | grep -c ": PASS"
cat > /tmp/t.ps <<'PS'
%!PS
/Helvetica-Bold findfont 40 scalefont setfont
20 300 moveto (RHEBAR TEST) show
newpath 20 20 moveto 150 0 rlineto 0 150 rlineto -150 0 rlineto closepath 4 setlinewidth stroke
showpage
PS
gs -q -sDEVICE=pdfwrite -o /tmp/t.pdf /tmp/t.ps
echo "== cupsfilter =="
for m in IP-802:76x130mm BY-498:100x150mm; do
  p=${m%%:*}; s=${m#*:}
  cupsfilter -e -p /w/PPDs/RHEBAR-$p.ppd -m printer/foo -o PageSize=$s -o Speed=3 -o Density=10 /tmp/t.pdf > /w/out-$p.tspl 2>/tmp/log || { tail -20 /tmp/log; exit 1; }
  echo "-- $p"; grep -a "^\(SIZE\|GAP\|SPEED\|DENSITY\|BITMAP\|PRINT\)" /w/out-$p.tspl | cut -c1-40
done
