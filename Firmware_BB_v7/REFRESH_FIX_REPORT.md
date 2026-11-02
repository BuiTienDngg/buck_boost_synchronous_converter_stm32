> **Lịch sử:** Báo cáo này mô tả bản refresh 500 Hz trước đó. Theo yêu cầu mới, firmware hiện đã về D / (1−D), xem [báo cáo hiện tại](PAIRED_PWM_REPORT.md).

# Sửa điều khiển Buck–Boost và lỗi khởi tạo — 27/10/2026

Dự án: `D:\My_project\buck_boost_converter\Firmware_BB_v7\MDK-ARM\Firmware_BB_V6.uvprojx`.

Đã sửa trực tiếp nguồn dự án, rebuild bằng Keil ARMCLANG 6.23: **0 lỗi, 0 cảnh báo**. Đã tạo lại AXF và HEX. Chưa nạp firmware hoặc thực hiện phép đo trên board.

## Lỗi khởi tạo: điều gì đã xác minh

- Code cũ đưa `PowerStage_Init() != HAL_OK` vào `Error_Handler()`, nơi khóa ngắt và lặp vô hạn. Nếu PC14 vừa được bật báo lỗi thì còi giữ mức bật; USB enumeration trước đó không chứng minh vòng điều khiển/CDC còn chạy.
- Trong snapshot nhận lúc bắt đầu sửa, `main.c` đã cấu hình TIM1 PSC=0/ARR=3599 và TIM3 PSC=71/ARR=499. Thử chạy hàm khởi tạo cũ với driver TIM HAL thật của dự án trong mô hình thanh ghi ARM: **trả HAL_OK** với cấu hình này. Không tái hiện được chính xác lần HAL_ERROR trên board từ snapshot đó.
- Đã xác minh `Firmware_BB_V6.ioc` còn **TIM3 ARR=999**, tương đương 1 kHz. Regenerate theo file này sẽ vi phạm điều kiện 2 kHz trong `PowerStage_Init`. Đã sửa ARR thành 499. Kiểm thử tái hiện được HAL_ERROR khi dùng ARR=999.
- Cả hai kênh TIM1 trong `main.c` hiện là PWM1. Tên CubeMX `Generation2 CH2` là tên kênh, không chứng minh OC2M là PWM2. Kiểm thử có chủ động đổi OC2M sang PWM2 và xác nhận bị từ chối đúng.
- Không bỏ qua kiểm tra timer. Đã tách mã chẩn đoán: handle, tần số, chế độ PWM, cấu hình timer, PWM Start CH1/CH2, DMA và Start TIM3. Chuẩn hóa cách đọc trường OC2M để dễ kiểm tra; phép so sánh OC2M cũ với PWM1 dịch 8 bit vốn đúng.
- Lỗi khởi tạo công suất hiện giữ `/SD=LOW`, giữ vòng chính và ngắt phục vụ UI/USB. Không đi vào `Error_Handler` từ nhánh này. Các lỗi hệ thống khác vẫn có xử lý dừng, nhưng đã thêm ngắt gate trước khi dừng.

## Điều khiển và refresh phần cứng

| Thành phần | Cấu hình |
|---|---|
| TIM1 | Up-count, 72 MHz, PSC=0, ARR=3599: 20 kHz, chu kỳ 50 µs |
| TIM3 | 72 MHz, PSC=71, ARR=499: 2 kHz |
| Vòng CV và ramp | 200 Hz |
| Refresh | Mỗi 40 chu kỳ TIM1: 500 Hz |
| Độ rộng yêu cầu IN thấp | 10 µs = 720 tick TIM1 |
| Nhánh giữ high-side | CCR=3600 trong 39 frame; CCR=2880 trong frame refresh: trung bình 99,5% |
| PWM thường | HIGH và LOW tối thiểu 3 µs; trần vật lý 94% ở 20 kHz |
| Nạp bootstrap ban đầu | Giữ hai IN thấp ít nhất 2 ms sau yêu cầu chạy hợp lệ |
| Cập nhật CCR | TIM1 update → DMA1 Channel5 → TIM1 DMAR, burst CCR1/CCR2; latch đồng thời ở update kế tiếp |

