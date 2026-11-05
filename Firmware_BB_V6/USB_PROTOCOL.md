# BBV6.37-PA6-ACTIVELOW

PA6 TIM3_CH1 10 kHz active LOW: 0 V bật/3,3 V tắt. SPI1 master TX-only theo CubeMX; TIM4 CC/CV 1 kHz. [Chi tiết](PA6_ACTIVE_LOW_VI.md). Các mục dưới là lịch sử.

# BBV6.34-SOLDER-PID

Khung 50 ms: 10 ms ??c nhi?t, 40 ms PA6 PWM 10 kHz. Duty do PID nhi?t ri?ng c?p nh?t m?i khung; TIM4 v?n ch?y n?n 1 kHz. [Chi ti?t](SOLDER_PID_50MS_10MS_VI.md). C?c m?c d??i l? l?ch s?.

# BBV6.33-PA6-PWM10K

SPI1 TX-only; PA6 TIM3_CH1 PWM 10 kHz; PID TIM4 1 kHz. Giữ khung 50 ms/OFF 15 ms để đọc nhiệt. Xem [hướng dẫn mới](SOLDER_PA6_PWM10K_VI.md). Các mục dưới là lịch sử.

# BBV6.29 — chu kỳ 50 ms / OFF 15 ms

Telemetry heater đổi sang `cycle_ms=50`, `quiet_ms=15`, `on_ms=0..35`, `cycle_pos_ms=0..49`; đây là mili giây. `sample_age_ms` là tuổi trung bình PA3, cập nhật trong khoảng LOW. Giữ 39 trường settings, CFG/SAVE và journal V4 như v6.28. App v6.28 dùng tiếp được. Xem [hướng dẫn](SOLDER_50MS_VI.md). Các phần dưới là lịch sử.

## Bản 6.28 trước đó — settings tay hàn

Thêm hai trường cuối settings: `solder_voltage` (mV, 1500..59900, mặc định 18000) và `solder_current` (mA, 50..15000, mặc định 6000). Tổng 39 trường, protocol vẫn 1. Dùng `@id CFG rev solder_voltage=7500 solder_current=1200` khi LAB/OFF hoặc SOLDER/OFF, rồi SAVE để giữ sau reset (journal V4). Khi SOLDER đang chờ/ON trả `output_off_required`; CHARGE hoặc menu đang mở trả `local_mode_required`. SET và ON từ USB vẫn bị khóa trong SOLDER. Hai trường có trong settings của reply GET/CFG/SAVE. Xem [hướng dẫn](SOLDER_APP_CONFIG_VI.md).

## Bản 6.27 trước đó — solder telemetry

Heater chuyển sang GPIO ON/OFF chu kỳ 50 giây: `cycle_ms=50000`, `quiet_ms=15000`, `on_ms=0..35000`, `cycle_pos_ms=0..49999`, `sample_age_ms` là tuổi mẫu nhiệt. `pwm_hz=0`, `fixed_pwm=0`, `ramp_ms=0`, `ramp_pm=0`, `protect=1`; `test18=1` chỉ giữ rail 18 V/6 A. `duty_pm` là lệnh GPIO tức thời 0/1000; tỷ lệ ON trong 35 giây bằng `on_ms/35000`. Không thay protocol/settings schema. Xem [chi tiết](SOLDER_ONOFF50S_VI.md). Các mục dưới là lịch sử.

## Bản 6.25 trước đó — TEST18-RAMP

Thêm `solder.ramp_ms` (0→2000 ms), `solder.ramp_pm` (giới hạn duty 0→500). PWM đầu ra tăng dần đến target test 50% sau khi rail ổn định, vẫn 10 Hz. Giữ các phase/lệnh cũ và `fixed_pwm=1` cho target test độc lập ADC. Xem [SOLDER_STARTUP_VI.md](SOLDER_STARTUP_VI.md).

## Bản 6.24 trước đó

TEST18 hiện duty 50% độc lập ADC/nhiệt quy đổi. Thêm `solder.fixed_pwm`, `pin_mode` (2 = GPIO push-pull 2 MHz), `pin_odr`, `pin_idr`, `rises`. `rises` đếm cạnh lệnh, tăng khoảng 10/s khi RUN. Xem [PB13_FIXED_TEST_VI.md](PB13_FIXED_TEST_VI.md). Các lệnh và app v6.19 vẫn tương thích.

## Bản 6.23 trước đó

Thêm `solder.pwm_hz=10`: PB13 PWM chu kỳ 100 ms. `duty_pm` là yêu cầu duty khung, `heater` là mức GPIO hiện tại. Xem [SOLDER_10HZ_VI.md](SOLDER_10HZ_VI.md). Các trường/lệnh cũ và app v6.19 giữ tương thích.

