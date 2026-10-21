# BBV6.23-TEST18-10HZ — PB13 tay hàn 10 Hz

Tài liệu bản trước. TEST18 hiện ở BBV6.24 phát 10 Hz cố định 50% độc lập ADC; xem [PB13_FIXED_TEST_VI.md](PB13_FIXED_TEST_VI.md). Nội dung PID dưới đây chỉ còn áp dụng cho chế độ thường TEST18=0.

Trong SOLDER RUN, PB13 phát một xung ở đầu mỗi chu kỳ 100 ms. Độ rộng xung theo PID, độ phân giải 1 ms; tối đa 95 ms HIGH và tối thiểu 5 ms LOW. Nếu PID yêu cầu 0 hoặc nguồn OFF/lỗi thì PB13 giữ LOW. Đây là PWM 10 Hz có duty thay đổi, không cố định 50/50 và không bật/tắt mỗi 100 ms (tức 5 Hz).

ADC PA3 chỉ được lấy sau khi heater LOW ít nhất 2 ms. Cửa sổ LOW 5 ms đủ hai mẫu bất đồng bộ cho mean trước chu kỳ mới. Thiếu mean vẫn ngừng heater. ADC PA3 tiếp tục đọc khi OFF, LAB hoặc fault.

Khung PID nhiệt chuyển từ 50 lên 100 ms: tích phân/vi phân điều chỉnh theo thời gian lấy mẫu mới; hệ số IIR, suy giảm tích phân và lọc vi phân quy đổi để giữ hằng số thời gian tương ứng. Không tuning lại hệ số nhiệt thực tế. Gác được xác nhận sau 3 khung (khoảng 300 ms); nhấc khỏi gác cập nhật ở khung tiếp theo.

Deadline heater 2 ms vẫn được gia hạn mỗi tick TIM3 trong suốt xung HIGH: nếu ngắt điều khiển ngừng thì SysTick ngắt heater, không đợi hết 95 ms. OFF/emergency/nguồn lỗi vẫn ngắt PB13 ngay. Không dùng HAL_Delay hay vòng chờ để tạo xung.

Nguồn SOLDER tiếp tục bản test 18 V/6 A, ADC thô, bảo vệ nhiệt mũi SENSOR/HOT/NO_RISE tắt; bảo vệ phần nguồn/STALE/TIMING/watchdog giữ nguyên. PA10 SD và PC15 chuyển đường không đổi. PWM buck/boost TIM1 vẫn 10 kHz, vòng điều khiển TIM3 vẫn 1 kHz.

Telemetry thêm `solder.pwm_hz=10`. `solder.duty_pm` là yêu cầu duty trong khung, có thể >0 trong khoảng LOW; `solder.heater` là mức PB13 thực tế lúc snapshot. App v6.19 dùng tiếp được.

Nạp `build/Firmware_BB_V6_BBV6_23_TEST18_10HZ.hex` (cùng nội dung với `build/Firmware_BB_V6.hex`). Chưa nạp hoặc đo xung trên board trong lượt sửa; kiểm tra cạnh xung và cửa sổ ADC bằng mô phỏng mã Cortex-M3.
