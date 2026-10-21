# BBV6.40 - software SPI, run power, run solder

Current confirmed checkpoint: [CHECKPOINT.md](CHECKPOINT.md). PA6 active HIGH; LCD software SPI on PA5/PA7. See [PA6_MAIN_SOFTLCD.md](PA6_MAIN_SOFTLCD.md). The entries below are historical.

# BBV6.37-PA6-ACTIVELOW

PA6 TIM3_CH1 10 kHz active LOW: 0 V bật/3,3 V tắt. SPI1 master TX-only theo CubeMX; TIM4 CC/CV 1 kHz. [Chi tiết](PA6_ACTIVE_LOW_VI.md). Các mục dưới là lịch sử.

# BBV6.34-SOLDER-PID

Khung 50 ms: 10 ms ??c nhi?t, 40 ms PA6 PWM 10 kHz. Duty do PID nhi?t ri?ng c?p nh?t m?i khung; TIM4 v?n ch?y n?n 1 kHz. [Chi ti?t](SOLDER_PID_50MS_10MS_VI.md). C?c m?c d??i l? l?ch s?.

# BBV6.33-PA6-PWM10K

SPI1 TX-only; PA6 TIM3_CH1 PWM 10 kHz; PID TIM4 1 kHz. Giữ khung 50 ms/OFF 15 ms để đọc nhiệt. Xem [hướng dẫn mới](SOLDER_PA6_PWM10K_VI.md). Các mục dưới là lịch sử.

# BBV6.29-ONOFF50MS-OFF15MS — chu kỳ mili giây

Chu kỳ PB13 **50 ms**, OFF tối thiểu **15 ms**, ON tối đa **35 ms** theo nhiệt. Trung bình PA3 mỗi 5 ms khi heater đã tắt ít nhất 2 ms; yêu cầu mẫu nhiệt mới trước mỗi lần ON. LCD ghi `50MS CYCLE`. Giữ cấu hình áp/dòng tay hàn trên app v6.28, PID và Flash V4. Xem [hướng dẫn](SOLDER_50MS_VI.md).

Nạp `build/Firmware_BB_V6_BBV6_29_ONOFF50MS_OFF15MS.hex` hoặc HEX Keil cùng nguồn. Đã build/kiểm thử phần mềm, chưa nạp board trong lượt sửa này. Các mục dưới là lịch sử các bản dùng đơn vị giây.

## Bản 6.28 trước đó — cấu hình tay hàn

Mở `PC_App/dist/BB_V6_Studio_v6_28.exe` → **Áp / dòng tay hàn** hoặc **Cấu hình → Tay hàn**. Chỉnh điện áp và giới hạn dòng dùng chung C245/C210, mặc định 18 V/6 A. Chỉ áp dụng khi LAB hoặc SOLDER đã OFF, rồi **Lưu Flash** để giữ sau reset. PID, preset và cấu hình cũ được giữ qua chuyển journal V1/V2/V3 → V4. Chu kỳ 50 giây / OFF tối thiểu 15 giây giữ nguyên. Xem [hướng dẫn](SOLDER_APP_CONFIG_VI.md).

Cần nạp `build/Firmware_BB_V6_BBV6_28_SOLDER_CONFIG.hex` và dùng app v6.28 đi kèm. App mới vẫn đọc board/profile cũ, khóa riêng ô tay hàn khi firmware chưa hỗ trợ. Đã build/kiểm thử phần mềm; chưa nạp hoặc bật board trong lượt sửa này.

## Bản 6.27 trước đó — ONOFF50S-OFF15S

PB13 bật/tắt GPIO: mỗi chu kỳ OFF ít nhất 15 giây để đo nhiệt, ON 0..35 giây theo sai lệch nhiệt đặt/đo. Bỏ PWM heater 10 Hz và ramp duty; PWM nguồn vẫn 10 kHz. Giữ rail 18 V/6 A, bật lại kiểm tra nhiệt mũi, giữ các bảo vệ nguồn. Nhiệt không được cập nhật trong khoảng ON, có thể dài đến 35 giây; hệ số nhiệt vẫn tạm. Xem [hướng dẫn và giới hạn](SOLDER_ONOFF50S_VI.md).

Nạp `build/Firmware_BB_V6_BBV6_27_ONOFF50S_OFF15S.hex` (GCC) hoặc `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex` (Keil). Đã build/kiểm thử phần mềm; chưa nạp hoặc đo trên board. Các mục dưới đây là lịch sử phiên bản trước.

## Bản 6.25 trước đó — TEST18-RAMP

Vào/bật lại SOLDER: PB13 LOW 100 ms → PC15 HIGH, chờ 200 ms → bật nguồn 18 V/6 A, đợi áp ổn định 100 ms → PWM 10 Hz ramp 0→50% trong 2 giây. OFF/fault reset ramp và đưa PC15 LOW. Nạp `build/Firmware_BB_V6_BBV6_25_TEST18_RAMP.hex`. Xem [SOLDER_STARTUP_VI.md](SOLDER_STARTUP_VI.md).

## Bản 6.24 trước đó

Sửa nguyên nhân không có xung trên board: ADC PA3 bão hòa làm PID nhiệt cũ đặt duty=0. TEST18 hiện phát xung 50 ms ON / 50 ms OFF độc lập nhiệt quy đổi, vẫn đọc ADC và giữ bảo vệ nguồn. Khởi tạo lại PB13 output push-pull và thêm log thanh ghi/chỉ số cạnh. Nạp `build/Firmware_BB_V6_BBV6_24_TEST18_10HZ.hex`. Xem [PB13_FIXED_TEST_VI.md](PB13_FIXED_TEST_VI.md).

## Bản 6.23 trước đó

PB13 phát PWM chu kỳ 100 ms theo PID, tối đa 95 ms ON / tối thiểu 5 ms OFF để đọc ADC. Nguồn test SOLDER giữ 18 V/6 A; PWM buck/boost vẫn 10 kHz. Nạp `build/Firmware_BB_V6_BBV6_23_TEST18_10HZ.hex`. Xem [SOLDER_10HZ_VI.md](SOLDER_10HZ_VI.md).

## Thay đổi bản 6.22 trước đó

Xử lý nút/USB xen giữa lúc vẽ LCD, giữ ngưỡng giám sát 250 ms. Có HEX riêng tên phiên bản để tránh nạp nhầm: `build/Firmware_BB_V6_BBV6_22_TEST18.hex`. Xem [TIMING_UI_FIX_VI.md](TIMING_UI_FIX_VI.md).

Bản hiện tại là bản test: giữ V/A để vào SOLDER, rail 18 V/6 A cho cả hai lựa chọn mũi, tắt SENSOR/HOT/NO_RISE của mũi và hiện ADC thô. ADC PA3 vẫn đọc khi LAB/OFF/fault. Các bảo vệ phần nguồn, TIMING, mất mẫu và watchdog vẫn bật. App v6.19 dùng tiếp được. Xem [SOLDER_TEST18_VI.md](SOLDER_TEST18_VI.md).

## Bản bình thường BBV6.20 trước đó

LCD/app hiển thị lại nhiệt mũi °C và bật lại SENSOR/HOT/NO_RISE; bảo vệ nguồn, ADC timeout, nhiệt FET và watchdog vẫn giữ. Giữ BTN2 V/A khoảng 1 s vào SOLDER bằng loại mũi đang chọn; nguồn đang ON được dừng trước khi chuyển. Nhấn ngắn V/A giữ chức năng cũ. Hệ số nhiệt vẫn tạm V5, chưa hiệu chuẩn bằng nhiệt kế. Xem [SOLDER_NORMAL_VI.md](SOLDER_NORMAL_VI.md).

Xem [SOLDER_MODE_VI.md](SOLDER_MODE_VI.md) trước khi nạp: **PA10 trở lại SD chung như cũ; PB13/T_IRQ chuyển sang GPIO heater HIGH=ON, LOW=OFF.** Tách PB13 khỏi XPT2046. Nếu đã nối SD cố định 3,3 V thì bỏ dây đó trước khi nối về PA10. C245 24 V/6 A, C210 12 V/4 A; nhiệt PA3, gác PB11 active-low, đổi đường PC15. PWM nguồn giữ 10 kHz; heater bật/tắt theo PID ở nhịp 1 kHz. PID CV/CC và dữ liệu Flash cũ giữ nguyên.

