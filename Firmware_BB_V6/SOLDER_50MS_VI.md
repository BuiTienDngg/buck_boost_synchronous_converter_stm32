# BBV6.29 — chu kỳ 50 ms, OFF 15 ms

Đơn vị hiện tại là **mili giây**: PB13 LOW ít nhất 15 ms trong mỗi chu kỳ 50 ms, HIGH 0..35 ms theo nhiệt đặt/đo. Không còn chu kỳ 50 giây của các bản 6.26–6.28. PB13 vẫn là GPIO output, không dùng TIM1_CH1N. PWM nguồn buck/boost vẫn 10 kHz.

## Điều khiển và đọc nhiệt

- Sau trình tự chuyển đường nguồn và chờ rail ổn định như cũ, mỗi frame bắt đầu bằng 15 ms OFF.
- PA3 chỉ được lấy mẫu khi heater thực sự tắt ít nhất 2 ms. Tính trung bình mẫu mỗi 5 ms trong khoảng OFF, để lấy được nhiệt mới trước mốc ON 15 ms. Không dùng mẫu cũ từ frame trước để cho phép ON.
- Chốt một lần thời gian ON: `clamp((nhiệt đặt - nhiệt đo)/100°C, 0, 1) × 35 ms`. Độ phân giải GPIO là 1 ms; 50% tương đương 17,5 ms được làm tròn thành 18 ms.
- Hết thời gian ON thì PB13 LOW đến frame mới. Tổng OFF luôn ít nhất 15 ms. Mẫu nhiệt giữ nguyên trong khoảng HIGH tối đa 35 ms, sau đó được cập nhật khi LOW; không phải giữ nguyên nhiều giây như bản trước.
- Thiếu mẫu mới trong cửa sổ OFF thì từ chối ON và dừng bằng lỗi nhiệt STALE. Gác tay vẫn lọc khoảng 300 ms; không rút thành 15 ms theo nhịp xử lý mẫu mới.
- OFF, fault, sụt rail và watchdog giữ cơ chế ngắt như trước. OCP không bị bỏ qua; việc đổi thời gian không chứng minh đã xử lý nguyên nhân dòng đỉnh.

## App và cấu hình

App v6.28 tiếp tục dùng được. Hai thông số áp/dòng tay hàn, journal V4, PID, preset và cấu hình đã lưu giữ nguyên. Chỉnh tại **Cấu hình → Tay hàn → Áp dụng cấu hình → Lưu Flash** khi OFF. Xem [hướng dẫn áp/dòng](SOLDER_APP_CONFIG_VI.md).

LCD hiển thị `50MS CYCLE`. Telemetry: `cycle_ms=50`, `quiet_ms=15`, `on_ms=0..35`, `cycle_pos_ms=0..49`. `duty_pm` vẫn là lệnh tức thời 0/1000, `heater` là mức GPIO; không lấy một mẫu USB để suy ra toàn bộ dạng xung 20 Hz. Trường cũ `pwm_hz=0` cho biết không dùng PWM timer cho heater; chân GPIO vẫn bật/tắt theo frame 50 ms.

## Build và kiểm thử

Nạp `build/Firmware_BB_V6_BBV6_29_ONOFF50MS_OFF15MS.hex` (GCC), hoặc `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex` (Keil). Chưa nạp hoặc bật board trong lượt sửa này.

Kiểm thử trên mã C application/control thực bằng Cortex-M3 giả lập: bắt đầu ON tại 15 ms, ON tối đa 35 ms; ở mức 50%, cạnh lên cách nhau đúng 50 ms và xung dài 18 ms. Kiểm tra chỉ lấy mẫu sau ít nhất 2 ms LOW, cập nhật nhiệt khi tăng ADC, dừng gia nhiệt khi đạt nhiệt đặt, mất mẫu, thiếu nhiệt tăng, đổi nhiệt đặt/gác tay, sụt rail, bật lại, tràn tick và OCP. Bảo toàn các kiểm thử cấu hình áp/dòng qua USB.

Mô phỏng không xác nhận điện áp thực tại PB13, hiệu chuẩn cảm biến, dòng đỉnh hay đáp ứng nhiệt trên board.
