# App v6.28 — áp/dòng tay hàn

Chạy `Chay_app.cmd` hoặc `dist/BB_V6_Studio_v6_28.exe`. Mở **Áp / dòng tay hàn** ở Tổng quan hoặc **Cấu hình → Tay hàn**, nhập điện áp/giới hạn dòng, **Áp dụng cấu hình → Lưu Flash**. Cho phép khi LAB/OFF hoặc SOLDER/OFF, khóa khi đang khởi động/ON. Cần firmware BBV6.28; board cũ vẫn đọc được nhưng hai ô mới bị khóa. Hai giá trị dùng chung C245/C210, mặc định 18 V/6 A. [Hướng dẫn đầy đủ](../SOLDER_APP_CONFIG_VI.md).

Các mục bên dưới là lịch sử phiên bản trước.

# App v6.19 — tay hàn hoạt động bình thường

Chạy `Chay_app.cmd` hoặc `dist/BB_V6_Studio_v6_19.exe`. Khi board ở SOLDER bình thường, app hiển thị nhiệt mũi, C245/C210, nhiệt đặt, duty heater và trạng thái gác; ghi lỗi cùng ADC vào nhật ký. Vẫn hỗ trợ bản ADC debug cũ với cảnh báo bảo vệ mũi tắt. Giữ V/A trên thiết bị khoảng 1 s để vào SOLDER nhanh; chọn loại mũi/nhiệt trong menu. App chỉ dừng heater từ xa, không cho ON hoặc đổi V/A trong SOLDER. Xem [SOLDER_NORMAL_VI.md](../SOLDER_NORMAL_VI.md).

# BB V6 · DC Power Studio

App Windows qua USB CDC cho STM32F103C8T6. Bản EXE nằm tại
`dist/BB_V6_Studio_v6_14.exe`, chạy trực tiếp, không cần cài Python. Có thể dùng
`Chay_app.cmd`; thêm `--demo` để xem giao diện với dữ liệu giả lập.

BBV6.14 cho chỉnh/lưu 15 A liên tục, 20 A đỉnh, VIN tối đa 60 V, VOUT tối đa
59,9 V, công suất 600 W và ngân sách dòng vào 10 A trong tab **Cấu hình**.
Chỉ cần sửa các trường giới hạn, **Áp dụng cấu hình → Lưu Flash**, không reset PID.
Profile Flash cũ không tự tăng giới hạn sau khi nạp HEX. Xem [hướng dẫn](../POWER_LIMITS_VI.md).

Tab Tổng quan hiển thị **DUTY BUCK · IN / HO** và **DUTY BOOST · IN / HO** ngay
dưới VIN/FET/POWER/trạng thái. Lấy `buck_pm` và `boost_ho_pm` từ telemetry, chia
10 để ra phần trăm (900 = 90,0%). Đây là duty HIGH lệnh IN/high-side của hai
driver, không phải phần trăm đóng low-side boost. Dữ liệu thiếu hiện “—”. Không
cần nạp lại firmware để dùng tính năng này với telemetry hiện tại.

## Bắt đầu

1. Nạp `../MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex` BBV6.14 đi kèm để chỉnh/lưu tốc độ
   ramp điện áp từ VOUT hiện tại đến mục tiêu khi ON hoặc tăng V đặt.
   Hạ V đặt có hiệu lực ngay; CC và các bảo vệ vẫn hoạt động trong ramp.
   PWM giữ 10 kHz, hệ số chia áp VOUT 23,0; có chẩn đoán PWM/ADC.
   Touch đã gỡ. Menu sạc/ramp/tay hàn thao tác bằng encoder trên thiết bị;
   xem [hướng dẫn](../MENU_CHARGER_VI.md). App hiện TIME, mAh, trạng thái sạc và
   lưu lỗi/hoàn tất sạc vào nhật ký. Trong CHARGE điện áp khóa theo profile,
   chỉ sửa dòng; trong SOLDER khóa V/A và cho dừng PC15. Sửa PID/cấu hình/preset
   trên app cần trở về LAB/OFF. Loại pin/số cell chọn trực tiếp trên thiết bị.
