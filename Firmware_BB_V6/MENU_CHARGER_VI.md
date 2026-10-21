# BBV6.13 — menu, ramp, sạc pin và chuyển tay hàn

Bản này giữ PWM 10 kHz và PID CV/CC đã tuning. Touch vẫn được gỡ khỏi bản build.
Digit đang chỉnh có số và gạch chân màu đỏ. TIME cạnh POWER đếm thời gian output
thực sự chạy, dừng khi OFF/đầy/lỗi và về 0 khi bắt đầu lượt mới. Bộ đếm gồm cả
giai đoạn nạp trước, không tính thời gian kiểm tra pin trước khi bật.

## Thao tác

- Nhấn encoder: đổi digit trên màn hình chính. BTN2 đổi V/A trong LAB; chế độ
  sạc chỉ chỉnh dòng, điện áp được khóa theo loại pin và số cell.
- Giữ encoder 1 giây: mở menu. Xoay để chọn, nhấn để vào/chỉnh/xác nhận.
- Giữ encoder trong menu con: về menu chính; giữ tại menu chính: thoát.
  BTN2 cũng quay lại; nếu đang chỉnh thì kết thúc chỉnh giá trị hiện tại.
- BTN1 luôn dừng output khi ở menu. Muốn bật lại, thoát menu rồi nhấn BTN1.
  Giữ BTN1 để dừng/xóa lỗi khi đủ điều kiện.
- Đổi chế độ hoặc profile cần OFF. M1–M3 chỉ dùng trong LAB như trước.

## Ramp

RAMP SPEED có FAST **30 V/s**, NORMAL **10 V/s**, SLOW **2 V/s**. Chọn lúc OFF,
firmware tự lưu vào Flash. App vẫn cho nhập tốc độ khác qua cấu hình ramp.
Đây là tốc độ tăng điện áp tham chiếu; giảm điện áp đặt có hiệu lực ngay,
điện áp thực phụ thuộc tải/tụ và giới hạn dòng. Bảo vệ và CC vẫn hoạt động.

## Sạc pack có BMS

Vào BATTERY CHARGER, chọn TYPE, CELLS, CHARGE và TIME LIMIT; chọn USE CHARGER
để về màn hình sạc, rồi nhấn BTN1 để bắt đầu. USE CHARGER chưa bật output.
Không tự suy đoán số cell từ điện áp pack.

| Thông số | Li-ion/LiPo | LiFePO₄ |
|---|---|---|
| Số cell nối tiếp | 1S–6S, người dùng chọn | 1S–6S, người dùng chọn |
| Điện áp CV | 4,20 V/cell | 3,65 V/cell |
| Từ chối pack dưới | 2,50 V/cell | 2,00 V/cell |
| Nạp trước khi dưới | 3,00 V/cell | 2,80 V/cell |

Chỉ dùng profile khi điện áp sạc do nhà sản xuất cell quy định khớp bảng trên.
Dòng mặc định **0,50 A**, chỉnh từ **0,10 A** đến giới hạn Imax hiện tại, bước
50 mA trong menu hoặc theo digit ở màn hình chính. Chọn dòng theo cell/BMS;
giới hạn 8 A của thiết bị không phải dòng phù hợp cho mọi loại pin.

Trình tự điều khiển:

1. CHECK: kiểm tra pack và cảm biến trong 1 giây, chưa phát PWM. Pack quá thấp,
   quá cao, profile ngoài giới hạn hoặc cảm biến/input không hợp lệ bị từ chối.
2. PRE: nạp trước với 10% dòng đặt, tối thiểu 50 mA và tối đa 200 mA, điện áp
   tham chiếu bằng ngưỡng nạp trước. Vượt gần ngưỡng liên tục 5 giây mới sang CC/CV.
3. CC/CV: giới hạn dòng rồi giữ điện áp theo profile bằng vòng điều khiển hiện có.
4. FULL: ở CV gần điện áp đích, dòng đã lọc và dòng thô thấp hơn ngưỡng kết thúc
   liên tục 60 giây thì ngắt PWM. Ngưỡng kết thúc = 10% dòng đặt, tối thiểu 20 mA.
   Biến động dòng/điện áp hoặc quay lại CC sẽ đặt lại bộ đếm 60 giây.