# Firmware BB V6 - STM32F103C8T6

**BBV6.15:** giữ encoder → DISPLAY → chọn NUMBER/GRAPH. GRAPH vẽ V/A 30 giây,
hai thang đo tự động, vẫn hiện giá trị đo/đặt, trạng thái và các nút. Đổi view
khi output ON được; reset mặc định NUMBER. Xem [hướng dẫn](GRAPH_DISPLAY_VI.md).
PID, giới hạn và Flash cấu hình giữ nguyên; app v6.14 tiếp tục dùng được.

**BBV6.14:** mở trần cấu hình 15 A liên tục / 20 A đỉnh, VIN ngắt 60 V,
VOUT đặt tối đa 59,9 V, công suất 600 W và dòng vào ước lượng tối đa 10 A.
Phần cứng chủ board xác nhận MOSFET HY3810. Cả sáu giới hạn chỉnh/lưu được trên app;
PID và profile Flash cũ được giữ lại. Xem [cách áp dụng và giới hạn thực tế](POWER_LIMITS_VI.md).
Các mục lịch sử bên dưới có thể nêu các mức trần cũ.

**Cập nhật gain INA199:** gain **100**, shunt **1 mΩ**, REF = 0 V. Quy đổi dòng,
RMS và ngưỡng OCP dùng hệ số mới; trần hiện tại 15 A liên tục / 20 A đỉnh.
Với hệ số hiệu chuẩn mặc định, 1 A → 0,1 V; 8 A → 0,8 V; 10 A → 1,0 V.

**BBV6.13:** digit đang chỉnh màu đỏ; thêm TIME cạnh POWER. Giữ encoder 1 giây
để mở menu chọn ramp 30/10/2 V/s, sạc pack có BMS 1S–6S (Li-ion/LiPo hoặc
LiFePO₄), hoặc SOLDER (tạm bật PC15 sau khi shutdown DC-DC). Sạc có nạp trước,
CC/CV, ngắt khi đầy, timeout và khóa xả tụ. App v6.13 bổ sung trạng thái/thời gian
và nhật ký lỗi/hoàn tất sạc. Xem [hướng dẫn thao tác và giới hạn](MENU_CHARGER_VI.md).
Chức năng sạc mới chưa được xác nhận trên phần cứng. Các mục bên dưới là lịch sử.

**BBV6.12 — tăng độ rõ chữ/số LCD:** bốn ô V/A đo và đặt dùng số 28 pixel,
VOLTAGE/CURRENT dùng chữ 14 pixel; SETV/SETA giữ nhãn nhỏ gọn. VIN, FET TEMP
và POWER nằm ở ba cột riêng với số 14 pixel; công suất hiện đến 0,1 W.
Bỏ dòng STEP, bỏ nhãn BTN1–BTN5; hàng dưới OUT ở trái, M1–M3 ở phải dùng
chữ nhỏ 7 pixel. Digit được chọn vẫn có gạch chân; cảnh báo/lỗi ngắn hiện
riêng khi cần, chi tiết vẫn nằm trên app. Không thay điều khiển hoặc cấu hình.
Ảnh dựng từ driver LCD thực: `build/preview/normal.png`, `fault.png`,
`large_values.png`. HEX mới chưa nạp/kiểm tra trực tiếp trên board.

**BBV6.11 — bỏ touch, OUT bên trái / preset bên phải:** driver XPT2046,
CAL, xử lý chạm và telemetry touch đã được gỡ khỏi bản build. Mã cũ được
lưu trong `archive/touch_BBV6_10/` để tham khảo, không liên kết vào firmware.
PB12 giữ CS cao để touch không chiếm SPI của LCD; PB13 không còn ngắt EXTI.
LCD bỏ CAL và các nút ±, tăng cỡ số mức đặt. Hàng dưới từ trái sang phải:
**OUT (BTN1) | M1 (BTN3) | M2 (BTN4) | M3 (BTN5)**.
BTN1 nhấn để ON/OFF, giữ 1 s để xóa lỗi khi đủ điều kiện; BTN2 đổi V/A,
nhấn encoder đổi digit. Preset nhấn để gọi, giữ để lưu khi OFF như trước.
PID/ramp/giới hạn bảo vệ và cấu trúc Flash cấu hình được giữ nguyên;
app v6.9 tiếp tục dùng được, nhật ký lỗi vẫn nằm trên app.
Keil 0 lỗi/0 cảnh báo: Flash 45.936 byte, RAM gồm stack/heap 14.208 byte.
So BBV6.10 tiết kiệm 4.480 byte Flash và 712 byte RAM, còn 17.552 byte
trong vùng chương trình 62 KiB. Chưa nạp/test bố cục mới trên board.
Nạp `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex` (Keil) hoặc
`build/Firmware_BB_V6.hex` (GCC). Các mục touch bên dưới là lịch sử bản cũ.

**BBV6.10 — sửa driver touch XPT2046:** gửi lệnh khởi tạo PD1:PD0=00 ngay
sau bật nguồn; đọc lực nhấn Z1/Z2 để phát hiện touch ngay cả khi IRQ không
xuống. Giữ ADC hoạt động khi lấy mẫu, bỏ mẫu sau đổi kênh, median 5 mẫu và
kiểm tra ba mẫu giữa. SPI touch giảm còn 562,5 kHz và luôn trả lại clock LCD.
Log USB bổ sung mức IRQ, lực nhấn, số lần đọc/mẫu hợp lệ/bị loại và trạng thái
driver. App v6.9 dùng tiếp được. Chưa xác nhận touch trên board sau bản sửa;
nạp HEX mới rồi giữ encoder khi reset để CAL bốn điểm.

**Sửa build Keil BBV6.9:** cấu hình ArmClang 6.23 trước đây dùng `-O3` và
thư viện chuẩn đầy đủ, gây tràn Flash và 185 lỗi linker `L6406E`/`L6407E`.
Project đã chuyển sang `-Os` (Options for Target → C/C++ → Optimization)
và bật **Use MicroLIB** (tab Target). Rebuild bằng chính Keil đạt 0 lỗi,
0 cảnh báo; không thay thuật toán PID, ramp hoặc cấu trúc lưu cấu hình.
Giới hạn vẫn là 62 KiB chương trình, hai trang cấu hình cuối 2 KiB và RAM 20 KiB.
Chạy `python tools/build_keil.py --uv4 D:/KeilC/UV4/UV4.exe` để rebuild và
kiểm tra HEX không ghi vào vùng cấu hình. Log/báo cáo ở `build/keil/`;
HEX Keil ở `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex`.
`build/Firmware_BB_V6.hex` là bản build GCC riêng. Chưa nạp bản Keil lên board.

**BBV6.9 — LCD gọn, touch và nhật ký lỗi trên app:** hai ô số đo V/A lớn,
mức đặt/digit riêng, nút ±, M1–M3 và OUT. LCD chỉ hiện trạng thái ngắn
PROTECT/WAIT; lý do, ADC trip và lịch sử lỗi được xem trên app v6.9.
Touch tạm dùng giao thức XPT2046/ADS7846 qua SPI1, T_CS PB12, IRQ PB13
pull-up được poll, không chạy SPI trong ISR. Xem
[hướng dẫn touch bản cũ](archive/touch_BBV6_10/TOUCH_UI_TEST_VI.md), bao gồm giả định phần cứng.
Giữ encoder khi reset để vào CAL hoặc chạm CAL khi OFF, nhấn/nhả bốn dấu
cộng xanh. Hiệu chuẩn tạm lưu RAM, cần làm lại sau reset để bật OUT bằng
touch; encoder/BTN1 vẫn dùng ngay. Giữ OUT 1 s rồi nhả để bật, chạm để tắt.
Giữ RESET để xóa lỗi theo interlock cũ; CAL khóa cả ON từ USB.
PID, ramp, giới hạn bảo vệ và journal Flash V3 giữ như BBV6.8.
App `PC_App/dist/BB_V6_Studio_v6_9.exe` tự ghi lỗi dạng JSONL vào
`%LOCALAPPDATA%/BB_V6_Studio/logs`, giữ snapshot/thời gian giờ Việt Nam,
không lặp cùng trạng thái mỗi mẫu. Tab Nhật ký & chẩn đoán tách lịch sử
lỗi khỏi thông số thô; Khôi phục mặc định nằm trong menu Bảo trì.
Đã build/kiểm thử phần mềm và EXE DEMO; chưa nạp hay thử touch trên board.