## Thay đổi bản 6.22 trước đó

Thêm `ui_draw_ms` (thời gian lớn nhất một lượt vẽ LCD, gồm phục vụ nút/USB xen giữa); không dùng trường này thay tuổi foreground. `timing_src=2` / `timing_value` vẫn là quá hạn phục vụ >250 ms. Xem [TIMING_UI_FIX_VI.md](TIMING_UI_FIX_VI.md).

Mặc định bản 6.21–6.25: `solder.protect=0`, `solder.test18=1`, rail SOLDER 18 V/6 A cho cả hai lựa chọn mũi. `solder.adc` tiếp tục cập nhật trong LAB, OFF và fault, khi heater đã tắt ít nhất 2 ms. App v6.19 vẫn tương thích. Xem [SOLDER_TEST18_VI.md](SOLDER_TEST18_VI.md).

## Thông tin bản bình thường BBV6.20 trước đó

BBV6.20 đổi mốc nhiệt tạm sang ADC 1200 ≈30°C, giữ độ dốc V5. Thêm `solder.trip_adc` (mẫu đầu tiên gây SENSOR/HOT), `timing_src` và `timing_value` (nguồn và giá trị đầu tiên của lỗi TIMING). Xem [SOLDER_NORMAL_VI.md](SOLDER_NORMAL_VI.md) để giải mã. App v6.19 vẫn tương thích.

Mặc định `solder.protect=1`: SENSOR/HOT/NO_RISE hoạt động, LCD/app hiển thị nhiệt `solder.temp_dC` theo hệ số tạm V5. ADC thô giữ tại `solder.adc`. App v6.19 nhận biết firmware debug cũ `protect=0` để hiện ADC và cảnh báo. Giữ V/A là thao tác local, không thêm lệnh bật SOLDER từ USB. Chi tiết: [SOLDER_NORMAL_VI.md](SOLDER_NORMAL_VI.md).

Không đổi protocol 1 hoặc schema settings V3. Xem [SOLDER_MODE_VI.md](SOLDER_MODE_VI.md) về các trường, mã lỗi và thao tác. `pwm.sd` đọc GPIO PA10 điều khiển SD chung. `solder.heater` là trạng thái ngõ ra heater PB13 (0/1); `solder.duty_pm` là yêu cầu PID trước chuyển thành chuỗi bật/tắt nhịp 1 ms. CH3 không còn điều khiển heater; giữ trường `ccr3` để tương thích. App v6.16 nhận nhiệt/target/duty và lưu lỗi tay hàn; SET/ON/CFG vẫn khóa khi SOLDER, OFF/`!` vẫn có hiệu lực.

# BBV6.15 USB CDC protocol 1

BBV6.15 thêm chế độ LCD NUMBER/GRAPH chọn bằng encoder. Protocol, settings V3,
caps và các lệnh giữ nguyên BBV6.14; app Windows v6.14 tương thích.

BBV6.14 giữ settings V3/37 trường và protocol 1. Các trường `vin_max`, `vout_max`,
`i_max`, `i_peak`, `power_max`, `input_current_max` tiếp tục dùng CFG/SAVE khi LAB/OFF.
Trần `caps`: `vin_mV=60000`, `vout_mV=59900`, `i_mA=15000`, `ipeak_mA=20000`,
`power_mW=600000`, `input_mA=10000`, `ovp_mV=60000`. Trần capability được lấy từ macro,
không phải giá trị profile hiện tại. Xem [POWER_LIMITS_VI.md](POWER_LIMITS_VI.md).

BBV6.13 giữ tương thích protocol 1 và settings journal V3 (37 trường). Reply
thêm `mode`, `time_s`, `change`; telemetry thêm các trường trên và object `charge`.

| Trường | Ý nghĩa |
|---|---|
| mode | 0 LAB, 1 CHARGE, 2 SOLDER |
| time_s | Thời gian output chạy trong lượt hiện tại; dừng khi OFF |
| change | Mức PC15 thực đọc từ ODR |
| charge.chem / cells | 0 Li-ion/LiPo, 1 LiFePO4 / số cell nối tiếp 1–6 |
| charge.phase | 0 IDLE, 1 CHECK, 2 PRE, 3 CC/CV, 4 FULL, 5 STOP, 6 ERROR |
| charge.reason | 0 OK; 1 LOW; 2 HIGH; 3 HEALTH; 4 TIMEOUT; 5 PRE_TIMEOUT; 6 NO_CURRENT; 7 CONFIG; 8 SWITCH |
| charge.set_mA / end_mA | Dòng sạc đã chọn / ngưỡng kết thúc |
| charge.limit_h / tail_ms | Timeout giờ / thời gian đang đáp ứng điều kiện kết thúc |
| charge.mAh10 | Điện lượng ước lượng, đơn vị 0,1 mAh |

