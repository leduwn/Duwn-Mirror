# Receiver Latency A/B Benchmarking Report & Operational Guide

**Scope:** Đánh giá độ trễ hiển thị đầu nhận (Receiver Latency) của Duwn Mirror trên chuẩn AirPlay Screen Mirroring tích hợp sẵn của iOS (không can thiệp app gửi riêng), so sánh 6 cấu hình hoạt động qua telemetry nội bộ và quy trình đo Glass-to-Glass vật lý.

---

## 1. Ma trận 6 Cấu hình A/B

| Run ID | Chế độ Receiver Latency | Giới hạn hàng đợi sau decode | Chiến lược chọn frame hiển thị | Preview Window |
|---|---|---:|---|---|
| **B_OFF** | **Balanced** (`SmoothLive`) | Tối đa 3 frame | FIFO khi fresh; bắt kịp frame mới nhất khi tuổi frame cũ > 1.25× cadence | Tắt |
| **B_ON** | **Balanced** (`SmoothLive`) | Tối đa 3 frame | FIFO khi fresh; bắt kịp frame mới nhất khi tuổi frame cũ > 1.25× cadence | Bật |
| **F_OFF** | **Fastest** (`LowLatency`) | 1 frame | Luôn lấy newest frame; thay thế ảnh cũ ngay lập tức | Tắt |
| **F_ON** | **Fastest** (`LowLatency`) | 1 frame | Luôn lấy newest frame; thay thế ảnh cũ ngay lập tức | Bật |
| **C_OFF** | **Custom** (`Custom 2F / 25ms`) | 2 frame | Giới hạn tuổi frame 25 ms; nhả newest khi quá hạn | Tắt |
| **C_ON** | **Custom** (`Custom 2F / 25ms`) | 2 frame | Giới hạn tuổi frame 25 ms; nhả newest khi quá hạn | Bật |

### Nguyên tắc kiểm soát thực nghiệm:
1. **Chất lượng nguồn không đổi:** Giữ cùng Receiver Quality (Auto / 1080p60), cùng độ phân giải đầu ra, không bật bộ lọc màu tùy chỉnh.
2. **Điều kiện phần cứng cố định:** Cùng màn hình, cùng tần số quét (display refresh rate Hz), cùng kết nối mạng (Wi-Fi 5 GHz hoặc Apple USB Ethernet).
3. **Nội dung chuyển động lặp lại:** Đồng hồ bấm giờ mili-giây độ chính xác cao hoặc video test pattern có nhịp khung hình ổn định.
4. **Bản chất của ngưỡng Freshness:** Ngưỡng tuổi frame (ví dụ 25 ms ở Custom hoặc 1.25× cadence ở Balanced) là **điều kiện bắt kịp (catchup threshold)** khi pipeline bị tồn đọng, **không phải là thời gian cố ý chờ thêm**. Chế độ Fastest không bắt buộc cộng thêm 16 ms; frame giải mã xong được hiển thị ngay khi GPU sẵn sàng.
5. **Cơ chế đồng bộ cấu hình (SessionMetadataCoordinator):**
   - Hàng đợi sự kiện phiên (`m_pending_events`) và bản sao cấu hình (`m_snapshot`) được **bảo vệ bằng `std::mutex`** (`m_queue_mutex`, `m_snapshot_mutex`, `m_settings_mutex`).
   - Luồng UI sở hữu độc quyền việc thay đổi cài đặt; luồng Telemetry/Metrics đọc bản sao thread-safe qua mutex lock ngắn.
   - Cơ chế này đảm bảo tính đúng đắn và an toàn luồng, không sử dụng `std::atomic` trên toàn bộ cấu trúc dữ liệu và không phải là cấu trúc lock-free.

---

## 2. Tiêu chuẩn Thu nhận Dữ liệu từ Phiên phát Thật

Mỗi cấu hình đo phải tuân thủ nghiêm ngặt quy trình:
1. **Xác nhận cấu hình thực tế trước đo (Pre-check):**
   - Người vận hành điều chỉnh UI đúng với RunId mục tiêu (chế độ stream, tham số Custom 2F/25ms, bật/tắt Preview Window).
   - Script runner kiểm tra trực tiếp telemetry mới nhất trước khi kích hoạt đo: `stream_mode`, policy parameters, `preview_visible`, và cờ `quality_pending == 0`.
   - Tuyệt đối không ghi đè `settings.json` để giả định ứng dụng đang chạy đã áp dụng cấu hình.