**BBV6.8 — chỉnh ramp trên app và lưu Flash:** `ramp_v_per_s` từ 0,1 đến
100 V/s, mặc định 10 V/s. App `PC_App/dist/BB_V6_Studio_v6_8.exe` có nút
**Chỉnh ramp** và nhóm **Ramp điện áp** ở đầu tab Cấu hình. Khi output OFF,
nhập tốc độ → Áp dụng cấu hình → Lưu Flash. Ramp áp dụng lúc ON/tăng Vset;
giảm Vset vẫn có hiệu lực ngay, không thêm xả chủ động khi RUN.
Firmware đọc journal V1/V2 cũ, giữ nguyên 36 thông số/PID và preset,
chỉ bổ sung ramp 10 V/s trong RAM. Lần SAVE tiếp theo ghi journal V3.
V3 vẫn giữ hai trang, CRC và commit cuối; firmware cũ không đọc định dạng V3.
Profile 36 trường cũ được app mới bổ sung ramp mặc định khi nạp; khi nối
board cũ, app vẫn cho chỉnh PID và khóa riêng mục ramp cho tới khi nạp BBV6.8.
Đã khôi phục CV 0,015/0,20/0 và CC 0,01/1,0/0 qua COM14, nhận ACK SAVE
và đọc lại đúng trên board BBV6.5, output OFF. Có profile dự phòng
[PID đã chốt, ramp 10 V/s](PC_App/Profiles/PID_tuned_ramp_10Vps.json).
HEX mới chưa nạp/đo ramp trên board; đã kiểm thử mã C, journal và EXE DEMO.

**BBV6.7 — encoder đảo chiều và chọn digit:** nhấn PB4 để lần lượt chọn
bước 0,01 → 0,1 → 1 → 10 V; ở mục dòng chọn 0,01 → 0,1 → 1 A rồi quay
về đầu. BTN2 PB8 đổi chỉnh V/A, nhớ riêng digit của mỗi mục. Màn hình
gạch dưới digit đang chọn. Giữ nút cũng chỉ thực hiện một lần, không lặp.
Không dùng fine/coarse nữa; chiều nằm ở `PSU_ENCODER_DIRECTION=-1`.
Bản chính dùng lại đúng thuật toán CC BBV6.5 đã đo ổn 2 A, không đưa thử
nghiệm BBV6.6 vào bản UI này. Mặc định CC Kp=0,01 / Ki=1,0 / Kd=0;
cấu hình đã lưu Flash vẫn được ưu tiên. PWM 10 kHz và ramp 10 V/s giữ nguyên.
Đã kiểm thử nút/encoder bằng mã Cortex-M3, chưa thử trực tiếp encoder trên
board sau thay đổi; nạp `build/Firmware_BB_V6.hex` để dùng giao diện mới.

**Đã tuning CC trực tiếp trên BBV6.5 qua COM14, không cần nạp BBV6.6:**
chốt `Kp=0,010`, `Ki=1,000`, `Kd=0` và đã SAVE/ACK vào Flash của board.
Ở mức đặt 1,5 V/2 A với nhíp, lượt gần 10 s cho dòng sau 0,5 s trung bình
1,9999 A, khoảng 1,984–2,014 A, CC liên tục. Startup có mẫu lọc lên 2,403 A;
chưa xác nhận dòng đỉnh bằng oscilloscope hoặc tải khác. Đã để board OFF,
setpoint trong RAM 1,5 V/2 A. SAVE giữ gain và preset, không giữ setpoint
đang chỉnh: sau reset cần chọn lại 1,5 V/2 A hoặc nạp
[profile CC](PC_App/Profiles/CC_2A_tweezers_BBV6_5.json) trong app.
Xem [báo cáo CC cập nhật](build/tuning_cc/README_VI.md).
BBV6.6 bên dưới là lịch sử bản thử thuật toán khác, chưa được đo trên board;
HEX hiện tại là BBV6.9 với vòng CC BBV6.5, gain đã chọn và ramp cấu hình được.

**BBV6.6 — sửa feedforward vòng CC trước khi tuning dòng:** CC dùng lệnh
chuyển đổi tuyệt đối, tích phân bám lệnh PWM khi chưa giới hạn dòng; không
cộng `reference_v / vin` của ramp CV vào CC nữa. Trước sửa, feedforward vẫn
tăng trong lúc ramp dù CC đã nhận quyền điều khiển. Kiểm thử hồi quy cô lập
đã tái hiện vấn đề trên BBV6.5 và qua sau sửa. Ramp CV 10 V/s vẫn giữ.
Hai lượt thử nhíp ở mức đặt 2 A đã dừng bởi ngưỡng host 2,2 A: mẫu dòng
lọc cao nhất lần lượt 2,276 A và 2,368 A. Không lưu gain thử; cấu hình,
setpoint và preset đã khôi phục, board OFF. **BBV6.6 chưa được đo trên board,
chưa chốt PID CC.** Xem [báo cáo CC](build/tuning_cc/README_VI.md).

**BBV6.5 — tuning CV từ dữ liệu COM14:** không còn dùng điều kiện
VOUT > tham chiếu +50 mV để tắt PWM/xóa tích phân liên tục khi đang điều áp.
Chờ precharge chỉ được chốt khi ON với VOUT > setpoint +50 mV, hoặc khi
hạ Vset xuống dưới VOUT hiện tại. Cờ được nhả khi VOUT <= setpoint +20 mV.
Sau khi nhả, PI/PID xử lý các sai lệch nhỏ bình thường; OVP/OCP vẫn ngắt như cũ.
Không thêm xả chủ động khi RUN. Đã đo trên board với VIN khoảng 17,83 V,
quạt 5 V khoảng 66 mA; chốt Kp=0,015, Ki=0,20, Kd=0 và lưu Flash.
Các hệ số này trùng mặc định, cải thiện đến từ việc bỏ ngắt/xóa PI trong
điều áp bình thường. Xem [báo cáo tuning](build/tuning/README_VI.md) và
[profile](PC_App/Profiles/CV_5V_fan_verified.json). Chưa xác nhận toàn dải tải/điện áp.

**BBV6.3:** hệ số chia áp VOUT được sửa thành `23.0f` theo chủ board.
CDC thêm `cv_sat`, `vout_div_pm`, `adc` và `pwm` để đối chiếu lệnh với
thanh ghi timer. Đây là bản bổ sung chẩn đoán; chưa xác định được nguyên nhân
phần cứng khiến VOUT không đáp ứng khi lệnh điều khiển đã chạm trần.

**BBV6.4: đã thêm lại ramp điện áp 10 V/s theo yêu cầu mới nhất.**
Khi ON, tham chiếu bắt đầu từ VOUT raw, chặn không vượt setpoint, rồi tăng
0,01 V mỗi tick TIM3 1 kHz. Tăng setpoint khi RUN cũng đi qua ramp; hạ
setpoint có hiệu lực ngay, giữ cơ chế chờ áp tụ hạ và envelope OVP.
PWM TIM1 vẫn 10 kHz. CC được ưu tiên ngay cả khi đang ramp; OFF hoặc lỗi
xóa tham chiếu và ngắt lệnh PWM. Ramp chỉ làm mềm tham chiếu, không xác nhận
tầng gate đã hoạt động đúng hay bảo đảm tốc độ VOUT thực trên board.
Tốc độ nằm ở `PSU_RAMP_V_PER_S` trong `Core/Inc/psu_config.h`, chưa lưu trong Flash.
USB CDC có giao thức cấu hình
PID, bảo vệ, ADC/NTC và xả tụ; kèm app Windows `PC_App/dist/BB_V6_Studio_v6_4.exe`.
Xem [hướng dẫn app](PC_App/README_VI.md) và [giao thức USB](USB_PROTOCOL.md).
Chỉ áp dụng cấu hình nâng cao khi OFF và xả đã dừng; Lưu Flash để giữ sau reset.

