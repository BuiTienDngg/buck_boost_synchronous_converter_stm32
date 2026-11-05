# BBV6.33 — PA6 TIM3_CH1 PWM 10 kHz; PID TIM4

Đã sửa trực tiếp project; chưa nạp hoặc chạy tải trên board trong lượt này.

- SPI1 master TX-only dùng SPI_DIRECTION_1LINE. SCK=PA5, MOSI=PA7; SPI không cấu hình hoặc giải phóng lại PA6. HAL_SPI_Transmit chọn chiều TX qua BIDIOE; LCD chỉ dùng truyền.
- PA6 = TIM3_CH1, không remap. Clock timer 72 MHz, PSC=0, ARR=7199, PWM1 active HIGH. CCR1=0/3600/7200 tương ứng duty 0/50/100%.
- TIM3 chỉ phát PWM, không bật ngắt update. TIM4 PSC=71, ARR=999, ngắt 1 kHz chạy vòng CV/CC, trạng thái tay hàn và lấy mẫu; ưu tiên ngắt giữ mức 1. TIM1 buck/boost và TIM2 encoder giữ vai trò hiện có. TIM4 dùng base interrupt, không chiếm các chân PB6..PB9.
- Giữ khung nhiệt 50 ms: OFF hoàn toàn 15 ms đầu, đọc PA3 sau tối thiểu 2 ms yên tĩnh; 35 ms sau dùng PWM 10 kHz. Duty theo nhiệt hiện tại; chỉ dùng mẫu mới của khung. 50% là PWM 50% suốt 35 ms, không còn một xung GPIO dài 18 ms.
- PB13 cũ luôn LOW. OFF, OCP/OVP, lỗi cảm biến, USB OFF và watchdog đều ép TIM3_CH1 inactive/CCR1=0. SysTick tắt carrier nếu TIM4 không gia hạn trong 2 ms; IWDG vẫn hoạt động.
- LCD hiện 10KHZ PWM. Telemetry protocol 1: solder.pwm_hz=10000, pin=PA6, timer=TIM3_CH1, ccr1 là compare tay hàn; heater là trạng thái cho phép PWM, không phải mức chân tức thời. on_ms=35 khi có yêu cầu nhiệt, 0 khi không yêu cầu; duty_pm=0..1000. Các trường lịch sử ccr3/test18/ramp_ms/ramp_pm/fixed_pwm/pin_odr/pin_idr của solder đã bỏ. Nhóm pwm chung của TIM1 vẫn giữ nguyên. Settings V4/áp-dòng app không đổi.

## Đấu dây trước khi nạp

Chuyển tín hiệu gate/driver tay hàn từ PB13 sang PA6. Tách PA6 khỏi đường MISO/SDO ngoại vi cũ nếu đường đó đang được nối hoặc có thể lái chân. PA5/PA7 vẫn nối LCD như cũ.

## Kiểm tra

GCC và Keil build thành công; kiểm tra HEX từng địa chỉ trong vùng chương trình 62 KiB, không ghi vào settings tại 0x0800F800. Keil nén dữ liệu RW khi tạo load image; script kiểm tra kích thước HEX thực thay vì cộng kích thước RW giải nén.

Kiểm thử Cortex-M3: PWM 48/48 cho mỗi cấu hình release và normal; OCP tắt qua ADC/DMA, watchdog TIM4 stall, TIM3 callback không chạy PID, 50% CCR1=3600 và 15 ms quiet. Foreground 11/11, lease 8/8, remote 115/115, application 110/110, modes 149/149, menu 44/44, shortcut 17/17. Đây là kiểm thử phần mềm, chưa xác nhận dạng sóng thực hoặc ổn định PID trên board.

Firmware: build/Firmware_BB_V6.hex (GCC); MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex (Keil). Cấu hình CubeMX và tools/integrate.py đã cập nhật; kiểm tra idempotence và vector TIM4 đạt.
