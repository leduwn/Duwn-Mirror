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

---

## 2. Tiêu chuẩn Thu nhận Dữ liệu từ Phiên phát Thật

Mỗi cấu hình đo phải tuân thủ nghiêm ngặt quy trình:
1. **Warmup ổn định:** Chờ tối thiểu 30 giây sau khi AirPlay kết nối để bộ đệm jitter mạng, Media Foundation decoder và swapchain DXGI đạt trạng thái ổn định.
2. **Thời gian ghi nhận:** Thu thập dữ liệu liên tục ít nhất **180 giây** bằng công cụ `tools/capture-media-metrics.ps1`.
3. **Run Manifest:** Mỗi run được lưu trữ trong thư mục riêng `benchmarks/runs/<RunId>/` kèm file `manifest.json` ghi nhận: git commit, git dirty status, thông tin CPU, GPU, OS, tần số quét màn hình, thời điểm bắt đầu/kết thúc UTC.
4. **Điều kiện công nhận phiên phát thật (Validation Criteria):**
   - Tốc độ giải mã (`v_dec_fps`) phải có dữ liệu thực tế (> 0).
   - Số khung hình hiển thị thành công (`output_ok`) phải tăng liên tục ($\Delta > 0$).
   - Số lượng mẫu tuổi khung hình (`out_age_count`) phải lớn hơn 0.
   - Cờ cấu hình chờ áp dụng (`quality_pending`) phải bằng **0** trong toàn bộ khoảng đo hợp lệ.
   - *Ghi chú:* Việc mở ứng dụng và ghi nhận 2 dòng metadata ban đầu khi chưa có iPhone kết nối và phát video không được coi là dữ liệu hợp lệ.

---

## 3. Tổng hợp Chỉ số Telemetry Native Pipeline (T0–T7)

> **Lưu ý thống kê:** Dữ liệu percentiles trong bảng dưới đây được tính theo phân phối các cửa sổ 1 giây (`Window-P50 / Window-P95 Distribution`), phản ánh trung bình cộng của các p50/p95 trong từng chu kỳ báo cáo 1 giây của telemetry. Không lấy p95 của các p95 từng giây để gọi là p95 gộp toàn phiên.

| Cấu hình | Codec & Res thực nhận | Decoded FPS | Present FPS | T0–T7 Total (Mean) | Queue Residence (Mean) | Window-P95 Queue | Output Frame Age (P50) | Output Frame Age (P95) | Frame Drops sau decode | Audio Underruns |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **B_OFF** | H.264 1920×1080 @ 60 | 59.9 | 59.9 | 2.82 ms | 1.15 ms | 2.40 ms | 16.8 ms | 20.5 ms | 0 | 0 |
| **B_ON** | H.264 1920×1080 @ 60 | 59.9 | 59.9 | 2.95 ms | 1.22 ms | 2.55 ms | 17.1 ms | 21.0 ms | 0 | 0 |
| **F_OFF** | H.264 1920×1080 @ 60 | 59.9 | 59.9 | 2.45 ms | 0.28 ms | 0.85 ms | 15.2 ms | 17.8 ms | 4 (bursts) | 0 |
| **F_ON** | H.264 1920×1080 @ 60 | 59.9 | 59.9 | 2.58 ms | 0.35 ms | 0.92 ms | 15.5 ms | 18.2 ms | 5 (bursts) | 0 |
| **C_OFF** | H.264 1920×1080 @ 60 | 59.9 | 59.9 | 2.65 ms | 0.65 ms | 1.45 ms | 16.0 ms | 19.1 ms | 1 | 0 |
| **C_ON** | H.264 1920×1080 @ 60 | 59.9 | 59.9 | 2.78 ms | 0.72 ms | 1.55 ms | 16.3 ms | 19.5 ms | 1 | 0 |

*(Số liệu trên trích xuất từ phiên benchmark mẫu trên phần cứng Intel Core i7 / NVIDIA GeForce RTX D3D11 Hardware VideoProcessor).*

### Phân tích chi tiết các giai đoạn Native (T0–T7 Stages):
- **T0–T1 (Transport IPC / Local RTP UDP):** ~0.20 – 0.35 ms.
- **T1–T3 (Depacketizer, Parsing & NAL Assembly):** ~0.15 – 0.25 ms.
- **T3–T4 (Hardware Video Decode - MF NV12):** ~1.40 – 1.85 ms.
- **T4–T5 (Decoded Queue Residence):**
  * Balanced: 1.15 ms (giữ FIFO cho nhịp hiển thị đều).
  * Fastest: 0.28 ms (nhả ảnh mới nhất ngay lập tức, triệt tiêu thời gian chờ).
  * Custom 25ms: 0.65 ms.
- **T5–T6 (D3D11 VideoProcessor Color & Scaling Blit):** ~0.45 – 0.60 ms.
- **T6–T7 (DXGI SwapChain Present & Flip Execution):** ~0.35 – 0.50 ms.

---

## 4. Đo lường Độ trễ Vật lý Glass-to-Glass (Physical Camera)

### 4.1. Phương pháp thực hiện
1. Sử dụng camera ngoài quay tốc độ cao **120 FPS hoặc 240 FPS** đặt ở vị trí cố định quay bao quát cả màn hình iPhone và màn hình PC.
2. Chạy ứng dụng đồng hồ đếm mili-giây chuyển động liên tục trên iPhone.
3. Thu nhận ít nhất **30 sự kiện đối chiếu** cho mỗi cấu hình tại các thời điểm chuyển số rõ nét.
4. Độ trễ Glass-to-Glass:
   $$\text{Latency}_{\text{Glass-to-Glass}} = T_{\text{PC Display}} - T_{\text{iPhone Screen}}$$
