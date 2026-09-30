# Duwn Mirror — tùy chọn độ trễ hiển thị

Bản sửa được chuẩn bị từ repo https://github.com/leduwn/Duwn-Mirror, commit
`755b4ad957223b95bdb64cbee3eba10b873b6f79` ngày 30/09/2026.

Giữ cách kết nối Screen Mirroring có sẵn trên iPhone. Đây là bản tối ưu hàng đợi
và hiển thị phía Windows; chưa thay toàn bộ UxPlay và chưa có số đo độ trễ thực tế mới.

## Bản sửa làm gì

| Lựa chọn trong Video → Độ trễ hiển thị | Cách hoạt động |
|---|---|
| Cân bằng | Giữ cách phát cũ: tối đa 3 khung chờ, bắt kịp khi bộ đệm trễ |
| Nhanh nhất | Chỉ giữ khung hình đã giải mã mới nhất, tối đa 1 khung chờ |
| Ưu tiên mượt | Giữ tối đa 3 khung chờ, ngưỡng bắt kịp rộng hơn |
| Tùy chỉnh | Chọn 1–3 khung chờ, ngưỡng bắt kịp 5/10/16/25/40/60/100 ms |

Chuyển chế độ áp dụng ngay, không cần kết nối lại. Cài đặt được lưu và đọc lại
khi mở app. Độ phân giải, FPS yêu cầu và âm thanh vẫn dùng lựa chọn riêng của bạn.
Nhanh nhất có thể bỏ khung hình khi dữ liệu đến dồn; không tái mã hóa hoặc giảm
độ nét của ảnh đã nhận.

**5–100 ms là ngưỡng xử lý khung hình chờ, không phải cam kết độ trễ iPhone → PC.**
App không đợi đủ số khung hoặc cố tình chờ đủ số mili giây này để phát.

Đã bỏ một lần chờ sự kiện thừa, tối đa 16 ms, trong nhánh không có DXGI waitable
(thường liên quan renderer phần mềm). Vẫn giữ nhịp chờ DXGI ở đường phần cứng.

## Vì sao chưa bỏ UxPlay ngay

Báo cáo có sẵn trong repo ghi nhận:

- Tắt `-vsync` không cải thiện đường chuyển tiếp RTP.
- Tắt `sync` ở cả hai sink từng gây mất audio 3,356 giây và tràn hàng đợi.
- Pipeline native T0→T7 từng đạt trung bình 2,83 ms; mốc T0 nằm sau UxPlay nên
  không bao gồm mã hóa trên iPhone, Wi-Fi, xử lý UxPlay và quét màn hình PC.

Vì vậy bản sửa giữ cấu hình chuyển tiếp ổn định đã có. Nếu bật Nhanh nhất mà vẫn
trễ nhiều trong khi hàng đợi Duwn gần 0, cần đo đầu vào/đầu ra UxPlay rồi quyết
định thay tầng đó. Chỉ viết lại code receiver không đảm bảo nhanh ngang
ApowerMirror/DouWan khi chưa so sánh trên cùng thiết bị và nội dung.

## Áp dụng trên Windows

1. Giải nén gói này. Sao chép `Duwn-Mirror-Latency.patch` vào thư mục gốc repo
   Duwn-Mirror trên máy bạn, cạnh `CMakeLists.txt`.
2. Mở PowerShell/terminal tại repo. Nếu đang có sửa đổi riêng, commit hoặc lưu
   chúng trước để việc áp dụng patch dễ kiểm tra.
3. Chạy:

```powershell
git switch -c perf/receiver-latency-controls
git apply --check .\Duwn-Mirror-Latency.patch
git apply .\Duwn-Mirror-Latency.patch
```

`--check` chỉ kiểm tra khả năng áp dụng. Nếu báo xung đột, chưa chạy lệnh apply;
để công cụ lập trình đối chiếu với commit mới của bạn. Không cần ghi đè repo.

4. Với môi trường CMake/Visual Studio/vcpkg đã cấu hình theo `docs/build.md`:

```powershell
cmake --preset debug
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
```

5. Mở bản app vừa build. Trong tab Video, chọn **Nhanh nhất**. Giữ Receiver
   Quality đang dùng để so sánh cùng nguồn; Output để theo nguồn/Original giúp
   tránh suy luận sai về độ nét do phóng ảnh. Nếu thấy bỏ khung nhiều, thử
   **Tùy chỉnh → 2 khung → 25 ms**, hoặc trở về **Cân bằng**.

6. Kiểm tra đổi chế độ khi đang phát, tiếng Việt/Anh, cửa sổ nhỏ có cuộn đầy đủ,
   và mở lại app để kiểm tra lưu cấu hình. Khi đổi Receiver Quality, cơ chế
   reconnect vốn có của app vẫn áp dụng.

