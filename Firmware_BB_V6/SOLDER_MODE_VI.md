# Tay hàn — SD PA10, heater PB13

**Bản hiện tại BBV6.21-TEST18 là bản test 18 V/6 A, đọc ADC thô, tắt SENSOR/HOT/NO_RISE của mũi.** Bảo vệ nguồn/mất mẫu/TIMING/watchdog vẫn bật. Xem [SOLDER_TEST18_VI.md](SOLDER_TEST18_VI.md). Phần dưới mô tả chế độ bình thường trước đó và cách đấu chân.

**Bản 6.19 hiển thị lại nhiệt độ mũi và bật đầy đủ SENSOR/HOT/NO_RISE**, với `PSU_SOLDER_TEMP_PROTECTION=1` mặc định. Giữ V/A khoảng 1 s để vào SOLDER nhanh; nguồn đang ON được dừng trước khi chuyển. Xem [SOLDER_NORMAL_VI.md](SOLDER_NORMAL_VI.md). Tài liệu [ADC debug 6.18](SOLDER_ADC_DEBUG_VI.md) chỉ mô tả bản debug trước đó.

**PA10 trở lại điều khiển SD chung của hai IR2184S như dây cũ; PB13 (trước đây T_IRQ) là tín hiệu bật/tắt heater.** Nếu đã nối SD cố định lên 3,3 V theo bản 6.16, phải bỏ dây nối cố định đó trước khi nối SD về PA10. Giữ trở kéo xuống SD 10 kΩ. Tách PB13 khỏi chân T_IRQ/PENIRQ của XPT2046 trước khi nối vào mạch heater.

## Đấu nối

| Chức năng | Chân / quy ước |
|---|---|
| SD driver | PA10 GPIO push-pull; 0 = khóa hai IR2184S, 1 = cho phép |
| Heater | PB13 GPIO push-pull; HIGH cấp nhiệt theo sơ đồ Q10/push-pull/PMOS Q9, LOW tắt |
| Nhiệt mũi | PA3 / ADC2 injected IN3 |
| Nhiệt FET | PA0 / ADC2 regular, giữ NTC hiện tại |
| Gác tay hàn | PB11 input pull-up, mức 0 = đang gác |
| Chuyển đầu ra | PC15 = 1 khi ở SOLDER, sau khoảng chờ chuyển; PC15 = 0 khi về LAB hoặc mất điều kiện an toàn |

Giữ/bổ sung điện trở kéo xuống heater để Q10 tắt khi MCU reset/chưa cấu hình PB13. GPIO khởi tạo PB13 LOW; PB13 không còn là input/EXTI của touch. PB11 được cấu hình input pull-up ngay lúc khởi tạo, không phát mức output LOW khi boot.

LAB OFF khỏe mạnh vẫn dùng SD=1 và hai IN=0 như bản nguồn trước đây, tránh rò bootstrap. Lỗi/khởi động/chưa đủ điều kiện: SD=0, MOE=0, PB13=0. **Khả năng shutdown cả bốn gate đã được khôi phục.** `pwm.sd` đọc GPIO PA10. Kiểm tra oscilloscope VGS/IN và heater thực tế; bản này chưa được nạp hoặc chạy tay hàn thật trong lượt phát triển này.

Bản 6.16 chuyển PA10 thành heater nên ở LAB giữ PA10 LOW. Nếu dây SD vẫn nối PA10 thì driver bị khóa, là nguyên nhân nguồn không chạy với cách đấu cũ. Bản 6.17 sửa ánh xạ này, không đổi PID CV/CC hoặc giới hạn nguồn đã lưu.

## Thao tác

Giữ encoder → SOLDER → chọn TIP, TARGET, STAND → START SOLDER.