**Bản chính đã tích hợp logic OFF được chủ board thử: `PSU_LOW_SIDE_TEST=0`.**
Khi OFF và cảm biến hợp lệ ổn định 100 ms, PA10 SD = 1, CCR1/CCR2 = 0,
MOE bật; ép hai kênh timer inactive để PA8/PA9 thực sự giữ thấp. Hai low-side
bật, hai high-side tắt. BTN1 bật/tắt CC/CV bình thường, LCD hiện OFF/CV/CC.
Khi RUN có lệnh duty bằng 0, hai cầu cũng về trạng thái kẹp low-side này.
Lúc reset/khởi tạo, dữ liệu ADC/nhiệt chưa hợp lệ, timer PWM dừng hoặc lỗi
bảo vệ đã chốt, SD về 0 và MOE tắt. Chỉ về standby sau khi đủ điều kiện;
lỗi đã chốt phải xóa bằng giữ BTN1. Ghi Flash tạm ngắt driver, khôi phục
standby sau khi ADC và điều kiện an toàn ổn định lại, không tự bật CC/CV.
Chuyển RUN sang OFF ép hai ngõ IN thấp ngay và xóa duty đang đệm. Chuyển
sang RUN nạp đồng thời hai duty trước khi mở PWM1; khi đang RUN, hai duty
được cập nhật cùng chu kỳ timer. Không đổi giới hạn OVP/OCP hay thuật toán PI.
Standby kẹp VS1/VS2 về PGND. Bản hiện tại có thêm xả nhẹ bằng xung đơn sau
thao tác ON -> OFF bình thường theo yêu cầu chủ board; xem mục xả bên dưới.
Khi dùng CC/CV hoặc xả, phải lắp lại L4 và đủ linh kiện công suất; ngắt điện trước khi lắp/nạp.
Có thể đặt `PSU_LOW_SIDE_TEST=1` rồi build để quay lại bản test không cho RUN.

Bản triển khai ban đầu cho bộ nguồn buck-boost bốn MOSFET. Có điều khiển CC/CV,
bảo vệ phần mềm, UI ST7789, preset Flash và USB CDC. Chưa được kiểm chứng trên
tầng công suất thật; chưa phải bản firmware đã nghiệm thu cho sản phẩm bán ra.

**Sửa chọn nhầm CC khi tải nhẹ:** vòng dòng có thể giữ một lệnh duty thấp
do tích phân/tracking từ khởi động hoặc lần giới hạn dòng trước. Bản hiện
tại nhả hạn chế đó khi dòng thấp hơn giới hạn hiệu dụng quá một biên 5%
(biên được chặn 0,01..0,10 A), đồng thời nạp lại tích phân CC theo lệnh CV.
Đã bỏ giới hạn tăng lệnh khi nhả CC. Khi gần/vượt giới
hạn dòng hiệu dụng, bộ chọn min CV/CC vẫn ưu tiên vòng dòng. Giới hạn hiệu
dụng bao gồm dòng đặt, công suất và nhiệt, không chỉ số A trên encoder.
Regression dùng VIN 17,8 V, VOUT 2,53 V, IOUT 0,03 A đã tái hiện chọn CC
ở mã trước; bản sửa chọn CV. Có kiểm tra tải buộc CC rồi nhả về CV bằng
mô hình điện áp trung bình bậc một; mô hình này không xác nhận quạt, cuộn cảm
hay độ ổn định của board thật. Chức năng OFF/xả xung được giữ nguyên.

CDC thêm `ilim_mA` (giới hạn dòng hiệu dụng), `ratio_pm`, `cv_pm`, `cc_pm`
(lệnh tỷ số nhân 1000), `ref_mV`. Các số này giúp phân biệt duty bị giới
hạn trong firmware với việc lệnh PWM đã đủ nhưng tầng công suất chưa đạt áp.

**Bản thử theo yêu cầu: dòng đỉnh 10 A, dòng đặt/liên tục tối đa 8 A,
BTN1 PB9 được phép bật/tắt output.** `PSU_BENCH_MODE=1` cho phép thử khi hai
cờ xác minh current-path/power-stage còn 0; các cờ này không được đổi thành
đã nghiệm thu. Kiểm tra thang ADC, gate, NTC và mọi bảo vệ runtime vẫn giữ.
Khởi động luôn OFF, mặc định 5 V/1 A; chỉ bật khi nhấn ngắn rồi nhả BTN1 và
các cảm biến/điều kiện khởi động đều hợp lệ.

Theo yêu cầu của chủ board, **đã bỏ điều kiện VOUT dưới 0,8 V** khi bật,
chờ sẵn sàng và xóa lỗi. VOUT khoảng 5 V có sẵn khi OFF không còn chặn BTN1.
Tham chiếu tăng 10 V/s từ VOUT đo hiện tại đến setpoint.
Nếu đầu ra đã precharge trên setpoint +0,05 V khi ON hoặc khi hạ Vset,
giữ PWM 0/kẹp low-side tới khi VOUT <= setpoint +0,02 V. Sau đó vòng CV
điều khiển liên tục; không tự kích xả khi RUN. OVP vẫn dùng ngưỡng cũ,
không nâng ngưỡng theo điện áp có sẵn: chẳng hạn setpoint 5 V, VOUT 6 V vẫn
gây lỗi OVP khi bật. OCP 10 A/hai mẫu, dòng liên tục 8 A và các bảo vệ khác
vẫn hoạt động. Thay đổi này không xác nhận nguyên nhân VOUT tự tăng khi OFF
hay khả năng khởi động thực tế của tầng công suất khi output đã có điện áp.

Trần công suất thử tạm **60 W**, dòng vào ước tính tối đa **5 A** theo hiệu
suất 0,85. Đây là ngân sách phần mềm tạm, không phải rating đã đo. Ví dụ tại
36 V dòng bị giới hạn thêm khoảng 1,67 A bởi trần 60 W; không cam kết 8 A
trên toàn dải điện áp. Đặt ngân sách về 0 vẫn khóa RUN, kể cả bench mode.

VIN và VOUT đều dùng hệ số 23 (220k/10k), VDDA 3,3 V, REF=0 V và tổng shunt thực tế
1 mOhm đã được xác nhận. Gain 200 giữ theo yêu cầu. Ảnh schematic cho thấy
shunt low-side ở đường Vout_N về PGND; cách nối/nguồn INA199 và rating tầng
công suất vẫn chưa nghiệm thu.

## Đã triển khai

- TIM1 CH1 PA8 và CH2 PA9: 72 MHz / 7200 = **10 kHz**, hai kênh cùng timer.
- PA10: `/SD` IR2184S, **0 = ngắt, 1 = cho phép**. OFF bình thường dùng SD=1,
  MOE=1, hai IN=0. Lỗi/reset dùng SD=0 và cắt TIM1 MOE. Trở kéo xuống 10k
  giữ ngắt khi reset. PWM=0/kẹp low-side khác với shutdown cả bốn MOSFET.
- ADC1: rank 1 PA1 VOUT, rank 2 PA2 IOUT, rank 3 **PA4 VIN**. Đã sửa rank 3
  bị trùng PA1 trong project CubeMX ban đầu.
- ADC1 software-start, scan liên tục ba kênh, không dùng trigger timer. ADC
  clock 12 MHz, sample time 239,5 chu kỳ: 63 us/frame, khoảng 15.873 frame/s.
  ADC vẫn chạy khi output OFF, TIM1 MOE bị cắt hoặc bộ đếm TIM1 dừng.
- DMA circular tám frame; mỗi callback half/full xử lý bốn frame. OCP kiểm tra
  từng mẫu thô liên tiếp, xuyên qua ranh giới buffer, trước averaging/IIR.
- TIM3: PSC=71, ARR=999, ngắt điều khiển **1 kHz**. Hai PI chọn lệnh thấp hơn
  để chuyển CC/CV, tracking anti-windup; ramp tham chiếu theo cấu hình, mặc định 10 V/s.
