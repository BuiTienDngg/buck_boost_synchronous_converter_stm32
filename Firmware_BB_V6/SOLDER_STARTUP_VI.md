# BBV6.25-TEST18-RAMP — trình tự chuyển tay hàn và ramp duty

Giữ V/A để vào SOLDER. Mỗi lần bật lại bằng BTN1 cũng chạy mới toàn bộ trình tự:

1. Ngắt heater: PB13 LOW, duty/giới hạn ramp về 0, tắt DC-DC và PC15 LOW.
2. Giữ OFF tối thiểu 100 ms. Nếu tụ nguồn từ LAB còn cao hơn profile +0,5 V, tiếp tục giữ switch LOW, không dùng heater để xả tụ.
3. Chuyển PC15 HIGH. Chờ tối thiểu 200 ms tính từ lệnh chuyển switch, kể cả khi trước đó scheduler bị trễ.
4. Đặt nguồn 18 V/6 A và bật DC-DC. Ramp điện áp nguồn vẫn theo cấu hình RAMP đang chọn trên máy/app.
5. Đợi VOUT đạt 95% profile đến profile +0,5 V, reference gần mức đặt và giữ ổn định 100 ms. PB13 vẫn LOW trong suốt bước chờ.
6. Bắt đầu PWM tay hàn 10 Hz, ramp duty 0→50% trong 2 giây. Giới hạn duty tăng ở ranh giới chu kỳ 100 ms; bước đầu 2,5% (xung khoảng 3 ms), rồi 5%, 7,5%... đến 50%. Không chèn xung mới giữa chu kỳ.

OFF/fault ngắt PB13 trước khi đổi switch, dừng nguồn, kéo PC15 LOW và reset ramp. Bật lại phải chờ switch, nguồn ổn định và chạy ramp từ đầu. Nếu rail sụt dưới 70% profile khi RUN, ngừng heater và reset ramp; giữ bảo vệ rail timeout hiện có. Không tự xóa fault hoặc tự thử lại sau fault.

`solder.ramp_ms`: thời gian ramp đã chạy, tối đa 2000 ms. `solder.ramp_pm`: giới hạn duty hiện tại, 0→500 trong TEST18. `duty_pm` là duty thực tế được phép, không vượt giới hạn này. Các phase giữ tương thích: 1 ROUTE, 2 PRECHARGE, 3 RUN; trong RUN, LCD báo RAMP đến hết 2 giây. `fixed_pwm=1` vẫn có nghĩa target test cố định 50%, độc lập nhiệt quy đổi; duty đầu ra được giới hạn bởi ramp.

TEST18 vẫn dùng nguồn 18 V/6 A cho cả hai lựa chọn mũi, ADC thô, không điều nhiệt theo PID. PA3 tiếp tục lấy mẫu trong khoảng PB13 LOW. Giữ PWM buck/boost 10 kHz, vòng TIM3 1 kHz, SD PA10 và watchdog heater 2 ms. Không đổi các PID nguồn, giới hạn/preset đã lưu Flash hoặc schema settings.

`PSU_SOLDER_HEATER_RAMP_MS=2000` trong `Core/Inc/psu_modes.h` là thời gian ramp duty tạm. Khi build chế độ thường TEST18=0, áp dụng ceiling 0→95% trong cùng thời gian; duty vẫn thấp hơn nếu PID nhiệt yêu cầu ít hơn. NO_RISE bắt đầu giám sát sau ramp.

Nạp `build/Firmware_BB_V6_BBV6_25_TEST18_RAMP.hex`. App v6.19 dùng tiếp được. Chưa nạp hoặc bật board trong lượt sửa này; trình tự và cạnh GPIO được kiểm tra bằng mô phỏng application/control thực.