Timeout toàn lượt mặc định 6 giờ, chỉnh 1–12 giờ; nạp trước tối đa 30 phút.
Không có dòng sau 120 giây ở PRE hoặc 30 giây ở CC/CV sẽ dừng, trừ pack đã gần đầy
ngay từ đầu có thể xác nhận FULL. Pack tụt dưới ngưỡng tối thiểu trong 50 ms sẽ dừng.
OVP thô riêng cho sạc đặt tại điện áp đích + max(50 mV, 30 mV × số cell), kiểm tra
trên từng mẫu ADC. Các bảo vệ input/dòng/nhiệt/ADC/timing cũ tiếp tục hoạt động.
TIME và mAh chỉ là ước lượng từ mẫu đo; chưa hiệu chuẩn như thiết bị đo dung lượng.

Khi FULL, OFF hoặc lỗi, toàn bộ gate được shutdown và không phát xung xả tụ.
Không tự sạc lại sau FULL/lỗi/mất nguồn; cần thao tác bật mới. Khi chuyển về LAB,
xả chủ động vẫn bị khóa cho đến khi OFF và VOUT dưới 1,5 V liên tục khoảng 2 giây.
**Tháo pack trước khi trở lại LAB**, vì LAB OFF bình thường chủ động bật low-side.

Lỗi chi tiết và sự kiện FULL được lưu trên app v6.13. LCD hiển thị trạng thái
và lý do ngắn. Loại pin, số cell, dòng sạc, timeout và chế độ hiện chỉ lưu RAM;
reset luôn trở về LAB/OFF, PC15 = 0. Ramp vẫn lưu Flash V3; PID/preset cũ được giữ.

## SOLDER

Khi output OFF, chọn SOLDER: shutdown bộ DC-DC, chờ tối thiểu 100 ms rồi bật
CHANGE/PC15 = 1 nếu input/cảm biến hợp lệ. BTN1 dừng hoặc bật lại đường chuyển;
lỗi input/nhiệt/ADC làm PC15 về 0 và không tự bật lại. Muốn về LAB, dừng bằng BTN1
rồi chọn LAB SUPPLY. App cho dừng đường chuyển, không cho khởi động SOLDER từ xa.
Hiện chỉ chuyển nguồn; chưa có PID nhiệt hoặc điều khiển tay hàn.

## Kiểm tra trước khi nối pin

Chức năng mới đã kiểm thử logic trên mô phỏng Cortex-M3 và app demo; **chưa được
nạp và xác nhận sạc pin trên phần cứng**. Kiểm tra trước bằng nguồn giả lập pin/tải
phù hợp: đúng profile/CV/CC, giới hạn dòng, OVP, timeout, dừng và không xả khi OFF.
Đối chiếu VOUT/IOUT với đồng hồ và hiệu chuẩn trước khi dùng pin thật.

Board chỉ đo nhiệt FET, chưa đo nhiệt pack hay điện áp từng cell. BMS ngoài phải
phù hợp chemistry/số cell, bảo vệ và cân bằng cell; cần bảo vệ nhiệt pack phù hợp.
INA199 hiện đo dòng một chiều nên không phát hiện được dòng ngược. Shutdown gate
không tạo cách ly vật lý khỏi pack: diode thân MOSFET và đường bootstrap vẫn tồn
tại. Muốn chặn dòng ngược/cách ly thực sự cần phần cứng phù hợp. Không dùng chế độ
này để phục hồi pack BMS đã ngắt hoặc cell xuống dưới ngưỡng cho phép.

Nguyên tắc CC/CV, nạp trước và kết thúc theo dòng tham khảo tài liệu
[TI BQ25306](https://www.ti.com/product/BQ25306); đây không phải chứng nhận tương
đương một IC sạc chuyên dụng.