- Callback DMA tính mean bốn frame và công bố kết quả mỗi 252 us. TIM3 chỉ
  lấy bộ mean hoàn chỉnh mới nhất, đổi đơn vị rồi IIR alpha=0,25 cho VIN,
  VOUT, IOUT; không đọc buffer DMA hay khởi động ADC. Nhánh bảo vệ nhanh
  kiểm tra từng mẫu thô trước mean/IIR, không bỏ sót mẫu giữa hai tick TIM3.
- Dòng liên tục: cộng bình phương từng mẫu dòng trong DMA; IIR bình phương
  dòng với tau khoảng 1 s ngay tại callback DMA (bước 252 us). TIM3 dùng
  kết quả này để chốt lỗi nếu vượt 8² A² trong 100 ms. USB báo căn bậc hai
  của giá trị này (`irms_mA`); không lấy bình phương của dòng đã mean.
- ADC2 PA0 chạy liên tục; xử lý NTC mỗi 10 ms, IIR alpha=0,1. Dữ liệu nhiệt
  thô kiểm tra ngắt trước lọc. Phát hiện hở/chập NTC theo gần hai rail ADC.
- TIM2 encoder x4, filter 8, remap PA15/PB3, tắt JTAG và giữ SWD.
- PB4 và PB5..PB9 có pull-up, EXTI hai cạnh, debounce foreground 25 ms,
  long-press 1 s. EXTI chỉ xác nhận cạnh, không vẽ LCD hoặc xử lý điều khiển.
- ST7789 240x320 đặt ngang 320x240, RGB565, SPI1 18 Mbit/s. Vẽ theo dòng,
  không cấp framebuffer 153.600 byte trong RAM 20 KiB. Chỉ cập nhật vùng đổi.
- USB CDC đã khởi tạo, trả lời line coding, log JSON Lines 10 Hz, xử lý BUSY,
  disconnect và giữ buffer truyền cho tới khi USB hoàn tất. Chưa có app PC.
- Ba preset lưu hai trang Flash luân phiên, CRC32, commit cuối. Mất điện giữa
  lần ghi giữ bản hợp lệ trước. Chỉ cho ghi khi output OFF; không tự bật lại.
- PC13 LED theo output enable, active HIGH theo diễn giải câu trả lời của chủ
  board; PC14 còi active HIGH; PC15 luôn 0, chưa có chế độ tay hàn. PB11 giữ 0.
- Independent watchdog khoảng 0,8 s theo LSI điển hình; foreground chỉ feed
  khi TIM3 còn tăng heartbeat. Có cắt nguồn khi foreground treo >250 ms hoặc
  vòng điều khiển vượt ngân sách 500 us theo DWT. Thời gian thực cần đo trên MCU.
- HardFault/NMI/các fault CPU và lỗi HAL cắt `/SD` và MOE trước khi dừng.

## Các thông số tạm và điều kiện còn thiếu

Tất cả tập trung tại `Core/Inc/psu_config.h`.

| Thông số | Hiện tại |
|---|---|
| VDDA | 3,300 V, đã xác nhận; cần hiệu chuẩn sai số thực tế |
| Chia VIN | 220k/10k, hệ số 23, đã xác nhận |
| Chia VOUT | Hệ số 23,0 (220k/10k), cập nhật theo chủ board |
| Shunt / INA199 | 1 mOhm / gain 100 / REF=0 V đã xác nhận |
| NTC tạm đã được chủ board yêu cầu | 10k tại 25 C, B3950, pull-up 10k lên 3,3 V, NTC xuống GND |
| Nhiệt cảnh báo / ngắt / xóa lỗi | 70 / 85 / dưới 60 C |
| Điện áp đặt | 1,50..36,00 V; 0 V tương ứng thao tác OFF, không phải điều áp về 0 |
| Dòng đặt / ngưỡng đỉnh | 0,05..8,00 A / 10 A |
| Công suất / dòng vào ước tính tối đa | Tạm 60 W / 5 A, phục vụ thử nghiệm |
| Chế độ thử | PSU_BENCH_MODE=1, BTN1 bật được khi đủ điều kiện khởi động |
| Tụ đầu vào C16 / tụ đầu ra C15 | Theo ảnh schematic: mỗi tụ 1000 uF; chưa biết voltage rating, ESR, ripple current |
| Cuộn cảm L4 / MOSFET Q1..Q4 | 97 uH / ký hiệu IRFZ44 theo ảnh; Isat, linh kiện thực tế và công suất nhiệt chưa xác minh |
| Mạch đo dòng, tầng công suất đã xác minh | Chưa, các cờ xác minh tương ứng vẫn 0 |

Với VDDA 3,3 V:

1. VIN 40/23 = **1,739 V** tại PA4. Full-scale danh định 75,9 V; đủ khoảng
   đo cho VIN 40 V và ngưỡng bảo vệ. Firmware vẫn giới hạn VIN tối đa 40 V.
2. VOUT 36/23 = **1,565 V** tại PA1. Full-scale danh định 75,9 V; đủ khoảng
   đo overshoot/OVP. Firmware vẫn giới hạn setpoint VOUT tối đa 36 V.
3. Ngưỡng hiện tại: 10 A x 0,001 Ohm x 100 = **1 V**, trong thang ADC;
   8 A tương ứng 0,8 V. Full-scale ADC danh định là 33 A, còn phụ thuộc nguồn
   và swing INA199. Firmware vẫn giới hạn 8 A liên tục / 10 A đỉnh.
4. Shunt trong ảnh nằm low-side, giữa Vout_N và PGND; do đó mức VOUT 36 V
   không tự đặt common-mode 36 V lên INA199. Cần xác nhận INA199 IN+ lấy Kelvin
   ở Vout_N và IN- ở PGND. INA199 chỉ hỗ trợ common-mode tới 26 V nên không
   chuyển sang high-side 36 V với IC này.

Hai cầu chia áp giữ nguyên; gain INA199 đã cập nhật thành 100 theo phần cứng mới.
Thang đo lớn hơn không nâng giới hạn dòng của tầng công suất. Chia áp sau một
amplifier đã bão hòa không khôi phục được phép đo.

`Psu_ConfigErrors()` kiểm tra khoảng đo theo ngưỡng đỉnh cấu hình, cộng 5%
headroom. Mask mặc định trong bản bench 10 A/8 A là **0**. Chỉ bỏ yêu cầu
hai cờ xác minh current-path/power-stage theo yêu cầu cho thử; kiểm tra
range/gate/NTC/ngân sách không bị bỏ. Nếu tắt `PSU_BENCH_MODE` khi hai cờ
xác minh vẫn 0, mask là `0x60` và output bị khóa lại.

Giới hạn dòng vào hiện chỉ là **ước lượng ngân sách công suất** từ VIN,
hiệu suất bảo thủ 0,85 và rating cấu hình. Không có cảm biến dòng vào nên không
thể coi đây là bảo vệ quá dòng đầu vào. 36 V x 8 A = 288 W; tại VIN 7 V,
hiệu suất 85%, dòng vào cần khoảng 48,4 A. Bản thử hiện tại không cho phép
mức công suất đó; ngân sách là min(60 W, VIN x 5 A x 0,85).

IR2184S cần nguồn gate 10..20 V; VIN xuống 7 V đòi hỏi nguồn driver phù hợp,
không thể giả định cấp VCC driver trực tiếp từ VIN trên toàn dải.

PC13..PC15 có giới hạn tải riêng: chỉ điều khiển logic/buffer hoặc transistor
phù hợp, không trực tiếp cấp dòng cho còi/relay/LED. LCD backlight chưa có chân
điều khiển trong cấu hình; giả định được cấp nguồn riêng.

## Bảo vệ và giới hạn thực tế

| Bit lỗi | Điều kiện |
|---|---|
| 0x001 | OCP: tối thiểu **hai mẫu dòng thô liên tiếp >=10 A** |
| 0x002 | OVP: tham chiếu x1,05 +0,5 V, chặn trên 37 V |
| 0x004 | VIN >40 V |
| 0x008 | VIN <7 V liên tục 5 tick điều khiển |
| 0x010 | Nhiệt >=85 C |
| 0x020 | ADC rail hai mẫu liên tiếp hoặc cảm biến nhiệt không hợp lệ |
| 0x040 | ADC/DMA lỗi, ADC1 mất dữ liệu >=3 ms hoặc nhiệt mất dữ liệu >=200 ms |
| 0x080 | Dòng nhiệt dài hạn >8 A theo mean-square/IIR và debounce 100 ms |
| 0x100 | Mất deadline hoặc foreground trễ |
| 0x200 | Output <0,3 V và dòng >90% dòng đặt trong 500 ms, setpoint >=1,5 V |