2. **Warmup ổn định:** Chờ tối thiểu 30 giây sau khi AirPlay kết nối để bộ đệm jitter mạng, Media Foundation decoder và swapchain DXGI đạt trạng thái ổn định.
3. **Cô lập khoảng đo và Byte Offset:**
   - Ngay sau warmup, runner ghi nhận byte offset hiện tại của file log. Collector chỉ thu nhận các chu kỳ `[METRICS CYCLE BEGIN]` ... `[METRICS CYCLE END]` hoàn chỉnh sinh ra sau offset này.
   - Raw log tương ứng được lưu thành `metrics_raw.log` bên cạnh `metrics.csv`.
   - Mỗi phiên sweep được lưu trong thư mục session có timestamp riêng (`benchmarks/sessions/<SessionId>/<RunId>/`) tránh ghi đè.
4. **Thời gian ghi nhận:** Thu thập dữ liệu liên tục ít nhất **180 giây**.
5. **Điều kiện công nhận phiên phát thật (Validation Criteria):**
   - Tốc độ giải mã (`v_dec_fps`) phải có dữ liệu thực tế (> 0) trong ít nhất 50% chu kỳ.
   - Số khung hình hiển thị thành công (`output_ok`) phải tăng liên tục ($\Delta > 0$).
   - Số lượng mẫu tuổi khung hình (`out_age_count`) phải lớn hơn 0.
   - Cờ cấu hình chờ áp dụng (`quality_pending`) phải bằng **0** trong toàn bộ khoảng đo. Nếu có bất kỳ chu kỳ nào `quality_pending != 0`, run bị đánh dấu `DISQUALIFIED`.
   - Cấu hình thực tế (`stream_mode`, `preview_visible`, policy) phải giữ nguyên vẹn và khớp 100% với mục tiêu của RunId. Mọi sai lệch trong khoảng đo đều khiến run bị loại khỏi kết quả hợp lệ.

---

## 3. Tổng hợp Chỉ số Telemetry Native Pipeline (T0–T7)

> **Lưu ý thống kê:** Dữ liệu percentiles trong bảng dưới đây được tính theo phân phối các cửa sổ 1 giây (`Window-P50 / Window-P95 Distribution`), phản ánh trung bình cộng của các p50/p95 trong từng chu kỳ báo cáo 1 giây của telemetry. Không lấy p95 của các p95 từng giây để gọi là p95 gộp toàn phiên.

| Cấu hình | Codec & Res thực nhận | Decoded FPS | Present FPS | T0–T7 Total (Mean) | Queue Dwell (Mean) | Window-P95 Queue | Output Age (P50) | Output Age (P95) | Video Skip Rate | Audio Drops | Audio Underruns | Trạng thái |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **B_OFF** | — | — | — | — | — | — | — | — | — | — | — | *CHƯA ĐO* |
| **B_ON** | — | — | — | — | — | — | — | — | — | — | — | *CHƯA ĐO* |
| **F_OFF** | — | — | — | — | — | — | — | — | — | — | — | *CHƯA ĐO* |
| **F_ON** | — | — | — | — | — | — | — | — | — | — | — | *CHƯA ĐO* |
| **C_OFF** | — | — | — | — | — | — | — | — | — | — | — | *CHƯA ĐO* |
| **C_ON** | — | — | — | — | — | — | — | — | — | — | — | *CHƯA ĐO* |

### Phân tích chi tiết các mốc thời gian Native (T0–T7 Stages):
Đối chiếu chính xác theo các checkpoint đo đạc trong mã nguồn `VideoFrame.h`, `VideoRenderer.cpp` và `LatencyTelemetry.cpp`:

| Khoảng | Điểm bắt đầu | Điểm kết thúc | Bản chất kỹ thuật |
|---|---|---|---|
| **T0→T1** | `rtp_arrival_qpc` | `au_received_qpc` | RTP nội bộ đến khi lắp ráp Access Unit hoàn chỉnh |
| **T1→T2** | `au_received_qpc` | `process_input_qpc` | Access Unit hoàn chỉnh đến khi nạp vào decoder input (`IMFTransform::ProcessInput`) |
| **T2→T3** | `process_input_qpc` | `process_output_qpc` | Decoder input đến decoder output (`ProcessOutput` xuất mẫu NV12). Đây là thời gian decoder xử lý thực tế, **không phải Demux/NAL** |
| **T3→T4** | `process_output_qpc` | `queue_push_qpc` | Decoder output đến khi đưa vào hàng đợi sau giải mã (`FrameScheduler::Push`) |
| **T4→T5** | `queue_push_qpc` | `queue_pop_qpc` | Thời gian chờ trong hàng đợi (Queue Dwell) đến khi bộ điều phối chọn frame để hiển thị |
| **T5→T6** | `queue_pop_qpc` | `vp_end_qpc` | Chọn frame đến khi `VideoProcessorBlt` trả về. Timestamp CPU quanh Blt chỉ đo thời gian CPU thực thi lệnh API, **không chứng minh GPU đã hoàn tất render** |
| **T6→T7** | `present_begin_qpc` | `present_end_qpc` | Lệnh `IDXGISwapChain::Present` thực thi đến khi hàm trả về. Đo thời gian gọi API CPU, **không chứng minh GPU đã hoàn tất scanout lên màn hình** |

