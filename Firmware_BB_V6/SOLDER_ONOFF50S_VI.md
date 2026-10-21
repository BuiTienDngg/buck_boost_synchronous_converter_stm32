# Cập nhật BBV6.28

Điện áp và giới hạn dòng tay hàn đã chuyển sang cấu hình trên app, lưu Flash được; giá trị mặc định vẫn 18 V/6 A. Chu kỳ 50 giây và OFF 15 giây dưới đây giữ nguyên. [Xem hướng dẫn hiện tại](SOLDER_APP_CONFIG_VI.md). Phần ghi chú 6.27 bên dưới mô tả bản trước khi thêm hai trường settings.

# BBV6.27-ONOFF50S-OFF15S — heater GPIO chu kỳ 50 giây

Đơn vị theo yêu cầu là **giây**: mỗi chu kỳ dài 50.000 ms, OFF ít nhất 15.000 ms, ON tối đa 35.000 ms. Không dùng PWM heater 10 Hz hoặc ramp duty 2 giây của BBV6.25 nữa.

Bản 6.27 tăng OFF từ 5 giây của bản 6.26 lên 15 giây, giữ tổng chu kỳ 50 giây; ON tối đa giảm từ 45 xuống 35 giây.

## Trình tự và thời gian bật

- Giữ trình tự cũ: heater LOW, chờ OFF 100 ms, PC15 HIGH, chờ switch 200 ms, bật DC-DC và chờ nguồn ổn định 100 ms.
- Sau khi vào RUN, mỗi chu kỳ bắt đầu bằng **15 giây OFF**. PA3 chỉ được lấy mẫu sau khi heater thực sự tắt tối thiểu 2 ms; tính trung bình mỗi 100 ms.
- Hết 15 giây, chốt thời gian ON từ nhiệt đo mới của chính chu kỳ đó. PB13 HIGH liên tục trong thời gian đã chốt, sau đó LOW đến hết chu kỳ. Không chèn một lần ON mới giữa chu kỳ.
- Công thức tỷ lệ tạm: `fraction = clamp((nhiệt đặt - nhiệt đo) / 100°C, 0, 1)`, `on_ms = fraction × 35000`. Đây là điều khiển tỷ lệ theo sai lệch nhiệt, không phải PID đã tuning.
- Ví dụ đặt 300°C: đo 200°C hoặc thấp hơn → ON 35 s; đo 250°C → ON 17,5 s; đo từ 300°C → không ON. Thời gian còn lại đều OFF, vì vậy tổng OFF luôn ít nhất 15 s.
- Giảm nhiệt đặt/gác tay có thể rút ngắn lần ON hiện tại theo nhiệt đo gần nhất. Tăng nhiệt đặt hoặc nhấc tay khỏi gác không kéo dài/chèn thêm ON trong chu kỳ đã chốt; chờ chu kỳ mới.
- OFF, fault hoặc rail sụt làm dừng heater. Khởi động lại hoặc phục hồi rail đều phải đi lại khoảng OFF trước khi gia nhiệt.

## Phần cứng và bảo vệ

PB13 vẫn là GPIO push-pull 2 MHz, HIGH=heater ON, LOW=OFF. PB13 có chức năng phụ TIM1_CH1N nhưng không được cấu hình alternate function. Khởi tạo chủ động tắt CC1NE, CC2NE, CC3NE để không cho TIM1 điều khiển ngõ bổ sung; PA8/PA9 vẫn là PWM nguồn 10 kHz.

Giữ rail 18 V/6 A cho cả C245/C210 (`PSU_SOLDER_TEST18=1` chỉ chọn rail, không còn ép heater 50%). Bật lại kiểm tra nhiệt mũi SENSOR/HOT/NO_RISE mặc định vì heater đã trở lại điều khiển theo nhiệt. Không thay các PID CV/CC, cấu trúc settings, giới hạn hay preset Flash.

OCP/OVP/VIN, nhiệt FET, ADC nguồn liên tục, kiểm tra thời gian và watchdog vẫn hoạt động. Watchdog heater 2 ms được gia hạn mỗi tick 1 kHz, kể cả khi GPIO HIGH liên tục nhiều giây.

**Giới hạn đo:** khi heater ON, nhiệt hiển thị giữ mẫu cuối lúc OFF; firmware không thể phát hiện nhiệt mũi mới trong tối đa 35 giây ON. Timeout PA3 200 ms áp dụng khi OFF, có thời gian trở lại lấy mẫu sau cạnh tắt; không áp dụng nhầm cho khoảng ON được lập lịch. NO_RISE dùng thời gian gia nhiệt thực đã tích lũy và được đánh giá khi có mẫu OFF mới. Hệ số nhiệt còn tạm: ADC 1200≈30°C; độ dốc C245=1,1°C/count, C210=1,5°C/count. Chưa hiệu chuẩn nhiệt nóng hoặc kiểm tra trên board.

Thay đổi chu kỳ không chứng minh đã xử lý nguyên nhân quá dòng trong log BBV6.25. Xung dòng lúc đóng heater vẫn có thể làm OCP ngắt ngay.

## LCD, app và telemetry

LCD hiển thị nhiệt mũi, nhiệt đặt, `50S CYCLE`, tỷ lệ `ON n/100` trong 35 giây, và trạng thái HEAT/READ/STAND. App v6.19 vẫn nhận giao thức cũ và hiển thị nhiệt; không cần thay settings hoặc profile.

- `pwm_hz=0`, `fixed_pwm=0`: không còn PWM heater định kỳ 10 Hz.
- `cycle_ms=50000`, `quiet_ms=15000`.
- `on_ms`: ngân sách ON đã chốt, 0..35000.
- `cycle_pos_ms`: vị trí trong chu kỳ, 0..49999.
- `sample_age_ms`: tuổi mẫu nhiệt đã tính trung bình, có thể tăng trong ON.
- `duty_pm`: lệnh GPIO tức thời, 0 hoặc 1000; không phải tỷ lệ ON trong 35 s. Dùng `on_ms/35000` để tính tỷ lệ đó.
- `heater`, `pin_mode`, `pin_odr`, `pin_idr`, `rises` giữ ý nghĩa chẩn đoán GPIO.
- `ramp_ms=0`, `ramp_pm=0` giữ lại để tương thích trường cũ; `protect=1`.

## Build và kiểm thử

HEX GCC: `build/Firmware_BB_V6_BBV6_27_ONOFF50S_OFF15S.hex`; HEX Keil: `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex`. Cả hai được build từ cùng nguồn; không ghi vào hai trang cấu hình từ 0x0800F800.

Nếu chưa có thư viện giả lập, cài `python -m pip install --target tools/_deps unicorn==2.1.4`. Chạy `python tools/test_modes.py` để kiểm tra sạc/menu/nút/giao diện và heater mới ở cả rail TEST18 và rail theo loại mũi. `tests/solder_onoff_fixture.c` thay thế các fixture heater PWM/TEST18 cố định cũ; các fixture cũ vẫn giữ lại để tham khảo lịch sử, không được chạy bởi runner hiện tại.

Kiểm thử dùng mã C thực chạy trên Cortex-M3 giả lập, có kiểm tra GPIO/ADC, 15 s OFF, 35 s ON, 17,5 s ON, nhiệt đạt đích, thay nhiệt đặt/gác tay, thiếu mẫu, SENSOR/HOT/NO_RISE, sụt rail, OCP, bật lại và tràn bộ đếm ms. Đây không phải mô phỏng nhiệt hoặc kiểm chứng công suất thực. Chưa nạp hoặc bật board trong lượt sửa này.