Lỗi chốt, không tự restart. Giữ BTN1 để xóa lỗi khi đã hết nguyên nhân, VIN
7,2..39,5 V, nhiệt dưới 60 C, dòng dưới 0,2 A, dữ liệu tươi và trạng thái ổn
định ít nhất 100 ms sau khi trip. Sau đó nhấn BTN1 riêng để bật.

Giảm dòng tuyến tính từ 70 tới 85 C. VIN <7,8 V hiện cảnh báo. Khi chỉnh
giảm điện áp, OVP dùng đường bao chỉ giảm theo VOUT thực để không báo nhầm
điện áp còn trên tụ; đường bao không tăng theo một lỗi điện áp đang tăng.
Hạ setpoint khi đang ON không tự bật chế độ xả; VOUT vẫn có thể giảm chậm
khi không tải. Xả xung chỉ được kích sau thao tác OFF bình thường.

## Xả nhẹ sau OFF - bản thử theo yêu cầu

`PSU_DISCHARGE_ENABLED=1` bật helper xả hở vòng. Nhấn BTN1 tắt khi đang ON
(hoặc USB `!`) sẽ chờ cảm biến/dòng tải ổn định, kẹp hai low-side thêm 20 ms
để nạp bootstrap, rồi bắt đầu xả C15. Không tự xả khi vừa boot, sau lỗi,
xóa lỗi hay khi chỉnh giảm setpoint trong RUN.

- Q1 luôn OFF, Q3 ON; Q2 được bật từng xung đơn, Q4 điều khiển bổ sung bởi
  IR2184S. Vòng dòng: C15(+) -> Q2 -> L4 -> Q3 -> PGND -> C15(-).
- Dùng TIM1 OPM, RCR=0, CH1 forced-inactive, CH2 PWM2. Bộ đếm 72 MHz,
  ARR=7199. Mỗi lần kích chỉ có **một xung 2..20 us**, kết thúc bởi phần
  cứng timer trong 66,7 us. Không dùng vòng chờ phần mềm để cắt gate.
- Hai lần kích cách nhau ít nhất **20 ms**. Giữa các xung, OPM dừng ở CNT=0,
  PWM2 LOW; Q3/Q4 ON để dòng cuộn cảm suy giảm. Không chạy xả PWM 10 kHz.
- Độ rộng được tính từ `L * 1 A / VOUT`, làm tròn xuống và giới hạn 20 us.
  Với L danh định 97 uH: tại 36 V lệnh khoảng 2,7 us, tại 5 V khoảng 19,4 us.
  **1 A là ước lượng mức tăng dòng, không phải giới hạn dòng được đo/bảo vệ.**
  Sai số L, bão hòa, dòng còn lại và trễ driver làm dòng thực khác tính toán.
- Dừng tại VOUT raw <=0,5 V; không cố kéo âm hoặc đạt đúng 0,000 V.
  Dừng nếu VOUT tăng >0,3 V so với lúc OFF hoặc vượt 36,5 V; nếu không giảm
  ít nhất 0,05 V trong một cửa sổ 5 s; tối đa 360 s hoặc 18.000 xung.
  Không tự thử lại sau các điều kiện dừng này.
- Lỗi, mất ADC/nhiệt hoặc mất điều kiện standby hủy xả, SD=0. Lỗi đã chốt
  vẫn cần giữ BTN1 để xóa, không xả qua một tầng công suất đang có fault.
- Nhấn ON hủy xả, chờ đủ 20 ms từ xung cuối, khôi phục timer 10 kHz rồi mới
  cho CC/CV chạy. Lưu preset chờ xả hoàn tất/hủy và hết khoảng chờ này.
- LCD hiện `DRAIN`, số xung và đích 0,5 V; khi xong `DISCHARGE DONE`.
  Dừng do không giảm/tăng áp/timeout có thông báo riêng, không tự báo đã xả hết.
  CDC thêm `drain:{state,pulses,ticks}`; `ticks/72` là độ rộng lệnh us.
  State: 0 idle, 1 wait, 2 active, 3 done, 4 timeout, 5 no-drop, 6 rising, 7 aborted.

C15 âm nối thẳng PGND nên dòng xả nội bộ **không qua shunt output**. Các
giá trị IOUT trên LCD/CDC và bảo vệ 10 A không đo/giới hạn dòng xả này.
Đây là helper thử nghiệm được chủ board yêu cầu sau khi đã biết hạn chế;
chưa phải chức năng xả có bảo vệ dòng cho sản phẩm hoàn chỉnh. Có thể mất
hàng chục giây ở điện áp thấp và vài phút ở điện áp cao; không tăng mật độ
xung khi chưa đo gate/dòng cuộn cảm thực tế. Không đấu nguồn ngoài vào output
trong phép thử. Kiểm chứng trước ở 5..12 V với L4 đã lắp; sau đó mới đánh giá
dải cao. Tắt helper bằng `PSU_DISCHARGE_ENABLED=0` rồi build lại.

