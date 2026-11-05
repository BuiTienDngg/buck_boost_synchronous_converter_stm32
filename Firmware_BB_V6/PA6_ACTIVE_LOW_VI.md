> Historical v6.37 document. Superseded by v6.39 active HIGH; see PA6_ACTIVE_HIGH_VI.md.

# BBV6.37 — PA6 active LOW, SPI1 master TX-only, TIM4 CC/CV

Đã sửa trực tiếp project theo cấu hình STM32CubeMX người dùng vừa tái tạo. Giữ SPI1 phần cứng: master, SPI_DIRECTION_1LINE (Transmit Only), PA5 SCK/PA7 MOSI, không khởi tạo PA6 cho SPI. Không chuyển LCD sang SPI phần mềm. HAL SPI_1LINE_TX chọn chiều MOSI output.

TIM3_CH1 PA6: timer clock 72 MHz, PSC=0, ARR=7199, PWM1 với TIM_OCPOLARITY_LOW (CC1P=1), f=10 kHz. TIM4 base interrupt PSC=71, ARR=999 tạo nhịp 1 kHz chạy Psu_Tick1kHz cho CC/CV và trạng thái ứng dụng. TIM3 không chạy ngắt PID.

## Cực tính tay hàn

- PA6=0 V: heater ON.
- PA6=3,3 V: heater OFF.
- Duty nhiệt 0%: CCR1=0; PA6 được giữ HIGH ở chế độ GPIO, không treo LOW khi timer dừng hoặc khi chờ cảm biến/rail.
- Duty nhiệt 50%: CCR1=3600; mỗi chu kỳ 100 µs có 50 µs LOW cấp nhiệt, 50 µs HIGH tắt.
- Duty nhiệt tối đa hiện tại 95%: CCR1=6840, LOW 95 µs/HIGH 5 µs; duy trì cạnh PWM khi PID bão hòa.
- Khung nhiệt 50 ms giữ nguyên: 10 ms đầu HIGH để đọc nhiệt PA3, 40 ms tiếp theo băm PWM theo PID nhiệt. Không còn ép LOW trong thời gian đọc nhiệt.

Boot, OFF, PID=0, lỗi, hết lease và mất nhịp TIM4 đều đưa PA6 về HIGH. Latch GPIO HIGH trước khi đổi chế độ chân; khi phát PWM, cấu hình cực tính LOW và compare trước rồi mới đưa PA6 về AF. Main/MSP và tools/integrate.py được cập nhật để giữ đúng mức tắt sau khi generate CubeMX. PB13 cũ vẫn giữ LOW, không phải tín hiệu điều khiển tay hàn nữa.

Telemetry solder.active_low=1; duty_pm là phần LOW/cấp nhiệt, heater là trạng thái cho phép PWM, không phải điện áp tức thời ngoài chân. ccr1 là compare TIM3_CH1. Khối pwm chung vẫn mô tả TIM1 nguồn.

## Kiểm tra và giới hạn

Kiểm thử Cortex-M3: PWM/PID/active-LOW 68/68 cho mỗi cấu hình; gồm khởi động OFF HIGH, 10 ms quiet HIGH, OCP/watchdog OFF HIGH, cực tính CC1P và duty. Foreground 11/11; lease 8/8; pin test 9/9; remote 115/115; application 110/110. GCC/Keil build đạt, HEX không chạm vùng settings 0x0800F800, tích hợp CubeMX chạy lại không đổi kết quả.

Chưa nạp bản active-LOW lên board, chưa xác nhận dạng sóng PA6 và LCD chạy đồng thời ở cấu hình mới. Không thay hoặc chứng nhận khả năng dùng đồng thời SPI1 và PA6 TIM3 trên phần cứng F103; đã giữ phương án SPI1 theo chỉ dẫn người dùng. Bản trước báo CCR khác 0 nhưng người dùng đo PA6 LOW là hiện tượng còn cần kiểm tra bằng bản mới và phép đo vật lý. Các bản HEX active-HIGH trước đây không áp dụng cho mức điều khiển mới.
