# BBV6.18 — hiển thị ADC, tắt bảo vệ nhiệt mũi

**Lịch sử bản debug 6.18:** firmware hiện tại 6.19 đã trở lại hiển thị °C và bật đầy đủ bảo vệ nhiệt mũi. Xem [SOLDER_NORMAL_VI.md](SOLDER_NORMAL_VI.md); các mô tả tắt bảo vệ dưới đây không áp dụng cho HEX 6.19 mặc định.

Theo yêu cầu debug/hiệu chuẩn, số lớn ở màn hình SOLDER là **ADC thô PA3 0–4095**, không đổi sang °C. Đây là mẫu gần nhất trong khoảng heater đã tắt ít nhất 2 ms. Chưa có mean mới thì hiện `----`. App v6.18 cũng hiển thị ADC PA3.

Màn hình luôn có nhãn **TIP PROTECT OFF / ADC DEBUG**; app hiện **BẢO VỆ NHIỆT MŨI TẮT**. Telemetry `solder.protect=0`.

Các ngắt tay hàn được bỏ trong bản này:

- `SENSOR`: ADC sát rail, nhiệt tính theo hệ số tạm dưới -30°C.
- `HOT`: nhiệt tính theo hệ số tạm vượt 500°C hoặc vượt TARGET+60°C trong 1 s.
- `NO_RISE`: không tăng 5°C sau 15 s cấp nhiệt lớn.

**Vẫn giữ** timeout ADC PA3 (thiếu mean thì dừng burst; mất mean kéo dài thì khóa), kiểm tra rail/precharge, giới hạn V/A/công suất, OVP/OCP/ngắn mạch, bảo vệ nhiệt FET PA0 và timeout vòng điều khiển/watchdog. Không bỏ bảo vệ khối nguồn LAB hoặc bộ sạc pin.

PID nhiệt, nhiệt SET/STAND và hệ số V5 vẫn dùng nội bộ. Chỉ đổi phần hiển thị sang ADC và bỏ các ngắt nêu trên. Raw=0 và raw=4095 đều được chấp nhận khi có mẫu thật; raw=0 có thể khiến PID yêu cầu duty lớn vì nhiệt quy đổi bị kẹp ở 25°C. **Bản này không có ngắt quá nhiệt mũi**, không phải bản đã hiệu chuẩn nhiệt.

Đấu dây giữ BBV6.17: PA10 = SD chung hai IR2184S; PB13 = heater HIGH/LOW; PC15 = chuyển đường; PA3 = nhiệt mũi; PB11 thấp = gác. C245 24 V/6 A, C210 12 V/4 A. Chi tiết: [SOLDER_MODE_VI.md](SOLDER_MODE_VI.md).

Để bật lại bảo vệ nhiệt mũi sau hiệu chuẩn, đổi `PSU_SOLDER_TEMP_PROTECTION` thành `1` trong `Core/Inc/psu_modes.h`, build và nạp lại. Cờ này không ghi vào Flash cấu hình, không đổi PID/preset nguồn đã lưu. Bộ kiểm tra vẫn chạy riêng cả cấu hình debug mặc định và cấu hình bảo vệ đầy đủ (`-DPSU_SOLDER_TEMP_PROTECTION=1`).

HEX mới chưa được nạp lên board trong lượt này. Tránh full-chip erase nếu muốn giữ hai trang Flash chứa PID/preset.
