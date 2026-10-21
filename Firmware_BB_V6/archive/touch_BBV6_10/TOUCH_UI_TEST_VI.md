# BBV6.10 — hướng dẫn test UI và touch XPT2046

Đã chuẩn bị HEX và app, chưa nạp board hay xác nhận touch vật lý.
Giữ nguyên PID đã chốt (CV 0,015/0,20/0; CC 0,01/1,0/0), encoder đảo chiều,
BTN2 đổi V/A và nhấn encoder đổi digit. Ramp cấu hình trên app vẫn hoạt động.

## Cấu hình touch

- Người dùng đã xác nhận controller touch điện trở XPT2046.
- SPI1 dùng chung LCD: PA5 SCK, PA6 MISO, PA7 MOSI; PB12 CS, PB13 IRQ.
- Touch mode 0, SPI 562,5 kHz. Khởi tạo PD1:PD0=00, đọc lực nhấn Z1/Z2,
  giữ PD0=1 khi đọc từng trục và bỏ mẫu settling sau khi đổi kênh. Median 5
  mẫu, kiểm tra ba mẫu giữa để chịu được một mẫu nhiễu đơn lẻ; bỏ median rail.
  Luôn gửi PD1:PD0=00 cuối giao dịch và trả lại clock LCD cả khi timeout.
- IRQ được pull-up/poll; đọc lực nhấn dự phòng mỗi khoảng 30 ms khi IRQ cao,
  khoảng 10 ms khi chạm/CAL. Không bật OUT chỉ vì chân IRQ bị kẹt thấp.
- Tham khảo: [XPT2046 datasheet, XPTEK](https://files.waveshare.com/wiki/common/XPT2046_Datasheet.pdf),
  trang 17–18 về settling, trang 24–25 về PD0/PENIRQ.
- Các macro `PSU_TOUCH_*` trong `Core/Inc/psu_config.h` là hiệu chuẩn ban đầu.
  CAL trên thiết bị tự nhận swap/invert qua 4 điểm, hiện chỉ lưu RAM.

## Test khi quay lại

1. Nạp `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex` (Keil) hoặc
   `build/Firmware_BB_V6.hex` (GCC) BBV6.10, mở
   `PC_App/dist/BB_V6_Studio_v6_9.exe` nếu cần xem lỗi/tọa độ.
2. Giữ nút encoder khi reset để vào CAL. Cách này không cần touch đang đúng
   tọa độ. Có thể chạm CAL ở góc trên khi output OFF nếu mapping tạm dùng được.
3. Chạm và nhả lần lượt dấu cộng xanh: trên trái → trên phải → dưới trái →
   dưới phải. Dùng bút nhựa/stylus, giữ chạm khoảng 0,2–0,3 s mỗi điểm.
   BTN2 hủy CAL.
4. Chạm chữ số trong SET V/SET A để chọn mục/digit; nhấn ± để đổi đúng bước.
   Encoder vẫn dùng song song. Dòng/áp được chặn trong giới hạn cấu hình cũ.
5. Khi OFF, chạm M1–M3 để gọi preset; giữ 1 s để lưu. Khi ON không đổi preset.
6. Touch bật OUT chỉ được mở sau khi CAL thành công: giữ OUT 1 s rồi nhả,
   không kéo ngón sang vùng khác. Khi đang ON, chạm OUT để tắt. RESET cần giữ
   1 s và vẫn tuân thủ điều kiện xóa lỗi của MCU, không tự bật lại.
7. Reset MCU làm mất hiệu chuẩn RAM: màn hình hiện CAL FIRST cho touch ON.
   BTN1 vật lý vẫn bật/tắt bình thường. Nếu touch chưa đáp ứng, dùng encoder/
   nút vật lý; gửi phần `touch` trong Thông số thô để chỉnh tiếp.

Nếu chưa nhận touch: ở app mở Nhật ký & chẩn đoán → Thông số thô, xem `touch`
khi thả và khi ấn. `reads` phải tăng cả khi IRQ luôn 1. `status=1`, `down=1`
và `valid` tăng báo đã đọc được touch; nếu pixel sai thì thực hiện CAL.
`status=2` liên tục khi ấn: kiểm tra kết nối panel/độ nhấn. `status=3` với Z1/Z2
cùng 0 hoặc cùng 4095: kiểm tra T_DO→PA6, T_DIN→PA7, T_CLK→PA5, T_CS→PB12,
nguồn 3,3 V và GND touch. `status=4/errors` tăng: lỗi SPI. IRQ PB13 không còn
là điều kiện bắt buộc; mức thấp lúc ấn vẫn là dấu hiệu đấu dây IRQ đúng.

Trong CAL, output luôn OFF; ON từ app/USB bị từ chối. Nhiễu/timeout SPI,
ngón kéo khỏi vùng và chạm bị giữ từ lúc boot không tự bật output. OFF ưu tiên
hủy gesture đang chờ. Không thay đổi bảo vệ OVP/OCP/SHORT/ADC/nhiệt.

## LCD và app

LCD chỉ giữ số đo, mức đặt, chế độ CV/CC/RAMP/OFF/DRAIN/PROTECT và hướng dẫn
ngắn. Không hiển thị dòng chẩn đoán ADC/OVP dài ở footer nữa.

App có Nhật ký & chẩn đoán → Lịch sử lỗi, chọn dòng để xem snapshot gồm
fault/config/start_block, V/I/VIN/nhiệt, raw trip, cấu hình đã xác nhận và
touch nếu có. Nhật ký JSONL tự ghi trong `%LOCALAPPDATA%/BB_V6_Studio/logs`,
có nút Xuất nhật ký lỗi. Chỉ ghi được dữ liệu app đã nhận; MCU vẫn giữ lỗi
chốt và snapshot đầu tiên đến khi xóa, không lưu toàn bộ lịch sử offline.

Ảnh preview từ driver C thực qua mô hình SPI tại `build/preview/`:
`normal.png`, `fault.png`, `cal_1.png`, `touch_off.png`, `pc_fault_log.png`.
Kiểm thử phần mềm không thay thế đo gate/current hay test touch thật.