2. Cắm USB data, chọn cổng STM32 Virtual COM và nhấn **Kết nối**. App chỉ đọc
   trạng thái/cấu hình khi kết nối, không tự bật output hay tự gửi profile.
3. Nhập V/A và **Áp dụng V/A**. **Bật output** dùng giá trị đã áp dụng; nếu vừa
   sửa V/A, app gửi SET, chờ ACK thành công rồi mới gửi ON.
4. Nút **Tắt ngay / Esc** luôn ở đầu cửa sổ. Nút này hủy ON đang chờ và dừng
   xả. **Tắt output** bình thường vẫn kích xả nhẹ theo cấu hình trên board.
5. Nút **Xóa lỗi** giữ các điều kiện an toàn của firmware; app không bỏ qua lỗi.

Chế độ **DEMO** có nhãn vàng rõ ràng và không mở cổng COM. Biểu đồ DEMO chỉ
phục vụ xem giao diện, không mô phỏng cuộn cảm/MOSFET hay kiểm chứng phần cứng.

## Cấu hình

Tab **Cấu hình** có 37 thông số trong các nhóm:

- Ramp điện áp: tốc độ tăng áp 0,1–100 V/s, mặc định 10 V/s.
- PID CV/CC: Kp, Ki, Kd. Kd mặc định 0; đạo hàm trên tín hiệu đo có lọc.
- Các ngưỡng VIN, VOUT, dòng đỉnh/liên tục và ngân sách công suất/dòng vào.
- Nhiệt cảnh báo/ngắt/reset; NTC R25, điện trở kéo, B và chiều mắc.
- VDDA, hệ số hiệu chuẩn VIN/VOUT/IOUT, offset ADC dòng, hệ số lọc IIR.
- Cho phép xả, dòng tăng ước tính, khoảng xung, độ rộng, ngưỡng dừng và timeout.

**Áp dụng cấu hình** chỉ được chấp nhận khi output OFF và xả đã dừng.
Nếu đang DRAIN, dùng **Dừng xả** trước. Các thông số được kiểm tra theo một
giao dịch; không áp dụng một nửa cấu hình khi một trường sai. App và firmware
đều kiểm tra phạm vi/thứ tự ngưỡng. Cấu hình có số revision để tránh ghi đè
một bản vừa thay đổi trên board.

Để chỉnh ramp, nhấn **Chỉnh ramp** ở Tổng quan hoặc mở nhóm **Ramp điện áp**
ở đầu tab Cấu hình. Nhập V/s, **Áp dụng cấu hình**, sau đó **Lưu Flash**.
Ví dụ 2,5 V/s tăng tham chiếu 0 → 5 V trong khoảng 2 s; VOUT thực tế phụ
thuộc tải và vòng điều khiển. Giảm V đặt có hiệu lực ngay. Nhãn ramp ở
Tổng quan lấy từ cấu hình/telemetry đã xác nhận, không từ giá trị chưa áp dụng.
Với BBV6.5/6.7, mục ramp bị khóa và vẫn chỉnh PID được; nạp BBV6.8 để mở ramp.

**Lưu Flash** lưu cấu hình đã áp dụng cùng ba preset vào journal hai trang
có CRC, ghi commit cuối. Mất điện trong lúc ghi vẫn giữ bản hợp lệ trước đó.
Journal V2 cũ giữ nguyên PID/hiệu chuẩn/preset, chỉ bổ sung ramp 10 V/s;
journal V1 chỉ có preset thì cấu hình nâng cao dùng mặc định. SAVE mới ghi V3,
firmware cũ không đọc V3. Profile JSON cũ có 36 thông số được bổ sung ramp mặc
định khi nạp bằng app mới. Setpoint hiện tại không được phục hồi thành ON khi boot;
boot luôn OFF, mặc định 5 V / 1 A hoặc được chặn theo giới hạn đã lưu.