`set_mV`/`set_mA` ngoài cùng vẫn là tham chiếu của vòng điều khiển: ở PRE có thể
khác profile đã chọn. Mode/chem/cells/timeout chọn tại encoder, hiện không có lệnh
USB đổi mode. `ON` bị chặn khi menu mở hoặc SOLDER (`local_mode_required`). Trong
CHARGE, `SET` phải giữ đúng điện áp cuối pack, chỉ đổi dòng ≥100 mA; `CFG`,
`DEFAULTS`, `PRESET` cần LAB. `OFF`/`!` dừng mọi chế độ; không xả chủ động pin.
Chi tiết ngưỡng và thao tác: [MENU_CHARGER_VI.md](MENU_CHARGER_VI.md).

BBV6.11 gỡ touch: telemetry không còn trường `touch`, không còn CAL hay lỗi
`touch_calibration`. Các lệnh, schema cấu hình, PID/ramp và journal Flash V3
giữ nguyên. App v6.9 xử lý được telemetry không có touch. Mô tả touch ở phần
lịch sử bên dưới chỉ áp dụng BBV6.9–BBV6.10.

115200 / 8N1 là USB line coding. Mỗi lệnh ASCII kết thúc LF; tối đa 1024 byte
gồm LF. ID là số nguyên 1..2147483647. Gửi một lệnh, chờ reply có cùng ID rồi
mới gửi lệnh tiếp. Không tự retry lệnh thay đổi trạng thái khi mất ACK.

```text
@1 GET
@2 SET 5000 1000
@3 ON
@4 OFF
@5 CLEAR
@6 CFG 0 cv_kp=20000 cc_ki=150000
@7 DEFAULTS 1
@8 PRESET 0 5000 1000
@9 SAVE
@10 CFG 2 ramp_v_per_s=2500
```

- GET trả toàn bộ cấu hình, revision, preset, trạng thái và hard caps.
- SET nhận mV, mA. ON/CLEAR giữ interlock của MCU; OFF bình thường kích xả.
- CFG nhận revision hiện tại và một hoặc nhiều `key=integer`; nguyên tử cả
  giao dịch, không cho trùng key. Scale từng trường có trong schema JSON.
  Ví dụ `cv_kp=20000` có nghĩa 0,020000, `vdda=3300` có nghĩa 3,300 V.
- DEFAULTS cũng yêu cầu revision. CFG/DEFAULTS/PRESET/SAVE chỉ khi OFF và
  không đang xả/chờ suy giảm dòng cuộn cảm.
- PRESET dùng slot 0..2, mV, mA. SAVE ghi cấu hình/preset đã áp dụng vào Flash;
  ACK chỉ trả sau khi ghi và kiểm tra xong. Các thao tác khác chỉ đổi RAM.
- `!` xử lý ưu tiên từ USB ISR: tắt, hủy ON chưa thực hiện, xóa phần lệnh đang
  nhận dở và hủy xả. `?` yêu cầu log sớm. Nên gửi `!\n` khi cần OFF ưu tiên.

Reply JSON có `type:"reply", protocol:1, fw:"BBV6.5", id, op, ok, error, rev,
enabled, fault, start_block, set_mV, set_mA, softstart:true, settings, presets,
caps`. `ok:false` không thay thế bằng ACK thành công giả; xem `error` và mask
interlock. Lệnh CFG sai revision phải GET lại; không ghi đè bằng bản cũ.

Telemetry 10 Hz có `type:"telemetry"`, `enabled`, `rev`, `ready_ms`, số đo,
`ilim_mA`, `ratio_pm`, `cv_pm`, `cc_pm`, `ref_mV`, `drain`, `trip`. Mode:
0 OFF, 1 RAMP, 2 CV, 3 CC, 4 FAULT. CC có ưu tiên khi đang ramp.
`ref_mV` là tham chiếu đã ramp; `set_mV` là mục tiêu người dùng đặt.
`ramp_active` cho biết tham chiếu chưa lên đến mục tiêu và output đang ON;
`ramp_mV_s:10000` báo tốc độ 10 V/s. `softstart:true` báo tính năng được bật,
không phải cờ đang ramp. Khi ON, tham chiếu bắt đầu từ min(VOUT raw,setpoint).
Tăng setpoint đi qua ramp, hạ setpoint có hiệu lực ngay; không xả tự động khi RUN.