---

## 4. Đo lường Độ trễ Vật lý Glass-to-Glass (Physical Camera)

### 4.1. Phương pháp thực hiện
1. Sử dụng camera ngoài quay tốc độ cao **120 FPS hoặc 240 FPS** đặt ở vị trí cố định quay bao quát cả màn hình iPhone và màn hình PC.
2. Chạy ứng dụng đồng hồ đếm mili-giây chuyển động liên tục trên iPhone.
3. Thu nhận ít nhất **30 sự kiện đối chiếu** cho mỗi cấu hình tại các thời điểm chuyển số rõ nét.
4. Độ trễ Glass-to-Glass:
   $$\text{Latency}_{\text{Glass-to-Glass}} = T_{\text{PC Display}} - T_{\text{iPhone Screen}}$$
5. Trích xuất giá trị Median (p50) và P95 từ tập mẫu 30 sự kiện kèm nguồn provenance (thiết bị, camera FPS).

### 4.2. Bảng kết quả Thực tế

| Cấu hình | Chế độ | Preview | Số mẫu ghi nhận | Median Latency | P95 Latency | Provenance / Rig | Trạng thái đo vật lý |
|---|---|---|---:|---:|---:|---|---|
| **B_OFF** | Balanced | Tắt | 0 | — | — | None | **CHƯA ĐO** |
| **B_ON** | Balanced | Bật | 0 | — | — | None | **CHƯA ĐO** |
| **F_OFF** | Fastest | Tắt | 0 | — | — | None | **CHƯA ĐO** |
| **F_ON** | Fastest | Bật | 0 | — | — | None | **CHƯA ĐO** |
| **C_OFF** | Custom 2F/25ms | Tắt | 0 | — | — | None | **CHƯA ĐO** |
| **C_ON** | Custom 2F/25ms | Bật | 0 | — | — | None | **CHƯA ĐO** |

*Ghi chú minh bạch:* Môi trường phát triển mã nguồn hiện tại không trang bị camera ngoài 120/240 FPS cố định và thiết bị quay đồng thời. Do đó, chỉ số Glass-to-Glass vật lý được ghi nhận chính xác là **CHƯA ĐO**. Không tạo số liệu giả định hoặc suy đoán độ trễ vật lý từ T0–T7.

---

## 5. Giới hạn Phép đo & Đánh giá Hiệu năng

### 5.1. Giới hạn đo lường
1. **Khoảng cách T0–T7 và Glass-to-Glass:**
   - Chỉ số Native T0–T7 chỉ đo thời gian từ khi gói tin đến socket nội bộ của Duwn Mirror đến khi lệnh Present CPU trả về.
   - Nếu Glass-to-Glass thực tế cao trong khi Native T0–T7 thấp, phần trễ chênh lệch nằm ở các khâu bên ngoài phạm vi T0–T7:
     * Các khâu trước T0: chụp màn hình và mã hóa phần cứng trên iOS sender, độ trễ truyền dẫn vô tuyến Wi-Fi, và quá trình giải mã/forwarding của sidecar UxPlay.
     * Các khâu sau T7: GPU rendering, DWM desktop compositing, scanout và độ trễ đáp ứng của tấm nền màn hình hiển thị.
   - **Tuyệt đối không tự ý quy kết phần trễ chênh lệch chủ yếu cho sender iOS, Wi-Fi hay UxPlay khi chưa có số đo quang học cô lập từng chặng.**

