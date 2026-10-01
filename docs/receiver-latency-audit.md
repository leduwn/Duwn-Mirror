# Receiver Latency Audit & Synchronization Hardening

Scope: Báo cáo kiểm tra đồng bộ luồng, chuẩn hóa số đo độ trễ và quy trình đánh giá A/B trên Duwn Mirror (AirPlay Screen Mirroring chuẩn cho iPhone, không cài app gửi riêng).

---

## 1. Thread Ownership & Đồng bộ trong FrameScheduler

Sau khi tích hợp `StreamingPolicy` (atomic snapshot 4-byte), audit toàn diện phát hiện việc atomic hóa cấu hình policy là chưa đủ để triệt tiêu data race trên hot-path của `FrameScheduler`.

### 1.1. Bảng phân định quyền sở hữu luồng (Thread Ownership)

| Thành phần / Trường | Luồng sở hữu (Writer) | Luồng đọc (Reader) | Cơ chế đồng bộ hóa |
|---|---|---|---|
| `m_decoded_queue` | Decode Thread (`PushFrame`) | Render Thread (`PopLatestValidFrame`) | `std::mutex m_decoded_queue_mutex`. **Không bao giờ giữ mutex khi gọi Present() hoặc WaitForSingleObject**. |
| `m_observed_display_interval_ms` | Render Thread | Decode Thread (`PushFrame`) | `std::atomic<double>` với relaxed memory order. |
| `m_active_generation` | Decode Thread / UI | Render Thread | `std::atomic<uint64_t>`. Chuyển generation purge sạch queue cũ. |
| `m_last_presented_sequence` | Render Thread | Render Thread | `std::atomic<uint64_t>`. |
| `m_prev_pts_ns` | Render Thread | Render Thread | `std::atomic<int64_t>`. |
| `m_cadence_delta_history`, `m_last_source_pts_ns` | Decode Thread | Decode Thread / Test | `std::mutex m_cadence_mutex`. |
| `m_pts_delta_samples` | Decode Thread | Render Thread | `std::mutex m_cadence_mutex`. Render thread hoán đổi (`std::swap`) sang vector cục bộ mỗi 1s, tính thống kê ngoài vùng khóa. |
| `m_render_reset_requested` | UI Thread (`Flush`) | Render Thread | `std::atomic<bool>`. Render thread nhận cờ và tự reset các sample vector nội bộ tại đầu vòng lặp. |
| Diagnostic vectors (`m_dxgi_ready_samples`, `m_dxgi_wait_samples`, v.v.) | Render Thread | Render Thread | Độc quyền Render Thread; không còn race khi UI gọi `Flush()`. |

### 1.2. Các data race đã xác nhận và phương án khắc phục

1. **Race trên `m_pts_delta_samples`**:
   - *Hiện tượng*: Decode thread gọi `m_pts_delta_samples.push_back()` trong khi Render thread gọi `CalcDiagnosticsStats(m_pts_delta_samples, ...)` và `m_pts_delta_samples.clear()`. Gây data race và nguy cơ crash heap do realloc đồng thời.
   - *Khắc phục*: Bảo vệ bằng `m_cadence_mutex`. Render thread dùng `std::swap` lấy toàn bộ mẫu ra vector cục bộ trong phạm vi khóa cực ngắn (<1 µs), sau đó tính percentiles ngoài khóa.
2. **Race trên diagnostic vectors khi UI gọi `Flush()`**:
   - *Hiện tượng*: UI thread gọi `Flush()` trực tiếp xóa `clear()` các vector chẩn đoán của Render thread.
   - *Khắc phục*: Thay thế việc trực tiếp `clear()` bằng cờ atomic `m_render_reset_requested = true`. Render thread tự kiểm tra và dọn dẹp ở đầu chu kỳ render loop.
3. **Race trên `m_observed_display_interval_ms`**:
   - *Hiện tượng*: Render thread cập nhật EMA display interval trong khi Decode thread đọc trong `PushFrame()` để tính staleness threshold.
   - *Khắc phục*: Chuyển thành `std::atomic<double>`.

---

## 2. Chuẩn hóa Telemetry & Tách biệt Preview / Output

### 2.1. Cô lập Output và Preview

Trước đây, khi mở cửa sổ phụ (Preview):
- `VideoRenderer::Present` dùng chung `GlobalMetrics().vp_duration_avg_ms`, `present_duration_avg_ms`, `total_pipeline_avg_ms`. Frame preview cập nhật sau Output ghi đè các chỉ số chung, làm sai lệch chỉ số màn hình chính.
- `frame` được truyền vào `m_preview_renderer->Present(frame, true)` làm ghi đè các timestamp QPC (`vp_begin_qpc`, `vp_end_qpc`, `present_begin_qpc`, `present_end_qpc`) của frame chính.
- `RecordOutputFrameAge` được gọi bất kể `Present()` thành công, bị skip hay lỗi.

