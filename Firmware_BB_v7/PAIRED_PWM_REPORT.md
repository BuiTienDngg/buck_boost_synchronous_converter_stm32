# Điều khiển hiện tại: D và 1−D — 27/10/2026

Đã thực hiện yêu cầu quay về hai nhánh PWM phối hợp:

- TIM1_CH1 / PA8, IR2184 nhánh Buck: duty high-side D.
- TIM1_CH2 / PA9, IR2184 nhánh Boost: duty high-side 1−D.
- Cả hai kênh dùng PWM1, cùng mốc chu kỳ 20 kHz. Đây là hai duty high-side cộng bằng 100%, không phải hai ngõ ra complementary HO/LO của một driver.
- Bỏ lịch refresh 500 Hz, bỏ nhánh pass giữ gần 100% và bỏ DMA1_CH5 refresh. Hai bootstrap được nạp trong khoảng LOW của từng chu kỳ PWM.
- Ghi hai CCR preload theo cặp, thu được CCR2 = 3600 − CCR1 sau khi làm tròn, latch ở update kế tiếp; không còn độ trễ bộ đệm DMA 1–2 ms.
- TIM3 giữ 2 kHz; CV/ramp 200 Hz. Các nhãn Buck/Boost/BB giữ hysteresis cho UI nhưng đều dùng chung ánh xạ D/(1−D), không reset PI theo nhãn.
- Giữ LOW/HIGH tối thiểu 3 µs: D thực tế 6..94% ở 20 kHz, còn chịu giới hạn DUTY_MAX_PM đã lưu. Giữ nạp bootstrap ban đầu ít nhất 2 ms mỗi lần bật driver; không lặp theo chu kỳ 500 Hz.
- Giữ /SD LOW ở OFF/FAULT, chẩn đoán Init/GET DIAG, còi, ADC/OVP/OCP/Vin, CV/CC, ramp, UI/USB và Flash.

Nguồn sửa trong lần này: `Core/Src/power_stage.c`, `Core/Inc/power_stage.h`, `Core/Src/stm32f1xx_it.c`, `Core/Inc/stm32f1xx_it.h`.
Hai file kiểm thử `tests/refresh_fixture.c` và `tests/run_refresh_tests.py` đã cập nhật cho D/1−D; giữ tên cũ để chạy lại thuận tiện.

Kiểm chứng: build Keil ARMCLANG 6.23, 0 lỗi / 0 cảnh báo; 434 assertion mô phỏng ARM hoàn tất, 0 lỗi. Đã kiểm tra D + (1−D), độ rộng tối thiểu, trạng thái OFF/FAULT, precharge, PI/CV/CC/ramp/Flash và 40 chu kỳ liên tiếp không có frame refresh 2 ms.
Kiểm thử cân bằng volt-second là mô hình lý tưởng, không thay thế đo output hoặc gate thật.

Log: `MDK-ARM/paired_pwm_build.log`, `MDK-ARM/paired_pwm_tests.log`; địa chỉ HEX và SHA-256 trong `MDK-ARM/paired_pwm_audit.json`.

HEX: 54,112 byte dữ liệu, đến 0x0800d35f; không chạm vùng lưu cài đặt 0x0800F800.

Bản sao trước sửa: `D:\My_project\buck_boost_converter\Firmware_BB_v7_backup_D1D_20261027_201151\before_D1D.zip`. ZIP và SHA-256 của toàn bộ file đã kiểm tra.

Chưa nạp firmware xuống board hoặc đo lại gợn thực tế.