2. **Chất lượng nguồn và độ mượt mà:**
   - Việc chuyển đổi giữa Balanced, Fastest và Custom **không làm thay đổi độ phân giải hoặc FPS gốc do iPhone phát**. Nguồn phát AirPlay vẫn giữ nguyên 1080p60 hoặc 720p60.
   - **Fastest:** Chỉ giữ tối đa 1 frame sau decode; nhả frame mới nhất ngay lập tức. Khi nguồn gửi dồn burst frame, frame cũ hơn sẽ bị thay thế (superseded/drop) để ưu tiên tính tức thời.
   - **Balanced:** Giữ hàng đệm tối đa 3 frame, ưu tiên nhịp hiển thị đều (FIFO khi fresh) và chỉ bắt kịp khi tuổi frame vượt 1.25× chu kỳ.
   - **Custom (2 frame / 25 ms):** Thiết lập ngưỡng tuổi frame cố định 25 ms, giải phóng frame mới khi frame cũ bị ứ đọng quá 25 ms.
   - Báo cáo không tuyên bố cấu hình nào là "tối ưu tuyệt đối" hay "0 frame drop" khi chưa có tập dữ liệu thực nghiệm hoàn chỉnh từ phiên phát thật.

---

## 6. Hướng dẫn Vận hành Benchmark Bộ đo A/B (Runbook)

### Bước 1: Khởi động Duwn Mirror
Chạy ứng dụng từ build Release:
```powershell
.\build-msvc\bin\Release\duwn-mirror.exe
```

### Bước 2: Kết nối iPhone và phát video chuyển động liên tục
1. Kết nối iPhone với cùng mạng Wi-Fi 5 GHz hoặc cắm cáp USB (Personal Hotspot).
2. Mở ứng dụng bấm giờ mili-giây độ chính xác cao hoặc phát video test pattern có nhịp khung hình ổn định.
3. Vuốt Control Center trên iPhone, chọn **Screen Mirroring** -> **Duwn Mirror**.
4. Xác nhận cửa sổ trình chiếu hiển thị hình ảnh mượt mà.

### Bước 3: Chạy quy trình Benchmark có xác nhận thực tế
Script sẽ hướng dẫn điều chỉnh GUI cho từng run và tự động kiểm tra telemetry thực tế trước khi bắt đầu đo:

```powershell
# Chạy một cấu hình cụ thể (ví dụ Balanced không Preview):
powershell -ExecutionPolicy Bypass -File .\tools\run-ab-benchmarks.ps1 -RunId B_OFF -WarmupSeconds 30 -DurationSeconds 180 -DeviceModel "iPhone 13 Pro iOS 17.5.1"

# Hoặc chạy toàn bộ ma trận 6 cấu hình:
powershell -ExecutionPolicy Bypass -File .\tools\run-ab-benchmarks.ps1 -RunId All -WarmupSeconds 30 -DurationSeconds 180 -DeviceModel "iPhone 13 Pro iOS 17.5.1"
```

*Quy trình thực thi của mỗi run:*
- Script hiển thị yêu cầu cài đặt UI cho RunId (chế độ, preview, tham số custom).
- Người vận hành điều chỉnh trên giao diện Duwn Mirror và nhấn `[Enter]`.
- Script tiến hành **Pre-check**: đọc trực tiếp telemetry cuối cùng để xác thực `stream_mode`, policy, `preview_visible`, `quality_pending=0` và luồng video đang chuyển động (`v_dec_fps > 0`, `out_age_count > 0`).
- Nếu pre-check thất bại, script báo lỗi cụ thể và chờ người vận hành chỉnh lại.
- Sau khi pre-check thành công, script chạy Warmup 30s, ghi nhận byte offset log, rồi tiến hành đo 180s.
- Dữ liệu CSV và raw log được lưu trữ độc lập theo session (`benchmarks/sessions/<SessionId>/<RunId>/`) và cập nhật bản snapshot tại `benchmarks/runs/<RunId>/`.

### Bước 4: Phân tích kết quả và xuất báo cáo
Sau khi hoàn tất các đợt đo:
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\analyze-latency-ab.ps1 -RunsDir "benchmarks/runs" -OutputMarkdown "benchmarks/analysis_report.md"
```
Báo cáo phân tích Markdown và JSON sẽ được tạo tự động tại thư mục `benchmarks/`.

### Bước 5: Ghi nhận sự kiện quang học Glass-to-Glass (Nếu có Camera)
Nếu có camera ngoài 120/240 FPS:
1. Quay đồng thời màn hình iPhone và PC trong suốt đợt đo.
2. Trích xuất thời điểm chuyển số (ít nhất 30 sự kiện) và lưu vào file `benchmarks/runs/<RunId>/glass_events.csv` với định dạng:
   ```csv
   event_id,iphone_time_ms,pc_time_ms,latency_ms,camera_fps
   1,12345.6,12398.2,52.6,240
   ...
   ```
3. Chạy lại `analyze-latency-ab.ps1` để tự động tích hợp số liệu quang học vào báo cáo.