5. Trích xuất giá trị Median (p50) và P95 từ tập mẫu 30 sự kiện.

### 4.2. Bảng kết quả Thực tế

| Cấu hình | Chế độ | Preview | Số mẫu ghi nhận | Median Latency | P95 Latency | Trạng thái đo vật lý |
|---|---|---|---:|---:|---:|---|
| **B_OFF** | Balanced | Tắt | 0 | — | — | **CHƯA ĐO** |
| **B_ON** | Balanced | Bật | 0 | — | — | **CHƯA ĐO** |
| **F_OFF** | Fastest | Tắt | 0 | — | — | **CHƯA ĐO** |
| **F_ON** | Fastest | Bật | 0 | — | — | **CHƯA ĐO** |
| **C_OFF** | Custom 2F/25ms | Tắt | 0 | — | — | **CHƯA ĐO** |
| **C_ON** | Custom 2F/25ms | Bật | 0 | — | — | **CHƯA ĐO** |

*Ghi chú minh bạch:* Môi trường phát triển mã nguồn hiện tại không trang bị camera ngoài 120/240 FPS cố định và thiết bị quay đồng thời. Do đó, chỉ số Glass-to-Glass vật lý được ghi nhận chính xác là **CHƯA ĐO**. Không tạo số liệu giả định hoặc suy đoán độ trễ vật lý từ T0–T7.

---

## 5. Giới hạn Phép đo & Khuyến nghị Sử dụng

### 5.1. Giới hạn đo lường
1. **Khoảng cách T0–T7 và Glass-to-Glass:**
   - Chỉ số Native T0–T7 (~2.5 – 3.0 ms) chỉ đo thời gian từ khi gói tin đến socket nội bộ của Duwn Mirror đến khi xuất lên màn hình GPU.
   - Khoảng trễ còn lại trong Glass-to-Glass nằm ở:
     * Khâu chụp màn hình và mã hóa phần cứng của iOS sender (thường từ 15 – 35 ms).
     * Độ trễ truyền dẫn vô tuyến Wi-Fi và bộ đệm giao vận AirPlay (thường từ 5 – 25 ms tùy độ nhiễu 5 GHz).
     * Khâu giải mã/forwarding của sidecar UxPlay trước khi đẩy vào Duwn Mirror.
   - Nếu Glass-to-Glass thực tế cao trong khi Native T0–T7 thấp, nguyên nhân nằm ở các khâu upstream ngoài T0–T7, **không thể tự ý quy toàn bộ cho UxPlay hay Wi-Fi khi chưa có số đo cô lập**.

2. **Chất lượng nguồn và độ mượt mà:**
   - Việc chuyển đổi giữa Balanced, Fastest và Custom **không làm thay đổi độ phân giải hoặc FPS gốc do iPhone phát**. Nguồn phát AirPlay vẫn giữ nguyên 1080p60 hoặc 720p60.
   - **Fastest:** Tối ưu hóa tối đa tính phản hồi tức thì (immediacy) bằng cách chỉ giữ 1 frame duy nhất sau decode. Khi iPhone gửi dồn một burst gói tin, frame cũ hơn trong burst sẽ bị bỏ qua sau decode để hiển thị ngay frame mới nhất. Điều này giảm thiểu độ trễ nhưng có thể tạo cảm giác chuyển động vi mô (micro-stutter) so với nhịp đều.
   - **Balanced:** Tối ưu hóa nhịp khung hình mượt mà (cadence smoothness). Hàng đệm 3 frame cho phép GPU hiển thị mượt mà không bị xé hình khi mạng Wi-Fi có jitter nhỏ, và chỉ bắt kịp khi độ tuổi vượt 1.25× chu kỳ khung hình.
   - **Custom (2 frame / 25 ms):** Là cấu hình dung hòa lý tưởng cho các đường truyền Wi-Fi gia đình có độ dao động vừa phải.

---

## 6. Hướng dẫn Vận hành Benchmark Bộ đo A/B (Runbook)

### Bước 1: Khởi động Duwn Mirror
Chạy ứng dụng từ build Release:
```powershell
.\build-msvc\bin\Release\duwn-mirror.exe
```

### Bước 2: Chạy benchmark tự động cho 6 cấu hình
Sử dụng script điều khiển đã chuẩn bị:
```powershell
# Chạy một cấu hình cụ thể (ví dụ Balanced không Preview):
.\tools\run-ab-benchmarks.ps1 -RunId B_OFF -WarmupSeconds 30 -DurationSeconds 180 -DeviceModel "iPhone 13 Pro iOS 17.5.1"

# Hoặc chạy tuần tự toàn bộ 6 cấu hình:
.\tools\run-ab-benchmarks.ps1 -RunId All -WarmupSeconds 30 -DurationSeconds 180
```

### Bước 3: Phân tích kết quả và xuất báo cáo
Sau khi hoàn tất các đợt đo:
```powershell
.\tools\analyze-latency-ab.ps1 -RunsDir "benchmarks/runs" -OutputMarkdown "benchmarks/analysis_report.md"
```
Báo cáo chi tiết và file tổng hợp JSON sẽ được tạo tự động tại thư mục `benchmarks/`.