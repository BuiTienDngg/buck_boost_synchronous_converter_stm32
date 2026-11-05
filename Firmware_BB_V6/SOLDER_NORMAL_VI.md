# BBV6.20 — sửa mốc ADC nhiệt phòng, chẩn đoán lỗi

Tài liệu bản bình thường trước đó. Bản hiện tại BBV6.21-TEST18 dùng 18 V/6 A và ADC debug, xem [SOLDER_TEST18_VI.md](SOLDER_TEST18_VI.md).

Đã trả LCD/app về hiển thị **nhiệt độ mũi °C**, bật lại bảo vệ **SENSOR, HOT, NO_RISE**, timeout ADC, nhiệt FET, bảo vệ nguồn và watchdog. Không còn nhãn TIP PROTECT OFF trong firmware mặc định. Telemetry `solder.protect=1`; ADC thô vẫn có tại `solder.adc` để hiệu chuẩn sau.

**Giữ BTN2 V/A (PB8) khoảng 1 giây để vào SOLDER nhanh**, dùng loại mũi/nhiệt đặt đang chọn. Nếu nguồn đang ON, firmware dừng nguồn/heater, hủy lệnh ON đang chờ rồi mới chuyển mode. Trình tự chờ chuyển đường 100 ms, bắt đầu DC-DC sau tối thiểu 300 ms và đợi rail ổn định vẫn được áp dụng. Không tự xóa lỗi; nếu có lỗi/config khóa hoặc đang ghi Flash thì không chuyển. Khi đã ở SOLDER, giữ V/A chỉ thoát menu về màn hình tay hàn, không tự bật lại heater đã OFF. Nhấn ngắn V/A vẫn đổi chỉnh V/A trong LAB.

Menu cũ vẫn dùng được: giữ encoder → SOLDER để chọn C245/C210, nhiệt SET/STAND. BTN1 dừng/chạy. C245 24 V/6 A; C210 12 V/4 A. Loại mũi/nhiệt đặt đang lưu RAM: sau reset mặc định C245/300°C, nhiệt gác 200°C. Chọn đúng loại mũi trước khi dùng phím tắt.

PA10 giữ SD chung hai IR2184S, PB13 điều khiển heater, PA3 đo nhiệt, PB11 thấp là gác, PC15 đổi đường SOLDER. PWM nguồn vẫn 10 kHz, heater GPIO mật độ xung ở nhịp 1 kHz. PID nguồn, giới hạn và preset Flash giữ nguyên.

BBV6.20 dùng mốc tạm người dùng đo ADC ≈1200 tại 30°C: C245 `30+(ADC−1200)×1,1`, C210 `30+(ADC−1200)×1,5`. Độ dốc giữ từ V5, cần thêm điểm đo nóng để hiệu chuẩn; không coi đây là phép đo nhiệt đã chính xác. PID nhiệt và IIR giữ nguyên.

`PSU_SOLDER_TEMP_PROTECTION` mặc định `1` trong `Core/Inc/psu_modes.h`. Cấu hình debug chỉ còn để kiểm tra riêng với override `=0`, không phải mặc định của HEX này. App v6.19 hiển thị nhiệt trong chế độ thường và vẫn nhận biết firmware ADC debug cũ để hiện ADC/cảnh báo đúng.

Chưa nạp hay bật heater qua USB trong lượt này. Tránh full-chip erase để giữ hai trang Flash chứa PID/preset. Chi tiết điều khiển: [SOLDER_MODE_VI.md](SOLDER_MODE_VI.md).

## Chẩn đoán BBV6.20

Công thức cũ biến ADC 1200 thành −55°C (C245) / −75°C (C210), thấp hơn ngưỡng −30°C nên chốt SENSOR. Công thức mới cho 30°C ở ADC 1200, dùng chung ở kiểm tra mẫu và tính mean/IIR. Giữ nguyên mọi ngưỡng bảo vệ và thời hạn heater.

`solder.trip_adc` giữ mẫu đầu tiên gây SENSOR/HOT tức thời, reset khi bắt đầu lượt mới; `solder.adc` vẫn là mẫu mới nhất. Giá trị 0 có thể là mẫu lỗi 0 hoặc chưa có lỗi; đối chiếu `solder.reason`.

`timing_src` là bitmask của fault 256: 1 heater quá hạn, 2 foreground không poll >250ms, 4 ISR quá 36000 chu kỳ CPU, 8 timer xả không dừng, 16 cấu hình timer xả sai. `timing_value` giữ giá trị của nguồn đầu tiên tương ứng: trễ tính từ hạn heater (ms, có thể =0), tuổi foreground (ms), chu kỳ ISR, thời gian từ xung xả (ms), hoặc CR1. Xóa cùng fault khi thao tác xóa lỗi thành công. Chưa kết luận nguyên nhân TIMING trên board từ log cũ.

App v6.19 tiếp tục dùng được, log JSON tự lưu các trường mới.