**Buck:** nhánh Buck PWM; nhánh Boost giữ high-side, có refresh 500 Hz. **Boost:** đối xứng. Trong cửa sổ refresh, cũng giới hạn xung nhánh đang PWM để hai IN đều thấp suốt 10 µs cuối chu kỳ. Khi hai low-side thực sự dẫn, hai đầu cuộn cảm được đưa gần GND; tránh tạo thêm một khoảng áp +Vin hoặc −Vout kéo dài 10 µs chỉ để nạp bootstrap. Đã bù tổn thất volt-second trung bình do cắt ngắn frame refresh.

**Buck–Boost:** cả hai IN dùng PWM1 cùng mốc chu kỳ, với cặp duty high-side D và 1−D khi chưa bão hòa. Có các vector chung HIGH/HIGH và LOW/LOW; không phải hai xung ngược pha. Tỉ số điện áp lý tưởng CCM vẫn là Dbuck_HS / Dboost_HS. Low-side được IR2184 tạo bù với high-side của chính nhánh đó.

PI dùng cùng tọa độ duty Buck–Boost tương đương cho cả ba chế độ. Khi đổi chế độ, nạp lại tích phân CV để giữ lệnh trước đó, đồng thời giữ phần tích phân giới hạn dòng CC. Có hysteresis với biên tăng theo Vin để tránh chạy PWM sát 100%; có chống tích phân bão hòa và slew limit. `DUTY_MAX_PM` giới hạn các khóa làm tăng truyền năng lượng: Buck high-side và Boost low-side; giảm giới hạn này không được làm tăng gain Boost. Nhánh pass có refresh được miễn giới hạn này theo chiến lược yêu cầu.

DMA dùng bộ đệm vòng 40 frame, cập nhật nửa đã đọc xong. Xung refresh và cặp CCR không phụ thuộc độ trễ ngắt. **Độ trễ áp dụng lệnh PI khoảng 1–2 ms, cộng tối đa một chu kỳ 50 µs để latch**, dù TIM3 vẫn tính ở 2 kHz; cần tính tới điều này khi chỉnh gain. Các lệnh OFF/trip tác động thẳng `/SD`, không chờ bộ đệm.

## Deadtime và giới hạn của lựa chọn 10 µs

Thông số người dùng cung cấp: mỗi bootstrap 1 µF // 100 nF, VCC IR2184 = 11 V, MOSFET IRF3710, L = 98 µH.