- C245: nguồn DC-DC **24 V / giới hạn 6 A**.
- C210: nguồn DC-DC **12 V / giới hạn 4 A**.
- TARGET 150–450°C, mặc định 300°C. Xoay encoder tại màn hình tay hàn: 5°C/nấc.
- BTN1 dừng/chạy lại; BTN3/4/5 chọn nhanh 250/300/350°C. Giữ encoder mở menu. Muốn đổi loại mũi hoặc về LAB: dừng BTN1 trước.
- STAND mặc định 200°C, chỉnh 150–250°C. PB11 thấp ba frame liên tiếp (~150 ms) chuyển sang nhiệt gác; nhấc lên phục hồi nhiệt đặt ở frame tiếp theo (≤50 ms).
- Gác liên tục 10 phút: tắt heater và nguồn DC-DC; nhấc tay không tự bật lại, nhấn BTN1 để chạy.
- Khi tắt heater, PC15 giữ đường SOLDER; khi về LAB khôi phục V/A LAB trước đó, output vẫn OFF.
- Profile mũi/nhiệt đặt/nhiệt gác đang lưu RAM; khởi động lại về LAB OFF, C245/300°C/200°C. Không thay cấu trúc Flash V3, preset và PID nguồn đã lưu được giữ nguyên.

## Điều khiển và lấy mẫu

Chuyển mode tắt heater trước, chờ 300 ms rồi mới cho DC-DC chạy. Heater chờ nguồn đạt 95%–(Vset+0,5 V), reference hoàn tất ramp và ổn định 100 ms. Không đạt sau 30 s: dừng. Profile không được vượt các giới hạn V/A đã cấu hình; các giới hạn công suất/dòng vào/nhiệt FET của nguồn tiếp tục có hiệu lực.

Khi chuyển từ rail 24 V còn điện sang C210 12 V, firmware chờ tụ xuống ≤12,5 V, heater vẫn OFF; không dùng mũi để xả tụ. Nếu tụ chưa xuống sau 30 s sẽ báo nguồn chưa sẵn sàng. Chỉ đổi tay/mũi khi đã OFF và điện áp phù hợp.

Giữ cách V5: frame 50 ms; HEAT 45 ms cấp nhiệt / 5 ms tắt; HOLD 5 ms cấp / 45 ms tắt; quá nhiệt đặt thì tắt toàn frame. PB13 là GPIO, không có PWM phần cứng độc lập ở chân này. Duty PID được chuyển thành chuỗi bật/tắt theo mật độ xung ở nhịp TIM3 1 kHz, độ phân giải thời gian 1 ms. Đây không phải carrier 10 kHz. PWM nguồn CH1/CH2 vẫn 10 kHz, CH3 không còn xuất ra chân nào. Hai ms đầu burst tăng duty từ tối đa 20% lên duty yêu cầu. PID ban đầu Kp=0,010; Ki=0,020; Kd=0,0015; dt=0,050 s, duty tối đa 95%, anti-windup và lọc D theo V5.

Chỉ kích ADC2 injected PA3 sau khi heater đã LOW ít nhất 2 ms, không chờ ADC trong ISR. Lấy ít nhất hai mẫu cho một mean/frame; nhiệt có IIR alpha=0,25. ADC2 regular PA0 và ADC1 DMA ba kênh liên tục không đổi; TIM3 vẫn 1 kHz. Không dùng ADC1 DMA để xen kênh nhiệt mũi.

Thiếu mean trong một frame: ngừng burst kế tiếp ngay; chỉ cho nhiệt trở lại khi có mean mới. Thiếu mean kéo dài sẽ chốt lỗi timeout như bảng dưới.

Hiệu chuẩn tạm BBV6.20: ADC 1200 ≈30°C theo người dùng, giữ độ dốc từ V5:

```
C245: T = 30 + (ADC - 1200) * 1.1
C210: T = 30 + (ADC - 1200) * 1.5
```

Kiểm tra lỗi trên số chưa clamp; nhiệt hợp lệ dưới 25°C hiển thị tạm 25°C giống V5. Đây là hệ số ban đầu, cần đối chiếu nhiệt kế/đầu đo mũi để hiệu chuẩn. Không có bảo vệ nhiệt phần cứng độc lập trong firmware.

