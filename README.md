# In qua Bluetooth cho máy in nhãn RHEBAR / Beeprt trên macOS

Driver chính thức RHEBAR cho Mac (`RHEBAR-Driver_1.3.2.20.pkg`) chỉ in được qua **USB**.
Repo này thêm một **cầu nối Bluetooth** nhỏ để in không dây từ Mac tới các máy in nhãn Bluetooth
như **BY-482BT**, IP-802BT, BY-426BT…, dùng đúng driver chính thức.

Đã chạy thực tế với **Beeprt BY-482BT** trên macOS.

> Dự án cá nhân, không liên quan tới RHEBAR/Beeprt. Repo không chứa driver chính thức;
> tải driver đó từ nhà sản xuất ([rhebar.com](https://www.rhebar.com)) hoặc nơi bán máy.

## Tải về

Vào [Releases](../../releases/latest):

| File | Dùng khi |
|---|---|
| `RHEBAR-Bluetooth-macOS.zip` | **In qua Bluetooth** (dùng kèm driver chính thức) |
| `RHEBAR-macOS-driver.zip` | Driver USB tự viết (không chính thức), chỉ khi không có driver chính thức |

## Cài in qua Bluetooth

Yêu cầu: macOS 11 trở lên (Mac Intel hoặc Apple M), máy in có Bluetooth thường (SPP).

1. Cài driver chính thức `RHEBAR-Driver_1.3.2.20.pkg`.
2. Bật máy in. **Cài đặt hệ thống → Bluetooth** → bấm **Kết nối** ở máy in (vd `BY-482BT-E220`).
   Nếu hỏi PIN, thử `0000` hoặc `1234`.
3. Giải nén `RHEBAR-Bluetooth-macOS.zip`. Mở **Terminal**, gõ `bash ` (có dấu cách),
   **kéo file `cai-bluetooth.sh` vào cửa sổ Terminal**, nhấn Enter.
4. Nhập mật khẩu Mac, nhấn Enter để đồng ý model được đoán (vd `BY-482BT`).

Xong: máy in **"BY-482BT-E220 (Bluetooth)"** xuất hiện trong danh sách máy in
(Cài đặt hệ thống → Máy in & Máy quét, và ô chọn máy in khi bấm In).
Không cần vào "Thêm máy in".

> Phải **kéo-thả** file script vào Terminal: macOS chặn Terminal đọc file trong Downloads/Desktop,
> chỉ file được kéo-thả mới đọc được. Vì vậy chương trình cầu nối được nhúng sẵn trong script.

### Khi in

- Chọn máy in "… (Bluetooth)", chọn đúng **khổ nhãn** (vd 100 x 150 mm), tỷ lệ 100%.
- Lần in đầu Mac mất vài giây để kết nối Bluetooth; máy in phải bật và ở gần.

### Gỡ

- Gỡ sạch: kéo `go-bluetooth.sh` vào Terminal giống lúc cài. Xóa máy in Bluetooth + cầu nối,
  giữ nguyên driver chính thức (in USB không ảnh hưởng).
- Chỉ xóa máy in trong Cài đặt hệ thống cũng được; cầu nối vẫn chạy nền nhưng rất nhẹ, không ảnh hưởng gì.
- Ghép đôi lại / đổi máy in: chạy lại `cai-bluetooth.sh`.

### Sự cố

| Hiện tượng | Cách xử lý |
|---|---|
| "Không thấy cổng Bluetooth" | Chưa ghép đôi; hoặc máy in chỉ có Bluetooth LE (chưa hỗ trợ). Kiểm tra bằng `ls /dev/cu.*` |
| In nhiều trang chỉ ra trang đầu | Cập nhật lên bản ≥ 1.0.1 (chạy lại `cai-bluetooth.sh` mới) |
| Lệnh in treo, không ra giấy | Tắt/bật máy in, xem nhật ký `tail /var/log/rhebar-btbridge.log` |
| In lệch, nhảy nhãn | Cho máy tự đo nhãn (giữ FEED khi bật nguồn), chọn đúng khổ nhãn |
| Máy in không có trong danh sách | Chạy `lpstat -v`; chạy lại `cai-bluetooth.sh` và xem lỗi in ra |

## Cách hoạt động

macOS không có đường in qua Bluetooth và không cho cài CUPS backend bên thứ ba (SIP). Nên:

```
Ứng dụng → CUPS + driver chính thức (TSPL) → socket://127.0.0.1:9190
        → rhebar-btbridge (LaunchDaemon) → /dev/cu.BY-482BT-E220 (Bluetooth SPP) → máy in
```

- [`src/btbridge.c`](src/btbridge.c): nghe `127.0.0.1:<port>`, mỗi lệnh in mở cổng serial Bluetooth
  (macOS tự kết nối), chuyển dữ liệu, đóng cổng khi xong. Chờ tối đa 30 giây nếu cổng chưa sẵn sàng.
  Gửi **từng trang** (cắt sau mỗi lệnh TSPL `PRINT`) và chờ máy in xong trang đó qua lệnh trạng thái
  `ESC ! ?` rồi mới gửi tiếp. Qua Bluetooth máy in không chặn được luồng dữ liệu như USB, gửi liền
  nhiều trang thì các trang sau bị rơi. Máy không trả lời trạng thái thì chờ theo thời gian ước tính
  từ `SIZE`/`SPEED`.
- [`bt-package/cai-bluetooth.sh`](bt-package/cai-bluetooth.sh): tìm cổng `/dev/cu.*`, chọn PPD
  chính thức theo tên máy, cài LaunchDaemon `com.rhebar.btbridge.<tên>`, tạo hàng đợi in bằng `lpadmin`.
  Mỗi máy in Bluetooth dùng một cổng riêng từ 9190.

## Driver USB tự viết (không chính thức)

Viết trước khi có driver chính thức, giữ lại cho ai cần:

- [`src/rastertotspl.c`](src/rastertotspl.c): CUPS filter, đọc CUPS raster v1/v2/v3 (không cần libcups),
  xuất lệnh TSPL `BITMAP`. Có chế độ ngưỡng (chữ/barcode) và dither (ảnh).
- [`tools/gen_ppd.py`](tools/gen_ppd.py): sinh 35 PPD (203/300 dpi, tốc độ, khổ nhãn theo từng model).
- Cài: giải nén `RHEBAR-macOS-driver.zip`, kéo `install.sh` vào Terminal.

Đã kiểm bằng `cupstestppd` (35/35 PASS) và `cupsfilter` PDF → TSPL trong Docker; chưa in thật.

## Build từ mã nguồn

Cần [zig](https://ziglang.org) (biên dịch chéo cho macOS từ Linux/Windows/macOS) và Python 3:

```bash
bash tools/build.sh    # zig qua pip: ZIG="python -m ziglang" PY=python bash tools/build.sh
```

Kết quả nằm trong `build/`: binary macOS x86_64 + arm64 (ad-hoc signed) và 2 file zip.

Test:

```bash
python3 tools/test_filter.py build/<rastertotspl cho máy đang chạy> build/test-out
docker run --rm -v "$PWD/build:/w" -v "$PWD/tests:/t" debian:bookworm-slim bash /t/docker-cups-e2e.sh
docker run --rm -v "$PWD/build:/w" -v "$PWD/tests:/t" python:3.12-slim bash /t/docker-bridge-pages.sh
```

## License

[MIT](LICENSE)