**Các cải tiến đã áp dụng:**
1. **Tách biệt chỉ số**:
   - Primary Output ghi vào `video_present_attempts`, `video_present_ok`, `video_present_skipped`, `video_present_errors`.
   - Preview Window ghi vào `preview_present_attempts`, `preview_present_ok`, `preview_present_skipped`, `preview_present_errors`, `preview_vp_duration_avg_ms`, `preview_present_duration_avg_ms`.
2. **Bảo vệ timestamp**: Non-blocking preview đo đạc thời gian bằng biến cục bộ trên stack, không ghi đè vào `VideoFrame`.
3. **Phân loại PresentResult**:
   - `Attempt`: Tổng số lần gọi `Present()`.
   - `Ok`: Frame swap chain trình bày thành công.
   - `Skipped`: Bị bỏ qua do `DXGI_ERROR_WAS_STILL_DRAWING`, cạnh tranh khóa không chặn (`try_lock`), hoặc cửa sổ bị ẩn/thu nhỏ. Frame bị skip không được coi là đã hiển thị và không ghi nhận frame age.
   - `Error`: `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, hoặc lỗi tạo resource/blit nghiêm trọng.

### 2.2. Thống kê Stage Percentiles theo mẫu thực (Sample-based Percentiles)

Percentile (`p50`, `p95`) cho 5 chặng xử lý cốt lõi được gom từ các mẫu đo thực tế trong cửa sổ 1 giây, sắp xếp (`std::sort`) để lấy giá trị phân vị thực, thay vì lấy percentile của các giá trị trung bình từng giây:

1. **Decode Time**: Đo bằng Decode thread trong `VideoDecoder::FeedAccessUnit` (`video_decode_time_ms`, `video_decode_p50_ms`, `video_decode_p95_ms`, `video_decode_sample_count`).
2. **Queue Residence**: Đo bằng Render thread khi lấy frame khỏi hàng đợi (`queue_residence_avg_ms`, `queue_residence_p50_ms`, `queue_residence_p95_ms`, `queue_residence_max_ms`, `queue_residence_sample_count`).
3. **DXGI Wait**: Đo thời gian thực sự chờ `WaitForSingleObject` trên frame latency waitable handle (`dxgi_wait_avg_ms`, `dxgi_wait_p50_ms`, `dxgi_wait_p95_ms`, `dxgi_wait_max_ms`, `dxgi_wait_sample_count`).
4. **VideoProcessor Blit**: Đo thời gian GPU VideoProcessor thực hiện CSC/Blt (`vp_duration_avg_ms`, `vp_duration_p50_ms`, `vp_duration_p95_ms`, `vp_duration_max_ms`, `vp_sample_count`).
5. **Present Duration**: Đo thời gian hàm `IDXGISwapChain1::Present1` thực thi (`present_duration_avg_ms`, `present_duration_p50_ms`, `present_duration_p95_ms`, `present_duration_max_ms`, `present_sample_count`).

---

## 3. Định nghĩa các chỉ số đo và Giới hạn

### 3.1. Độ trễ hiển thị Native Receiver (T0–T7)

- **Điểm bắt đầu T0**: Thời điểm gói tin RTP video đầu tiên của một Access Unit cập bến socket loopback nội bộ của Duwn Mirror (sau khi qua UxPlay).
- **Điểm kết thúc T7**: Thời điểm lệnh `IDXGISwapChain1::Present1` trả về trên Render thread.
- **Giá trị thông thường**: ~2.5 ms – 4.5 ms trên GPU D3D11 phần cứng.
- **Giới hạn quan trọng**:
  - T0–T7 **CHỈ ĐO ĐỘ TRỄ NỘI BỘ BỘ NHẬN (NATIVE RECEIVER LATENCY)**.
  - T0–T7 **KHÔNG PHẢI LÀ ĐỘ TRỄ GLASS-TO-GLASS**.
  - Các chặng trước T0 (Pre-T0: màn hình iPhone chụp frame, phần cứng Apple VideoToolbox nén H.264/HEVC, truyền qua Wi-Fi / USB, và depacketization trong UxPlay) chiếm khoảng 35–55 ms và nằm ngoài tầm quan sát của T0–T7.
  - Các chặng sau T7 (DWM compositing, VSync flip, và thời gian quét điểm ảnh của panel màn hình PC) chiếm thêm 8–16 ms.

---

## 4. Công cụ thu thập số liệu: tools/capture-media-metrics.ps1

Collector đã được tái cấu trúc hoàn chỉnh:
- **Đọc không khóa**: Dùng `[System.IO.FileStream]` với `FileShare::ReadWrite` qua con trỏ byte offset (`Seek`), không chặn file log của ứng dụng.
- **Khử trùng lặp (Dedup)**: Chỉ ghi dòng CSV khi có dữ liệu mới phát sinh; loại bỏ tình trạng ghi lặp lại 1800 dòng giống nhau khi log tạm dừng.
- **Độc lập Audio và Video**: Thu thập đầy đủ số liệu video ngay cả khi không có audio hoặc audio bị tắt; đánh dấu cờ `audio_stale` nếu audio quá 3 giây không cập nhật.
- **Đầy đủ Metadata & Stage Percentiles**: Ghi nhận `run_id`, `commit`, `transport`, `stream_mode`, cấu hình policy, thông số thực nhận (`codec`, `res`, `fps`), phân loại `output`/`preview` present, và p50/p95 của cả 5 stage.
- **Nhận diện Reconnect**: Đánh dấu cờ `reconnect_event` khi `format_generation` thay đổi hoặc trạng thái kết nối chuyển từ ngắt sang kết nối.

### Cách chạy Collector:
```powershell
.\tools\capture-media-metrics.ps1 -OutputPath "metrics-run1.csv" -DurationSeconds 1800
```

---

## 5. Quy trình A/B Testing & Đánh giá thực nghiệm

### 5.1. Cấu hình so sánh

| Cấu hình | Chế độ Receiver Latency | Giới hạn hàng đợi | Tiêu chí hiển thị | Cấu hình nguồn yêu cầu |
|---|---|---|---|---|
| **A (Balanced)** | `StreamingMode::SmoothLive` | Tối đa 3 frame | FIFO khi fresh; nhả newest khi frame cũ vượt 1.25× cadence | 1080p60 Auto |
| **B (Fastest)** | `StreamingMode::LowLatency` | 1 frame | Luôn lấy newest frame, bỏ frame cũ trong hàng đợi | 1080p60 Auto |
| **C (Custom)** | `StreamingMode::Custom` | 2 frame / 25 ms | Giới hạn tuổi frame 25 ms | 1080p60 Auto |

### 5.2. Môi trường kết nối
1. **Wi-Fi 5 GHz**: iPhone kết nối cùng băng tần 5 GHz với router PC (lưu ý: Wi-Fi 5 GHz khác với mạng di động 5G).
2. **Apple USB Ethernet**: Kết nối cáp Lightning/Type-C có hỗ trợ Apple USB Ethernet adapter để loại bỏ jitter vô tuyến.

### 5.3. Phương pháp đo Glass-to-Glass vật lý

1. Chạy đồng hồ bấm giờ mili-giây có độ chính xác cao (ví dụ web-based millisecond timer) trên iPhone.
2. Dùng camera ngoài quay tốc độ cao (120 FPS hoặc 240 FPS) đặt ở vị trí cố định quay đồng thời cả màn hình iPhone và màn hình PC.
3. Thu ít nhất 30 sự kiện đối chiếu (frame đóng băng hoặc chuyển số) cho mỗi cấu hình.
4. Tính chênh lệch thời gian giữa iPhone và PC:
   $$\text{Glass-to-Glass Latency} = T_{\text{PC Display}} - T_{\text{iPhone Display}}$$
5. Trích xuất median (p50) và p95 glass-to-glass thực tế.
6. Chạy phiên liên tục 30 phút để theo dõi drop rate, hàng đợi, underrun âm thanh và tính ổn định.

### 5.4. Trạng thái kết quả đo vật lý hiện tại

- **Đo vật lý Glass-to-Glass bằng camera ngoài**: `CHƯA ĐO` (Môi trường phát triển hiện tại không gắn camera 120/240 FPS và thiết bị quay đồng thời).
- **Tuyệt đối không tạo số liệu giả định** hoặc suy đoán độ trễ vật lý từ T0–T7.
- **Không tuyên bố nhanh ngang ApowerMirror/DouWan** khi chưa có dữ liệu đối chiếu từ cùng một camera trên cùng thiết bị thực tế.

---

## 6. Giữ các giới hạn an toàn đã kiểm chứng

1. **Giữ CurrentSafe**: Duy trì `-vsync yes` và `sync=true` trên forwarding sink của UxPlay. Báo cáo Phase 5 đã chứng minh việc tắt sync gây tràn bộ đệm video và vỡ đồng bộ âm thanh (underrun kéo dài 3.35s).
2. **Không drop NAL nén trước decode**: Mọi frame nén H.264/HEVC đều được đưa qua decoder nguyên vẹn để bảo toàn cấu trúc GOP và frame tham chiếu. Việc drop frame chỉ thực hiện trên frame uncompressed NV12 sau decode trong `PopLatestValidFrame`.
3. **Không tự hạ độ phân giải hoặc FPS**: Chuyển đổi latency mode chỉ thay đổi chiến lược lưu đệm và nhả frame ở đầu ra, giữ nguyên độ phân giải nguồn và FPS gốc từ iPhone.
4. **DXGI Waitable Handle**: Tiếp tục duy trì `SetMaximumFrameLatency(1)` và `WaitForSingleObjectEx` trong renderer để bảo đảm nhịp độ GPU mượt mà, không khóa hàng đợi decode.