## Bảo vệ và log

Heater bị khóa khi chưa có mean nhiệt hợp lệ, thiếu ADC nguồn/nhiệt FET, lỗi nguồn, ngoài mode SOLDER hoặc output chưa sẵn sàng. Không thực hiện xả tụ bằng cầu MOSFET trong SOLDER.

Heater GPIO HIGH được gia hạn ở mỗi tick điều khiển. Nếu TIM3 ngừng cập nhật, SysTick thấy hạn 2 ms đã hết sẽ kéo PB13 LOW, SD LOW, dừng PWM và chốt lỗi TIMING. IWDG vẫn bảo vệ khi CPU/ngắt dừng hoàn toàn. Đây là giám sát phần mềm, không thay thế bảo vệ nhiệt độc lập.

Các lỗi tay hàn chốt OFF, không tự phục hồi cấp nhiệt; sau khi sửa nguyên nhân nhấn BTN1 để thử lại. Lỗi phần nguồn vẫn cần giữ BTN1 để xóa theo điều kiện nguồn hiện tại.

| `solder.reason` | Điều kiện |
|---|---|
| 1 | ADC sát rail hoặc nhiệt suy ra dưới -30°C |
| 2 | Chưa có mean sau 250 ms / mất mean hợp lệ ≥200 ms |
| 3 | Mẫu nhiệt chưa lọc >500°C, hoặc nhiệt lọc >TARGET+60°C trong 1 s khi RUN |
| 4 | Đòi công suất lớn, còn thiếu >50°C nhưng nhiệt không tăng 5°C trong 15 s |
| 5 | Precharge quá 30 s hoặc rail dưới 70% điện áp profile trong 2 s khi RUN |
| 6 | VIN/ADC/FET/nguồn không đủ điều kiện |
| 7 | Điện áp/dòng profile vượt giới hạn cấu hình |

`solder.phase`: 0 OFF, 1 chờ chuyển, 2 precharge, 3 RUN, 4 ERROR.

USB telemetry có object `solder` gồm `tip` (0 C245 / 1 C210), `phase`, `reason`, `temp_dC`, `target_C`, `active_C`, `sleep_C`, `stand`, `valid`, `adc`, `duty_pm`, `heater`, `ccr3`. `heater` là trạng thái bật/tắt PB13; `ccr3` giữ để tương thích log cũ, không điều khiển heater nữa. `duty_pm` là yêu cầu PID trong burst ở thời điểm snapshot; khoảng tắt đo nhiệt có duty 0, không phải duty trung bình cả frame.

App v6.16 tiếp tục dùng được: hiển thị nhiệt/target/duty/trạng thái gác, ghi lỗi tay hàn vào nhật ký cùng ADC mẫu. SET/ON/CFG từ app tiếp tục khóa trong SOLDER; OFF/`!` vẫn dừng được ngay. Chỉnh nhiệt và loại mũi trên thiết bị.

## Kiểm tra thực tế còn lại

Chưa có dữ liệu chạy tay hàn thật để xác nhận hiệu chuẩn, cực tính công suất, thời gian settle của khuếch đại, đáp ứng PID hay khả năng dòng của nguồn. Trước khi nối mũi: kiểm tra PB13 LOW khi reset/OFF/lỗi, chỉ cấp nhiệt sau precharge, và PA3 không bão hòa trong cửa sổ heater OFF. Kiểm tra SD PA10 lên HIGH ở LAB OFF khỏe mạnh / RUN, xuống LOW khi lỗi. Sau đó đối chiếu nhiệt độ thật ở mức thấp trước khi tăng target. Mã Q9, dòng gate/zener và phần khuếch đại phải phù hợp điện áp profile thực tế; sơ đồ chưa cho đủ thông số để xác nhận định mức các linh kiện này.

Chỉ nạp các trang chương trình theo HEX, tránh full-chip erase nếu muốn giữ hai trang Flash chứa PID/preset.