Gói còn có `changed-files/` chứa đúng 17 file mới/sửa để đối chiếu. Nên dùng
patch thay vì sao chép đè các file vì repo của bạn có thể đã thay đổi.

## Kiểm thử đã chạy và phần còn thiếu

- 6/6 kiểm thử chính sách C++20 portable qua compiler với cảnh báo là lỗi.
- 36/36 kiểm thử scheduler/chính sách qua trên Linux với mô phỏng hàm Win32 và
  giao diện GPU rỗng. Dùng chính source scheduler thật, gồm bộ kiểm thử cũ,
  burst 1.000 khung, đổi chế độ ngay và giảm giới hạn hàng đợi khi đang hoạt động.
- Đọc/ghi Settings thật qua mô phỏng hệ điều hành: cả 4 chế độ được lưu/đọc lại;
  giá trị sai được giới hạn; settings v2 cũ dùng mặc định đúng.
- Bảng tiếng Việt/Anh đầy đủ và nhãn mới đúng.
- Patch áp dụng sạch trên commit nền và tạo ra đúng từng byte của 17 file sửa.

Chưa build toàn bộ app với Windows SDK, chưa kiểm tra giao diện Windows bằng
mắt, chưa đo trên iPhone hoặc kiểm chứng chất lượng/âm thanh thực tế sau sửa.
Các kiểm thử mô phỏng không thay thế kiểm thử Windows và thiết bị thật.

## Đo so sánh thực tế

Giữ cùng iPhone, PC, mạng, nội dung, độ phân giải/FPS nguồn thực tế và tần số
màn hình. Dùng máy quay khác quay cả iPhone lẫn màn hình PC, ưu tiên 120/240 FPS.
Thu ít nhất 30 sự kiện nhìn thấy được cho mỗi trường hợp. So sánh Cân bằng,
Nhanh nhất và Tùy chỉnh. Mỗi chế độ điền một dòng riêng:

| Kết nối | Cửa sổ | Chế độ | Nguồn thực tế/FPS | Median iPhone→PC | P95 iPhone→PC | P95 thời gian ở hàng đợi | Mất tiếng/underrun | Điều kiện |
|---|---|---|---|---|---|---|---|---|
| Wi-Fi | Preview | | | | | | | |
| Wi-Fi | Output | | | | | | | |
| USB network | Preview | | | | | | | |
| USB network | Output | | | | | | | |

USB ở đây là đường mạng USB đã có trong app. Lặp mỗi chế độ thành dòng riêng.
Đừng dùng số Pipeline Lag trong app thay cho độ trễ quay từ hai màn hình.
Wi-Fi 5 GHz mạnh giúp phần truyền mạng, nhưng không loại bỏ tự động độ trễ
capture/encode, bộ đệm, composition và scanout. FPS/độ phân giải AirPlay là yêu
cầu gửi cho iPhone; thiết bị có thể chọn mức thấp hơn.

Nếu bản này ổn, chạy chế độ muốn dùng ít nhất 30 phút và ghi video/audio drops,
underruns, A/V offset, FPS thật, độ trễ median/P95 trước khi đổi mặc định.

## Prompt bàn giao cho Claude Code trên máy Windows

> Áp dụng Duwn-Mirror-Latency.patch vào repo Duwn-Mirror hiện tại trên nhánh
> riêng. Đọc docs/receiver-latency-controls.md trước khi sửa. Nếu patch xung đột,
> đối chiếu với commit nền 755b4ad957223b95bdb64cbee3eba10b873b6f79 và chuyển
> các thay đổi đúng sang HEAD mới; giữ các thay đổi riêng đang có. Chạy build
> Debug và ctest trên Windows, sửa lỗi phát sinh, báo cáo rõ lỗi cũ/lỗi mới.
> Kiểm tra 4 chế độ Video/Độ trễ hiển thị, nhãn Việt/Anh, cuộn/dropdown ở kích
> thước cửa sổ nhỏ, chuyển chế độ khi đang stream, và persistence sau restart.
> Giữ CurrentSafe; không tắt sync cả audio/video hoặc drop NAL nén tùy tiện.
> Không đổi chất lượng nguồn khi đo A/B. Sau khi tôi cung cấp video/log thiết
> bị thật, phân tích riêng latency iPhone→màn hình và latency bên trong Duwn;
> không kết luận nhanh ngang DouWan từ số T0→T7. Nếu hàng đợi gần 0 mà vẫn trễ,
> lập phép đo UxPlay input/decrypted access unit/RTP output trước khi thay tầng
> nhận bằng native IPC. Không tự bịa số benchmark hoặc coi mô phỏng là chạy thật.
