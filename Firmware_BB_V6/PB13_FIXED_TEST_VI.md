# BBV6.24-TEST18-10HZ — xung test PB13 độc lập ADC

Bản mới BBV6.25 thêm trình tự switch và ramp duty 0→50% trong 2 giây; xem [SOLDER_STARTUP_VI.md](SOLDER_STARTUP_VI.md). Phần dưới lưu chẩn đoán và hành vi bản 6.24 trước đó.

Đã đọc board qua COM14, GET xác nhận BBV6.23. Ở SOLDER RUN, nguồn 18 V đã ổn định và không có fault, nhưng PA3=4094–4095. Công thức nhiệt tạm suy ra khoảng 3213°C nên PID của bản 6.23 đặt duty=0, vì vậy PB13 LOW. Tắt các lỗi nhiệt trước đó không có nghĩa bỏ điều khiển PID nhiệt. Capture tại `build/diagnostics/pb13_live.jsonl` ghi diễn biến này trước khi thay firmware.

BBV6.24 sửa TEST18 thành xung cố định 10 Hz, HIGH 50 ms / LOW 50 ms sau khi rail 18 V ổn định và SOLDER RUN. ADC vẫn lấy trong khoảng LOW nhưng giá trị nhiệt quy đổi không quyết định duty test. LCD ghi ON 50MS / OFF 50MS / 10HZ TEST. Chế độ thường (`PSU_SOLDER_TEST18=0`) vẫn dùng PID nhiệt, cửa sổ 100 ms và giới hạn duty 95%.

PB13 được xác nhận lại GPIO output push-pull 2 MHz, không pull, LOW tại `PsuApp_Init`, ngoài cấu hình MX_GPIO_Init hiện có. Ngắt touch EXTI15_10 không bật. PA10 giữ SD. Không đổi PWM buck/boost 10 kHz, vòng điều khiển TIM3 1 kHz, giới hạn nguồn 18 V/6 A và PID CV/CC đã lưu.

Telemetry `solder` thêm:

| Trường | Ý nghĩa |
|---|---|
| `fixed_pwm` | 1 trong TEST18: duty cố định 50%, không dùng PID nhiệt |
| `pin_mode` | Nibble PB13 của GPIOB CRH; 2 là output push-pull 2 MHz |
| `pin_odr` | Mức lệnh ngõ ra PB13 tại snapshot |
| `pin_idr` | Mức chân PB13 được GPIO đọc lại tại snapshot |
| `rises` | Số lần firmware đổi lệnh PB13 LOW→HIGH, tích lũy từ lúc boot |

`duty_pm=500`, `pwm_hz=10`, `fixed_pwm=1` khi RUN; `rises` tăng khoảng 10/s. Telemetry 10 Hz có thể luôn chụp vào cùng một pha xung nên `heater`, `pin_odr`, `pin_idr` có thể trông không đổi giữa các dòng; bộ đếm cạnh không bị vấn đề này. `rises` là số lệnh cạnh, không phải phép đo bằng input-capture. Nếu ODR HIGH mà IDR LOW, cần kiểm tra đường PB13 và tải ngoài, gồm dây T_IRQ cũ.

Nguồn OFF, emergency, fault nguồn hoặc mất mẫu PA3 vẫn ngắt heater. Bảo vệ 2 ms khi TIM3 ngừng vẫn giữ, dù xung HIGH kéo dài 50 ms. Không bỏ các bảo vệ dòng/áp, FET, watchdog, TIMING. ADC bão hòa thực tế vẫn cần kiểm tra mạch đo trước khi quay lại điều nhiệt; bản này phục vụ đo xung, không xác nhận nhiệt độ mũi.

Nạp `build/Firmware_BB_V6_BBV6_24_TEST18_10HZ.hex`, kết nối lại app để đọc tên firmware và các trường mới. App v6.19 vẫn tương thích. Lượt này chỉ đọc board, không gửi ON/OFF hoặc ghi cấu hình; chưa tự nạp bản mới hay bật heater bằng máy tính.