Kiểm tra OPM/PWM2 theo [ST AN4776](https://www.st.com/resource/en/application_note/an4776-generalpurpose-timer-cookbook-for-stm32-microcontrollers-stmicroelectronics.pdf).
Các kiểm thử mô phỏng thanh ghi kiểm tra thứ tự cấu hình, độ rộng/khoảng cách,
hủy xả và khôi phục RUN; không xác nhận biên độ dòng hay dạng sóng vật lý.

**10 A là ngưỡng phát hiện phần mềm, không phải bảo đảm đỉnh dòng vật lý.**
Chu kỳ mẫu 63 us; callback DMA mỗi 252 us. Phản ứng phần mềm có thể ở
cỡ vài trăm us, còn có trễ amplifier/lọc/ngắt. Với 97 uH và 40 V, di/dt lý
tưởng khoảng 0,412 A/us. Cần comparator/bảo vệ chu kỳ phần cứng và đo dòng
cuộn cảm nếu muốn bảo vệ MOSFET/cuộn cảm khi chập. Dòng output không đại diện
cho dòng cuộn cảm khi boost. Chưa có mạch ngắt độc lập hoặc TIM1 BKIN trong
project này. Không ghép comparator trực tiếp vào GPIO push-pull đang kéo cao.

Bảo vệ đảo cực, dòng ngược, quá áp do nguồn ngoài ở đầu ra, xả tụ an toàn
và điều kiện OFF cách ly tải còn phụ thuộc schematic. Không thể bổ sung đầy
đủ các chức năng đó chỉ bằng firmware với tập chân đo hiện tại.

## Điều khiển và UI

| Phần tử | Nhấn ngắn | Giữ 1 s |
|---|---|---|
| Encoder PB4 | Chuyển digit đang chỉnh | Chuyển digit một lần |
| BTN1 PB9 | Bật; khi đang bật, tắt ngay sau debounce | Xóa lỗi khi OFF và đủ điều kiện |
| BTN2 PB8 | Đổi chỉnh V/A | Đổi chỉnh V/A một lần |
| BTN3 PB7 | Gọi M1 khi OFF | Lưu M1 khi OFF |
| BTN4 PB6 | Gọi M2 khi OFF | Lưu M2 khi OFF |
| BTN5 PB5 | Gọi M3 khi OFF | Lưu M3 khi OFF |

Digit mặc định là 0,01 V hoặc 0,01 A mỗi nấc; nhấn encoder để chọn
0,1 / 1 / 10 V hoặc 0,1 / 1 A. Digit được gạch dưới và nhớ riêng cho V/A
trong phiên chạy, không ghi Flash. Sửa `PSU_ENCODER_COUNTS` nếu encoder
thực tế không có bốn cạnh mỗi nấc; `PSU_ENCODER_DIRECTION=-1` đảo chiều
so với bản cũ. Preset mặc định: 5V/1A, 12V/3A, 24V/2A.
Khởi động luôn OFF, setpoint 5V/1A; không phục hồi trạng thái ON từ Flash.
Preset chỉ gọi khi OFF để tránh đổi đột ngột khi đang cấp tải.
Các preset cũ CRC hợp lệ có dòng lớn hơn 8 A được giới hạn về 8 A trong RAM
khi tải; không tự ghi lại Flash và không tự bật output. Encoder và mọi đường
cập nhật setpoint đều giới hạn tối đa 8 A.

Khi yêu cầu ON bị từ chối, màn hình hiện **BLOCK** và lý do cụ thể:

| Thông báo | Cần xử lý |
|---|---|
| ADC NO DATA | Kiểm tra ADC1 continuous/software-start và DMA circular, chưa có dữ liệu tươi |
| CHECK TEMP SENSOR | Kiểm tra PA0, NTC tạm 10k/B3950, kéo lên 10k, hở/chập hoặc dữ liệu nhiệt mất |
| VIN BELOW 7.2V / VIN ABOVE 39.5V | Đưa VIN về khoảng cho phép khởi động |
| FET MUST BE BELOW 60C | Đợi nhiệt độ xuống dưới ngưỡng khởi động lại |
| OUTPUT CURRENT >0.2A | Kiểm tra offset/đường đo dòng hoặc dòng ngoài khi OFF |
| WAIT 100MS THEN RETRY | Đợi cảm biến ổn định rồi nhấn BTN1 lại |

Thông báo từ chối giữ lại để đọc; sau khi xử lý nguyên nhân, nhấn ngắn BTN1
để thử lại. ON thành công hoặc thao tác OFF/xóa lỗi thành công sẽ xóa lý do
trước. Các lỗi runtime vẫn hiện **FAULT** cùng tên lỗi; giữ BTN1 để xóa khi đủ
điều kiện. Lỗi cấu hình hiện **LOCK** kèm loại cấu hình cần kiểm tra.

Khi OVP tác động từ mẫu ADC thô, dòng lỗi chốt hai số, ví dụ
`OVP 06.19V>05.74V HOLD OUT TO CLEAR`: bên trái là điện áp quy đổi từ chính
mẫu gây lỗi, bên phải là ngưỡng OVP quy đổi từ mã ADC so sánh. Số VOUT lớn
ở phía trên vẫn là giá trị đang đo sau khi ngắt, nên có thể đã giảm về 0 V.
Snapshot giữ nguyên qua các callback DMA tiếp theo và chỉ xóa khi người dùng
xóa lỗi thành công. Đây là số đo ADC, chưa phân biệt được vọt áp VOUT thật
với nhiễu đường đo; cần đối chiếu oscilloscope nếu có. Ở setpoint 5 V, ngưỡng
ADC tương đương khoảng 5,737 V (LCD làm tròn 5,74 V); setpoint 12 V là
khoảng 13,098 V. Bản chẩn đoán này giữ nguyên ngưỡng và số mẫu bảo vệ.

USB bổ sung `start_block`, `bench`, `imax_mA`, `ipeak_mA`, `pmax_W` để chẩn đoán.
`adc_src:"cont"` xác nhận bản continuous, `samples` phải tăng cả khi OFF,
`adc_age` thường 1 ms khi dữ liệu mới, `dma_left` là số phần tử DMA còn lại
(buffer 24 phần tử). Nếu `samples` đứng yên, kiểm tra DMA1 channel1/NVIC và
ADC1; không cần bật PWM để có dữ liệu. Bản này bỏ đường trigger TIM1 CC3
cũ gây `ADC NO DATA` khi ngõ trigger không hoạt động.

JSON USB bổ sung object `trip`: `cause` là bit lỗi đầu tiên do mẫu ADC thô,
`sample` là số thứ tự frame ADC, `vo_adc`/`io_adc`/`vi_adc` là mã thô lúc lỗi,
`ovp_adc` là ngưỡng OVP ở cùng thời điểm. Với VDDA/gain mặc định,
VOUT = `vo_adc * 3.3 / 4095 * 21`; áp dụng thêm `PSU_VOUT_CAL` khi hiệu chuẩn.
`cause=0` nghĩa là chưa có snapshot ADC hoặc lỗi được phát hiện bởi nhánh
khác (ví dụ mất dữ liệu/nhiệt). Buffer JSON của app là 768 byte và giữ nguyên
đến khi CDC truyền xong; không thay đổi baud/USB endpoint.

Touch SPI dùng T_CS PB12, IRQ PB13 pull-up được poll; NVIC EXTI15_10 tắt.
Bus đổi sang 1,125 MHz/mode 0 khi đọc touch rồi trả lại cấu hình LCD.
SWD vẫn dùng được
trên PA13/PA14. PC15 không có thao tác chuyển sang khối hàn.

## Ưu tiên ngắt

NVIC priority group 4; số nhỏ ưu tiên cao hơn.

| Nguồn | Priority |
|---|---|
| ADC1 DMA1 channel1 | 0 |
| TIM3 control | 1 |
| USB LP | 3 |
| EXTI4, EXTI9_5 | 4 |
| TIM2 | 4, app tắt vì đọc counter không cần IRQ |
| Touch EXTI15_10 | NVIC tắt; poll foreground, không SPI trong ISR |
| SysTick | 15 |

Không gọi delay, SPI, USB hoặc Flash trong ngắt điều khiển. SPI blocking chỉ
ở foreground; các ngắt bảo vệ/điều khiển vẫn chạy. Flash chỉ ghi khi gate đã
ngắt; tạm dừng TIM3/ADC DMA và khởi động lại sau lưu. Ghi Flash có thể gây
gián đoạn USB ngắn vì CPU stall trên STM32F1.

## Build và kiểm thử

Arm GNU Toolchain đã có trên máy:

```powershell
cd D:\My_project\buck_boost_converter\Firmware_BB_V6
python tools\build.py
```

File nạp: `build/Firmware_BB_V6.hex` hoặc `.bin` tại địa chỉ **0x08000000**.
Có `.elf`, `.map` và `compile_commands.json`. Linker giới hạn firmware 62 KiB,
RAM 20 KiB; dành 0x0800F800 và 0x0800FC00 cho hai trang preset. Khi nạp lại,
không mass erase nếu muốn giữ preset. Không giả định chip C8 có Flash 128 KiB.

Keil: mở `MDK-ARM/Firmware_BB_V6.uvprojx`; đã thêm bốn module, scatter file
`psu.sct` chừa hai trang cuối, stack 2 KiB. Chưa chạy build Keil vì compiler
Keil chưa được tìm thấy; build thực hiện bằng GCC.

Sau khi Generate Code từ CubeMX:

```powershell
python tools\integrate.py
python tools\build.py
```

Script idempotent nối lại user hooks, ADC/timer/GPIO/NVIC và danh sách source
Keil. `.ioc` đã cập nhật ADC1 continuous/software-start, sample time 239,5
chu kỳ và DMA circular. Kiểm tra diff sau mỗi lần regen; không coi .ioc là
nơi chứa toàn bộ chính sách điều khiển.

Kiểm thử C thật trên Unicorn Cortex-M3, không dùng bộ điều khiển Python thay thế:

```powershell
python -m pip install --target tools/_deps unicorn==2.1.4 pillow
python tools\test_control.py
python tools\test_app.py
python tools\test_storage.py
python tools\preview_display.py
```

Kiểm thử gồm bản bench thực 10 A/8 A, fixture production đã xác minh và
production chưa xác minh (phải khóa). Có kiểm tra BTN1/debounce/ON/OFF,
hai mẫu OCP xuyên qua callback DMA, giới hạn encoder/preset cũ và lý do ON
bị từ chối. Kiểm tra luồng continuous khi PWM tắt/bộ đếm TIM1 dừng, mean
bốn mẫu được callback công bố, TIM3 lấy mean, mất ADC khóa ON/cắt RUN và
ADC2 đo nhiệt độc lập với ADC1. Có regression VIN 15 V/VOUT 4,97 V trước ON,
BTN1 bật/tắt lại được, xóa lỗi khi VOUT còn điện áp sau thời gian ổn định,
RUN giữ PWM 0 nếu VOUT ban đầu cao hơn tham chiếu và OVP vẫn trip đúng.
Kiểm tra OFF kẹp hai ngõ IN, thay duty cũ trước khi mở lại PWM, lưu preset
trở về standby, mất ADC/nhiệt hoặc timer dừng thì SD=0; foreground treo ở
OFF cũng chốt lỗi timing. `python tools/test_app.py --diagnostic` kiểm tra
thêm cấu hình test không cho RUN; mặc định kiểm tra bản chính.
Kiểm tra snapshot lỗi giữ mẫu/ngưỡng đầu tiên dù các mẫu sau thay đổi, xóa
snapshot khi xóa lỗi, JSON CDC truyền đủ snapshot và preview LCD OVP có đủ số.
Journal Flash V3 được kiểm tra cùng nâng cấp V1/V2, mất điện tại mọi ranh giới
erase/program và CRC hỏng. Fixture test không sửa cấu hình firmware nạp.
Ảnh preview trong `build/preview/` được dựng từ byte SPI do driver C thực phát.
Các kiểm thử này chưa xác nhận gate timing, ổn định CC/CV, EMI, nhiệt hoặc USB
enumeration vật lý trên board.

## Test USB và đưa tầng công suất vào hoạt động

1. Nạp bản mặc định với nguồn MCU, chưa cấp điện tầng công suất. Kiểm tra
   PA10 LOW khi VIN/cảm biến chưa hợp lệ. Khi đủ điều kiện, PA10 HIGH,
   PA8/PA9 LOW, hai low-side bật cố định, không có xung gate. LCD hiện OFF.
   Nếu thử
   ON mà chưa có VIN/NTC hợp lệ, LCD hiện BLOCK cùng lý do từ chối.
2. Cắm cáp USB data; tìm Virtual COM Port trong Device Manager. STM32F103 cần
   pull-up D+ 1,5k lên 3,3 V đúng phần cứng. USB clock đang là 48 MHz từ HSE
   **8 MHz**, PLL72 /1,5; xác nhận thạch anh board đúng 8 MHz.
3. Mở terminal COM 115200 8N1; baud chỉ là line coding CDC. Nhận mỗi dòng JSON
   với đơn vị số nguyên: mV, mA, nhiệt 0,1 C, duty phần nghìn; `boost_ho_pm`
   là duty MOSFET TRÊN phía boost. `?` yêu cầu log sớm; `!` yêu cầu OFF.
   USB hỗ trợ SET/ON/OFF và cấu hình; dùng app đi kèm hoặc xem USB_PROTOCOL.md.
4. Xác nhận phần cứng đo, đổi hằng số khớp mạch thực. Đo VDDA và hiệu chuẩn
   gain/offset với đồng hồ, nguồn tham chiếu/tải điện tử ở nhiều mức.
5. Xác nhận NTC bằng nhiệt kế, nguồn gate, gate polarity/deadtime bằng oscilloscope,
   dòng bão hòa cuộn cảm, MOSFET, tụ/ESR, đầu nối và giới hạn công suất/dòng vào.
6. Bản bench hiện cho BTN1 hoạt động với trần thử tạm 60 W/5 A ước tính.
   Thử công suất thấp với nguồn vào giới hạn dòng; xác định đường ngắt phần
   cứng và rating thực trước khi điền cờ xác minh hoặc tăng ngân sách.
7. Đo đáp ứng bước tải và Bode/biên ổn định để tune PI trong cả buck/boost,
   chuyển CC/CV, setpoint giảm, không tải, tải dung và ngắn mạch có kiểm soát.
   Các gain hiện tại chỉ là giá trị khởi đầu; Cout đã biết danh định 1000 uF,
   nhưng còn thiếu ESR, đặc tính cuộn cảm và đáp ứng đo thực để chốt bù PI.
8. Kiểm tra từng lỗi bằng tín hiệu giả lập trước khi tăng công suất; đo độ trễ
   thật và `cycles` qua CDC. Chạy nhiệt dài hạn trước khi nghiệm thu 8 A liên tục.

Hai chân PWM mang lệnh HO của từng IR2184S. Dùng cả hai cầu chuyển mạch để
tránh high-side bootstrap phải giữ 100%: VOUT/VIN gần D_buck_HO/D_boost_HO,
giới hạn duty cao 90%. Không áp dụng công thức 1/(1-D) cho duty HO boost này.
Thuật toán này chưa được hiệu chỉnh theo deadtime/tổn hao cụ thể của board.

## Đối chiếu ảnh schematic tầng công suất

Các giá trị đọc từ ảnh chỉ là giá trị schematic, chưa chứng minh rating/linh
kiện đã lắp. Chủ board đã xác nhận lại **tổng shunt thực tế 1 mOhm**; hai nhãn
R29/R6 mỗi con 0,01 Ohm trong ảnh không dùng để đổi hệ số firmware thành 5 mOhm.
Không đổi gain 200 hoặc compensation PI từ ảnh. Việc cho phép BTN1 ở chế độ
thử 10 A/8 A là theo yêu cầu riêng sau đó của chủ board.

Với hai half-bridge N-MOS đúng topology buck-boost, kết nối D/S cần là:

| MOSFET | Gate driver | Drain | Source |
|---|---|---|---|
| Q1, buck high-side | HS1 / driver buck HO | POWER/VIN | VS1 |
| Q3, buck low-side | LS1 / driver buck LO | VS1 | PGND |
| Q2, boost high-side | HS2 / driver boost HO | V_OUT | VS2 |
| Q4, boost low-side | LS2 / driver boost LO | VS2 | PGND |

**Theo ảnh schematic cập nhật, Q2 đã được đảo đúng chiều:** Source nối VS2,
Drain nối V_OUT, diode thân cho chiều VS2 -> V_OUT. R44 kéo gate về Source
Q2 = VS2, phù hợp với bảng trên; Driver boost VS cũng cần nối VS2. Đây là
đối chiếu ký hiệu và đường nối trên ảnh, chưa xác minh mapping symbol/footprint
hoặc PCB thực tế. Sơ đồ mới phù hợp với quy ước PWM HO boost trong firmware;
không cần đảo cực tính CH2 để xử lý chiều Q2.
Vẫn giữ cờ tầng công suất chưa xác minh do còn thiếu rating, đo kiểm và thông
tin mạch đo dòng/driver đầy đủ; bench mode chỉ cho phép thử theo yêu cầu,
không coi việc sửa chiều Q2 là xác nhận rating tầng công suất.

C15 âm nối PGND, còn terminal Vout_N nằm phía trên shunt. Nếu cầu chia VOUT
đo V_OUT so với PGND, điện áp tại cọc tải là VOUT_sense - IOUT x Rshunt.
Với shunt 1 mOhm, chênh khoảng 8 mV tại 8 A. Phải biết chân dưới cầu chia
đang về PGND hay Vout_N trước khi thêm bù sụt shunt trong firmware.
Nếu tải/máy đo và USB PC tạo một đường mass ngoài nối Vout_N về PGND thì
đường đó sẽ bypass shunt, làm sai phép đo và mất tác dụng CC qua cảm biến này.
Phải kiểm tra đường mass/cách ly trong cấu hình dùng thực tế.

Shunt ở nhánh cọc tải đo dòng tải, không đo trực tiếp dòng cuộn cảm hoặc toàn
bộ dòng đóng cắt/nạp C15. Không dùng riêng tín hiệu này để khẳng định giới
hạn đỉnh cuộn cảm/MOSFET 10 A, nhất là ở chế độ boost. Cần phần sơ đồ INA199,
driver/nguồn gate và thông số Isat/ESR/rating để hoàn tất đánh giá.

## Tài liệu gốc đối chiếu

- [STM32F103C8 datasheet, ST](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)
- [IR2184S datasheet, Infineon, hình 1 và 3 xác nhận /SD active LOW](https://www.infineon.com/assets/row/public/documents/24/49/infineon-ir21844s-datasheet-en.pdf)
- [INA199 datasheet, Texas Instruments](https://www.ti.com/lit/ds/symlink/ina199.pdf)
- [IRFZ44 datasheet, Vishay, đối chiếu N-MOS và diode thân](https://www.vishay.com/docs/91291/irfz44.pdf)
- [LM5176 datasheet, TI, tham khảo topology bốn N-MOS](https://www.ti.com/lit/ds/symlink/lm5176.pdf)