BBV6.8 thêm trường `settings.ramp_v_per_s` (scale 1000, khoảng 100–100000
trên wire, tương ứng 0,1–100 V/s). Chỉ CFG khi OFF/dừng xả, SAVE để giữ qua
reset. Telemetry `ramp_mV_s` phản ánh tốc độ đã áp dụng. Đây là trường thứ 37,
được append sau 36 trường cũ; journal V1/V2 đọc được và SAVE mới ghi V3.

BBV6.9 thêm `touch:{rx,ry,x,y,down,cal,ready,errors}` trong telemetry.
`rx/ry` là raw 12 bit; `x/y` là pixel landscape; `cal` 1..4 là điểm đang CAL,
0 là bình thường; `ready` báo đã hiệu chuẩn trong phiên. `errors` đếm timeout
SPI. CAL chỉ khi OFF và chặn ON với error `touch_calibration`. Không thêm
trường vào cấu hình Flash: hiệu chuẩn touch hiện là RAM-only.

BBV6.10 xác nhận controller XPT2046 và thêm chẩn đoán trong `touch`:
`irq` là mức chân PB13 (0 đang kéo thấp, 1 đang cao); `z1/z2` là ADC lực nhấn;
`z=max(0,z1+4095-z2)` khi z2>=z1; `reads` đếm lượt đọc SPI, `valid` đếm mẫu
chạm ổn định, `rejects` đếm lượt đọc bị loại. `status`: 0 khởi tạo, 1 chạm hợp lệ,
2 không đủ lực nhấn, 3 rail/nhiễu tọa độ, 4 lỗi giao tiếp SPI. `errors` đếm lỗi
HAL SPI, không phải mọi nguyên nhân mất touch; bus không nối có thể trả toàn
0/4095 với HAL_OK và sẽ được nhận diện qua `rejects/status`.
IRQ được dùng để đọc nhanh; firmware vẫn kiểm tra lực nhấn mỗi khoảng 30 ms
khi IRQ cao. Trong CAL hoặc đang chạm, đọc khoảng 10 ms theo foreground.
`ready` chỉ lên 1 sau khi hoàn tất bốn điểm CAL, không phải ID phần cứng.

BBV6.5 thêm `precharge_hold`: chốt khi ON với VOUT > setpoint +50 mV,
hoặc khi hạ setpoint xuống dưới VOUT + biên 50 mV; nhả tại setpoint +20 mV.
Vòng CV bình thường không còn ngắt PWM/xóa tích phân chỉ vì vượt tham chiếu
50 mV. Raw OVP/OCP và các bảo vệ khác vẫn hoạt động, kể cả lúc chờ precharge.

BBV6.3 thêm `vout_div_pm:23000` (hệ số VOUT 23,0), `cv_sat` (lệnh tỷ số
chạm trần), `adc:{vo,io,vi}` (mean ADC chưa đổi đơn vị) và `pwm`.
`pwm` chụp đồng thời ARR, PSC, CCR1/CCR2, CR1, CCMR1, CCER, BDTR,
GPIOA CRH, AFIO MAPR, SD (latch ODR PA10) cùng trạng thái vòng điều khiển.
CCR là giá trị preload, không phải phép đo duty trên chân MCU hay VGS MOSFET.
Ở 10 kHz: ARR=7199, PSC=0; PWM1 hai kênh có CCMR1 & 0x7070 = 0x6060,
CCER & 0x33 = 0x11, BDTR & 0x8000 khác 0. Chu kỳ là 7200 tick.
Chụp tức thời có thể rơi đúng pha xả hoặc standby; diễn giải cùng `drain`,
`enabled` và `fault`. Khi OFF, CCMR1 forced inactive là trạng thái có chủ đích.

ADC/DMA và vòng 1 kHz hoạt động độc lập với USB. Parser ở foreground; lệnh
điều khiển được thực hiện trong TIM3. Áp dụng cấu hình được khóa ngắt ngắn để
DMA không thấy bộ hệ số ADC đang thay dở. Trong lúc ghi Flash, driver ngắt,
ADC/TIM3 tạm dừng, sau đó cần dữ liệu mới và ổn định lại trước khi ON.

Giữ nguyên trần 40 V VIN, 36 V VOUT, 10 A đỉnh, 8 A liên tục. Không dùng USB
để bỏ kiểm tra cấu hình phần cứng, ADC stale, OCP, nhiệt hay các lỗi đã chốt.