**Khôi phục mặc định** áp dụng lại giá trị mặc định trong RAM; nhấn **Lưu Flash**
nếu muốn lưu qua reset. **Nạp/Lưu profile** thao tác với file JSON trên PC.
Nạp profile chỉ điền form, chưa gửi lên board. Áp dụng cấu hình và V/A khi cần.
Chọn M1–M3 điền V/A; **Gán M1–M3** đổi preset trong RAM, sau đó lưu Flash.
Khôi phục mặc định nằm trong menu **Bảo trì**, tách khỏi nút Áp dụng/Lưu.

Các mức trần cố định của bản bench:

| Thông số | Trần |
|---|---:|
| VIN | Ngắt ở 60 V |
| VOUT | Đặt tối đa 59,9 V |
| Dòng liên tục | 15 A |
| Dòng đỉnh | 20 A / tối thiểu 2 mẫu |
| Công suất / ngân sách dòng vào | 600 W / 10 A ước tính |
| Nhiệt ngắt | 85 °C |
| OVP tuyệt đối | 60 V |
| Xả xung | ≤20 µs, khoảng cách ≥20 ms; ở 60 V xung tối thiểu 2 µs có ΔI ≈1,24 A |

App cho hạ các mức trần này, không cho tăng vượt bản firmware hiện tại.
Shunt 1 mΩ, gain INA199 100; hệ số chia áp VIN và VOUT đều là 23,0
(220k/10k), theo cập nhật mới nhất của chủ board.
Dòng xả nội bộ không qua shunt output; chỉnh thông số xả không biến nó thành
xả có bảo vệ dòng. Kp/Ki/Kd và hiệu chuẩn cần được kiểm chứng trên board thật.

## Dữ liệu và kết nối

**Lịch sử lỗi** ghi tự động khi nhận trạng thái bảo vệ/chặn ON/xả dừng hoặc
lỗi ứng dụng, không lặp cùng trạng thái ở mỗi mẫu. Chọn dòng để xem snapshot;
**Xuất nhật ký lỗi** lưu JSONL. File tự ghi tại
`%LOCALAPPDATA%/BB_V6_Studio/logs`, timestamp giờ Việt Nam. LCD chỉ hiện trạng
thái ngắn; lý do lỗi và ADC trip ở app. Chỉ lưu được dữ liệu app đã nhận,
không tải lại toàn bộ lịch sử offline từ MCU. DEMO được đánh dấu trong log.
Mục **Thông số thô** tách riêng JSON hiện tại và log lệnh.

Biểu đồ VOUT/IOUT hiển thị 60 giây gần nhất. Tab **Nhật ký & chẩn đoán** có JSON
mới nhất, duty, lệnh CV/CC, giới hạn dòng hiệu dụng và mẫu lỗi gốc. Có nút sao
chép JSON, ghi CSV với timestamp giờ Việt Nam và các đơn vị mV/mA/0,1 °C.

App không tự gửi lại ON/CFG/SAVE khi mất ACK, vì lệnh có thể đã thực hiện.
App chỉ đọc lại trạng thái. Khi mất kết nối/dữ liệu, ON bị khóa; ngắt USB
không tự tắt output trên board. Kết nối lại chỉ đọc, không tự phục hồi ON.

## Chạy mã nguồn / build EXE

Python 3.10+ trên Windows có Tcl/Tk:

```powershell
python -m pip install -r requirements.txt
python app.py
python app.py --demo
python -m unittest discover -s tests -v
python app.py --smoke-test
```

Build EXE:

```powershell
python -m pip install -r requirements-build.txt
python build_app.py
```

`settings_schema.json` được sinh từ đúng định nghĩa C trong
`Core/Inc/psu_settings_fields.def`. Chạy `../tools/export_settings_schema.py`
sau khi đổi định nghĩa. Giao thức ASCII vào / JSON Lines ra được mô tả trong
`../USB_PROTOCOL.md`. App dùng Tkinter, pySerial và được đóng gói bằng PyInstaller.