[Datasheet IR2184, trang 3](https://www.infineon.com/assets/row/public/documents/24/49/infineon-ir21844s-datasheet-en.pdf) ghi deadtime 280/400/520 ns, trễ bật tối đa 900 ns, rise time tối đa 60 ns ở điều kiện thử nêu trong bảng. Vì thế xung IN thấp 300–500 ns không bảo đảm LO đã bật. TIM1 deadtime vẫn bằng 0 vì PA8/PA9 điều khiển hai chân IN độc lập; deadtime nội nhánh do IR2184 thực hiện, không phải TIM1 complementary outputs.

10 µs là **độ rộng IN**, không phải lời xác nhận LO dẫn trọn 10 µs hoặc tụ đã được nạp đủ. Với 11 V, biên VBS cần được đo thật. Từ dòng tĩnh bootstrap tối đa 150 µA, cộng ngân sách leakage 50 µA của bảng IR2184 và [Qg IRF3710 tối đa 130 nC ở điều kiện thử của datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf3710-datasheet-en.pdf), sụt áp lý tưởng sau 2 ms khoảng `(200 µA × 2 ms + 130 nC) / 1,1 µF ≈ 0,48 V`. Chưa gồm suy giảm điện dung theo bias, nhiệt độ, đặc tính diode, điện trở đường nạp hoặc linh kiện khác phiên bản. Độ hồi phục trong 10 µs phụ thuộc đường nạp; không thể xác nhận chỉ từ điện dung.

Nếu chỉ phát refresh một nhánh khi nhánh kia vẫn đặt áp lên L, 28 V trong 10 µs trên 98 µH có thể đổi dòng khoảng 2,86 A. Đó là lý do ghép cửa sổ LOW/LOW. Đây là tính toán lý tưởng; vẫn cần kiểm tra gate và switch-node trên phần cứng.

## Bảo vệ, còi, USB và Flash

- OFF, FAULT và mọi lỗi Init: `/SD=LOW` ngay. Khác code cũ giữ PA10 cao và hai low-side bật khi duty=0. Bộ đệm DMA không thể tự nâng `/SD` sau khi đã trip.
- PC14 mặc định active-high, còi 200 ms bật / 300 ms tắt. BTN1 xóa lỗi bảo vệ có thể phục hồi về OFF; cần nhấn riêng lần nữa để chạy. Lỗi Init/DMA phần cứng không được xóa thành trạng thái cho phép bật; BTN1 chỉ tắt tiếng, cần khắc phục và reset.
- OVP/OCP/Vin được kiểm tra thêm trên trung bình ADC mới trước IIR để bộ lọc không che lỗi. ADC stale đã giảm từ 200 ms xuống 5 ms; mỗi block 30 scan hiện khoảng 0,84 ms.
- Có phát hiện DMA lỗi, DMA không tiến triển và TIM3 không được phục vụ. NMI/HardFault/MemManage/BusFault/UsageFault và Error_Handler có gọi ngắt gate.
- Đây vẫn là bảo vệ dựa trên **dòng đầu ra**, không phải đo dòng cuộn cảm theo từng chu kỳ. Không thay thế comparator OCP hoặc interlock phần cứng.
- Giữ giao thức USB hiện có; thêm lệnh đọc `GET DIAG`. Không thay cấu hình USB, format telemetry, phần PC hoặc định dạng Flash V10. File `psu_flash.c` và project `.uvprojx` không đổi.
- Giữ lưu hai trang Flash CRC, setpoint CV/CC, ramp lên/xuống, encoder, nút và LCD. Chỉ lưu Flash ở OFF. Đã kiểm tra ảnh HEX không chạm hai trang cài đặt.

Gửi qua cổng CDC, có ký tự xuống dòng:

```text
GET DIAG
```

Trả về các dòng `D,INIT,...`, thanh ghi TIM1/TIM3 và trạng thái HAL hai kênh, kết thúc `OK,DIAG`. Mã INIT: 0 OK; 1 handle; 2 tần số; 3 PWM mode; 4 timer config; 5 Start CH1; 6 Start CH2; 7 DMA; 8 Start TIM3. Với firmware này, dữ liệu này giúp xác định lần HAL_ERROR thực tế thay vì suy đoán từ còi.

## File đã sửa

Đối chiếu SHA-256 với bản sao trước sửa: đúng 8 file nguồn/cấu hình sau thay đổi.

| File tương đối từ thư mục Firmware_BB_v7 | Thay đổi |
|---|---|
| `Core/Src/power_stage.c` | Init diagnostics, DMA refresh, ba chế độ, PI, bảo vệ, /SD và xử lý còi |
| `Core/Inc/power_stage.h` | Cấu hình refresh/pulse, chẩn đoán, API DMA/emergency, lỗi DMA, ADC timeout |
| `Core/Src/main.c` | Không treo khi Init công suất lỗi; xử lý lỗi Start TIM3; ngắt gate khi fatal |
| `Core/Src/stm32f1xx_it.c` | DMA1_CH5 IRQ và ngắt gate trong các exception |
| `Core/Inc/stm32f1xx_it.h` | Khai báo DMA1_CH5 IRQ |
| `Core/Src/psu_link.c` | Lệnh USB GET DIAG, không đổi các lệnh trước đó |
| `Core/Src/ui_lcd.c` | Hiển thị lỗi PWM DMA |
| `Firmware_BB_V6.ioc` | TIM3 ARR 999 → 499 |

Thêm `tests/refresh_fixture.c`, `tests/run_refresh_tests.py` và báo cáo này. Log: `MDK-ARM/refresh_rebuild.log`, `MDK-ARM/refresh_tests.log`, `MDK-ARM/refresh_image_audit.json`. Build đã cập nhật các file sinh AXF/HEX/MAP và object tương ứng.

## Kiểm chứng đã chạy

- Rebuild toàn bộ target `Firmware_BB_V6` bằng Keil ARMCLANG 6.23: 0 lỗi, 0 cảnh báo.
- Chạy mã C điều khiển thực cùng driver ST TIM/DMA thực của dự án bằng GCC ARM Cortex-M3 (`-O0`) trên Unicorn, với GPIO/clock/ADC/input được mô hình hóa rõ ràng: **253 assertion, 0 lỗi**, hoàn tất đến marker cuối suite. Bản code cũ trong ZIP: **18 assertion, 0 lỗi** cho các tình huống Init hợp lệ/lệch tần số/lệch PWM mode.
- Kiểm thử cấu hình timer, lỗi HAL Start từng kênh, DMA đang bận, lỗi DMA qua IRQ HAL thực, mất phục vụ DMA/TIM3, raw OVP/OCP, ADC lỗi/stale, xung refresh/giới hạn duty, precharge, OFF với frame còn chờ, hysteresis, PI giữ lệnh qua chuyển chế độ, CC, ramp, đổi gain và trì hoãn lưu Flash khi ON.
- HEX: 56.384 byte dữ liệu, địa chỉ cuối **0x0800DC3F**. Trang cài đặt bắt đầu 0x0800F800, còn nguyên ngoài ảnh firmware. RAM theo linker: **15.232 / 20.480 byte**. SHA-256 và danh sách thay đổi nằm trong file audit.
- Chưa kiểm chứng gate thật, VBS, dòng cuộn cảm, đáp ứng tải hoặc CDC trên board. Mô phỏng thanh ghi không thay thế được các phép đo này.

## Bản sao và bước kiểm tra trên board

Bản sao đã kiểm tra CRC ZIP, gồm 134 file nguồn/cấu hình cùng manifest SHA-256:

`D:\My_project\buck_boost_converter\Firmware_BB_v7_backup_refresh_20261027_192419\source_before_changes.zip`

1. Trước khi cấp công suất, kiểm tra PA10 thấp ở OFF/FAULT, PC14 đúng mức tắt, USB PING/GET CFG/GET DIAG và LCD/nút còn đáp ứng. INIT phải bằng 0, PWM_HZ=20000 và CONTROL_HZ=2000.
2. Kiểm tra PA8/PA9 ở phía logic: precharge, nhánh pass 500 Hz/10 µs và nhánh PWM 20 kHz; xác nhận LOW/LOW trùng nhau trong refresh. Các cạnh IN được timer tạo; không dùng phần mềm chờ nanosecond.
3. Cấp nguồn công suất có giới hạn dòng, tải nhẹ; dùng phép đo vi sai phù hợp để kiểm tra HO–VS, LO–COM và VB–VS của từng driver. Không dùng kẹp mass oscilloscope nối đất trực tiếp lên switch-node/high-side.
4. Xác nhận VBS khởi động vượt ngưỡng UVLO bật tối đa 9,8 V và VBS duy trì có dư so với UVLO tắt tối đa 9,0 V, cùng gate drive đủ cho MOSFET. Nếu 10 µs/500 Hz không nạp hồi đủ với đường nạp thực, cần chỉnh timing hoặc phần cứng rồi kiểm tra lại; firmware không đo được VBS.
5. Kiểm tra OFF/trip trên oscilloscope, sau đó mới tăng dần tải và chỉnh PI với độ trễ DMA đã nêu. Chưa có cơ sở xác nhận chạy toàn tải 5 A an toàn chỉ từ build và mô phỏng.
