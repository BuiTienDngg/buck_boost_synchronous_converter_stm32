# BBV6.14 — giới hạn điều chỉnh trên app

Phần cứng cập nhật theo chủ board: MOSFET HY3810, shunt 1 mΩ, INA199 gain 100,
VIN/VOUT chia 220k/10k (hệ số 23), VDDA 3,3 V. PWM vẫn 10 kHz.

| Trường trên app | Ý nghĩa | Trần firmware |
|---|---|---|
| Dòng liên tục tối đa (`i_max`) | Trần Iset, bảo vệ RMS lọc IIR | 15 A |
| Ngưỡng dòng đỉnh (`i_peak`) | OCP thô, cần 2 mẫu liên tiếp | 20 A |
| VIN tối đa (`vin_max`) | Ngắt đầu vào ở ngưỡng này | 60 V |
| VOUT tối đa (`vout_max`) | Trần điện áp đặt | 59,9 V |
| Công suất tối đa (`power_max`) | Trần công suất đầu ra ước lượng | 600 W |
| Ngân sách dòng vào (`input_current_max`) | Giới hạn dòng vào ước lượng | 10 A |

Các giá trị đều giảm được trên app. `i_peak` phải lớn hơn `i_max`; firmware từ chối
cấu hình nếu hiệu chuẩn không còn đủ thang ADC cho giới hạn đã chọn. Trần OVP
tuyệt đối là 60 V; đặt tối đa 59,9 V để đáp ứng yêu cầu VOUT <60 V. Đây là giới hạn
điều khiển/bảo vệ bằng ADC, không bảo đảm quá độ vật lý không bao giờ vượt 60 V.

## Áp dụng mà không mất PID

1. Nạp HEX BBV6.14, mở `PC_App/dist/BB_V6_Studio_v6_14.exe` hoặc `Chay_app.cmd`.
2. Kết nối USB và nhấn **Đọc board**. Giữ LAB/OFF và dừng xả trước khi chỉnh.
3. Trong tab **Cấu hình**, chỉnh sáu trường trên tại nhóm **Giới hạn điện áp**
   và **Dòng và công suất**. Các PID/ramp/hiệu chuẩn đang có giữ nguyên.
4. Nhấn **Áp dụng cấu hình**, chờ xác nhận; sau đó **Lưu Flash** để giữ qua reset.
5. Về **Tổng quan**, đặt V/A theo giới hạn mới rồi **Áp dụng V/A**.

Journal Flash V3 giữ nguyên 37 trường. Firmware đọc lại profile cũ 8 A/10 A,
36 V/40 V/60 W/5 A nếu profile đó đã lưu. Do đó chỉ nạp HEX chưa tự nâng các giá trị
trong Flash. Không cần khôi phục mặc định để nâng giới hạn, tránh mất PID đã tuning.
Nếu không có cấu hình hợp lệ, mặc định mới dùng các mức trần trong bảng; output
khởi động OFF, setpoint ban đầu 5 V/1 A, PC15 = 0.

## Vì sao đặt 600 W vẫn có thể bị giới hạn công suất

Firmware dùng công thức:

`P_cho_phep = min(power_max, VIN × input_current_max × efficiency)`

`I_gioi_han = min(Iset, P_cho_phep / VOUT)`, sau đó giảm thêm nếu FET nóng.

Với hiệu suất ước lượng mặc định 85% và input tối đa 10 A:

- VIN 13,2 V: P cho phép khoảng 112,2 W.
- VIN dưới 60 V: P cho phép dưới khoảng 510 W.
- 600 W là trần cấu hình, không phải công suất luôn có thể đạt với nguồn vào 10 A.

App hiện **P cho phép**, dòng giới hạn thực và hai duty Buck/Boost để đối chiếu.
Nếu VOUT = 10 V, IOUT = 4,93 A khi Iset = 5 A, nguồn có thể đang ở CC do tải;
nâng trần công suất không tự làm vượt dòng đặt 5 A.

Board chưa đo dòng đầu vào: `input_current_max` chỉ giới hạn theo mô hình công suất,
không thay thế bảo vệ dòng đầu vào bằng phần cứng. Dòng cuộn cảm khi boost có thể
lớn hơn dòng output đo được. Các mức mới chưa được xác nhận trên tải thật; kiểm tra
rating tụ, cuộn cảm, diode bootstrap, PCB/tản nhiệt và bảo vệ trước khi thử dải cao.
Ở VOUT gần 60 V, xung xả tối thiểu 2 µs có ΔI danh định khoảng 1,24 A với L=97 µH;
dòng xả này vẫn không đi qua shunt output.

## Thay đổi kỹ thuật

Ngưỡng ADC, OVP, giới hạn công suất và `caps` USB lấy theo cấu hình mới. Sửa phép
đổi mV/mA sang float để lệnh đúng 59,9 V/15 A không bị từ chối do sai số làm tròn.
App lấy schema/capabilities từ firmware và chỉ cho gửi các giá trị phù hợp.
Các kiểm tra thực hiện bằng mô phỏng Cortex-M3 và app demo; chưa gửi lệnh ON,
thay đổi cấu hình thật qua USB, hoặc nạp board trong lượt cập nhật này.
