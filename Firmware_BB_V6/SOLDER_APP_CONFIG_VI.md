# BBV6.28 — chỉnh áp và dòng tay hàn trên app

App mới: `PC_App/dist/BB_V6_Studio_v6_28.exe`. `PC_App/Chay_app.cmd` ưu tiên mở bản này. Cần firmware BBV6.28 để board nhận hai thông số mới; app mới vẫn dùng được với board cũ, nhưng khóa riêng hai ô tay hàn.

## Cách dùng

1. Nạp `build/Firmware_BB_V6_BBV6_29_ONOFF50MS_OFF15MS.hex` hoặc HEX Keil cùng nguồn tại `MDK-ARM/Firmware_BB_V6/Firmware_BB_V6.hex`. Mở app v6.28 và kết nối USB như trước.
2. Tắt output tay hàn. Có thể ở LAB/OFF hoặc SOLDER/OFF, đóng menu trên thiết bị và dừng xả nếu có. Không cần đổi về LAB khi tay hàn đã OFF.
3. Nhấn **Áp / dòng tay hàn** ở Tổng quan, hoặc mở tab **Cấu hình → Tay hàn** ngay phía trên.
4. Nhập **Điện áp cấp tay hàn (V)** và **Giới hạn dòng tay hàn (A)**.
5. Nhấn **Áp dụng cấu hình**, chờ board xác nhận. Muốn giữ sau reset, nhấn **Lưu Flash** và chờ xác nhận SAVE.
6. Bật tay hàn bằng nút trên thiết bị. Kết nối app hoặc áp dụng cấu hình không tự bật heater; app vẫn chỉ cho dừng tay hàn từ xa.

Hai thông số dùng chung cho C245/C210, độc lập điện áp/dòng LAB và preset M1–M3. Mặc định 18,00 V / 6,00 A. Dải nhập theo firmware: 1,50–59,90 V và 0,05–15,00 A; giá trị trên app còn phải nằm trong giới hạn VOUT/dòng liên tục hiện tại của board. Đây là dải cấu hình phần mềm, không phải xác nhận mức chịu được của mũi hàn. Giới hạn dòng là lệnh CC của nguồn DC-DC, không chặn tức thời dòng phóng từ tụ đầu ra khi đóng heater.

Firmware từ chối cấu hình khi output hoặc yêu cầu khởi động SOLDER đang hoạt động, kể cả giai đoạn đang chờ chuyển PC15 chưa bật DC-DC. Đang sạc phải dừng và chuyển về LAB. Lệnh SET V/A thông thường tiếp tục bị khóa trong SOLDER để không ghi nhầm điện áp LAB sang tay hàn.

Từ firmware v6.29, chu kỳ heater là **50 ms**, OFF ít nhất **15 ms**, ON tối đa **35 ms** theo sai lệch nhiệt. PB13 vẫn GPIO ON/OFF; watchdog và các bảo vệ nguồn/nhiệt giữ nguyên. Trong ON tối đa 35 ms, nhiệt hiển thị giữ mẫu cuối lúc OFF; được cập nhật lại trong mỗi frame. [Chi tiết chu kỳ](SOLDER_50MS_VI.md).

## Lưu Flash và tương thích

- Thêm `solder_voltage` và `solder_current`, đơn vị truyền số nguyên mV/mA, dùng lệnh CFG và SAVE hiện có. Settings có 39 trường; protocol vẫn 1.
- Hai trường được nối cuối cấu trúc, không đổi vị trí PID/ramp cũ. Journal mới V4 có CRC và commit cuối; vẫn đọc V1/V2/V3, giữ PID/preset/giới hạn/ramp cũ và bổ sung 18 V/6 A trong RAM.
- Nếu profile cũ có giới hạn nguồn thấp hơn 18 V/6 A, chỉnh hai ô tay hàn cho phù hợp trước khi bật. Firmware tiếp tục chặn khởi động profile vượt giới hạn.
- Chỉ SAVE mới ghi V4 vào hai trang cấu hình; nạp HEX không chạm từ 0x0800F800 trở lên. Firmware cũ không đọc được journal V4; nên xuất profile trước khi quay về bản cũ.
- Profile JSON 36/37 trường cũ được app mới bổ sung giá trị mặc định của hai trường. PID và mức đặt LAB vẫn giữ. App v6.19 cũ không hỗ trợ đủ settings mới; dùng app v6.28 đi kèm firmware v6.28.

## Kiểm chứng

Đã kiểm thử lệnh CFG qua mã application thật trên Cortex-M3 giả lập: lưu 7,50 V / 1,20 A khi SOLDER/OFF; bật bằng nút và đọc lại đúng mức đặt nguồn; từ chối thay khi đang chờ/ON và từ chối ngoài dải. Kiểm thử journal có cắt nguồn tại từng thao tác erase/program, bao gồm chuyển V3→V4, và đọc lại đúng hai giá trị mới.

App Python và EXE có kiểm thử DEMO/Tk: hiển thị 39 trường, áp dụng/lưu 9,00 V / 2,00 A, khóa ô mới khi nối firmware cũ, và mở cấu hình/lưu ở SOLDER/OFF. DEMO không kết nối COM hoặc điều khiển board thật.

Keil giữ `-Os`/MicroLIB và bật LTO để phần bổ sung vẫn nằm trong vùng chương trình 62 KiB. GCC/Keil được kiểm tra giới hạn HEX và vùng Flash cấu hình. Chưa nạp, bật hoặc đo board thật trong lượt sửa này.
