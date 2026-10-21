# BBV6.24-TEST18-10HZ — bản thử ADC, rail 18 V / 6 A

TEST18 hiện phát PB13 50 ms ON / 50 ms OFF độc lập nhiệt quy đổi để đo xung. Xem [PB13_FIXED_TEST_VI.md](PB13_FIXED_TEST_VI.md); phần dưới lưu lịch sử các bản trước.

PB13 hiện PWM 10 Hz, chu kỳ 100 ms, duty theo PID, tối thiểu 5 ms OFF để đo ADC. Xem [SOLDER_10HZ_VI.md](SOLDER_10HZ_VI.md). Phần dưới giữ thông tin bản test18 và sửa giao diện trước đó.

Bản 6.22 phục vụ nút/USB trong lúc vẽ LCD để tránh TIMING do một lượt vẽ lâu. Giữ nguyên các ngưỡng giám sát. Xem [TIMING_UI_FIX_VI.md](TIMING_UI_FIX_VI.md).

Giữ V/A khoảng 1 giây để chuyển SOLDER. Sau trình tự chuyển đường/khởi động nguồn, rail mặc định 18 V, giới hạn dòng 6 A cho cả lựa chọn C245 và C210 trong bản test này. Trở về LAB khôi phục V/A đã đặt trước đó. BTN1 và USB OFF/`!` vẫn dừng nguồn/heater.

Đã tắt ba lỗi nhiệt mũi SENSOR, HOT, NO_RISE. LCD và app v6.19 hiển thị ADC PA3, có nhãn bảo vệ nhiệt mũi tắt. PID nhiệt tạm và giới hạn duty cũ vẫn được dùng để phát heater; đây không phải chế độ heater luôn ON. Chưa hiệu chuẩn chính xác nhiệt mũi.

Không bỏ bảo vệ phần nguồn: ADC1 mất dữ liệu, quá dòng/áp, nhiệt FET, VIN, TIMING, watchdog và giới hạn cấu hình vẫn có hiệu lực. Thiếu mẫu PA3 hợp lệ theo thời gian vẫn khóa heater và báo STALE. Không tự xóa lỗi nguồn đã chốt; giữ BTN1 để xóa sau khi điều kiện lỗi hết.

ADC2 injected PA3 tiếp tục đọc khi LAB, SOLDER OFF và lúc chốt fault, không phụ thuộc trạng thái ON của rail. `solder.adc` là mẫu mới nhất. Khi heater hoạt động, chỉ nhận mẫu sau khi heater đã LOW ít nhất 2 ms; giữ cửa sổ đo yên tĩnh cũ để tránh đọc nhiễu lúc cấp nhiệt. Không lấy mẫu trong lúc heater HIGH.

Telemetry: `solder.protect=0`, `solder.test18=1`. App v6.19 tương thích, không cần cài lại. Log `timing_src`, `timing_value`, `solder.trip_adc` giữ nguyên.

PA10 vẫn là SD; PB13 là heater, PA3 đo mũi, PB11 LOW=gác, PC15 chuyển đường. Không đổi chân hoặc PID/giới hạn/preset đã lưu Flash. Firmware này không tự nạp hoặc gửi lệnh bật đến board trong lượt phát triển.

Muốn trở về chế độ bình thường trong source: đặt `PSU_SOLDER_TEST18=0` tại `Core/Inc/psu_modes.h` rồi build lại; khi không override riêng, các bảo vệ nhiệt mũi tự bật và rail trở về C245 24 V/6 A, C210 12 V/4 A.
