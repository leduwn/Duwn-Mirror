# Duwn Mirror — A/B Receiver Latency Benchmark Report

Date: 2026-10-01 09:23:05 UTC
Scope: Built-in iOS AirPlay Screen Mirroring receiver pipeline latency (T0–T7) and physical Glass-to-Glass assessment.

---

## 1. Test Configurations Matrix

| Run ID | Mode | Queue Capacity | Threshold Strategy | Preview Window | Status |
|---|---|---:|---|---|---|
| **B_OFF** | Balanced (SmoothLive) | Up to 3 frames | Fresh FIFO (1.25× cadence catchup) | OFF | NOT_RUN |
| **B_ON**  | Balanced (SmoothLive) | Up to 3 frames | Fresh FIFO (1.25× cadence catchup) | ON  | NOT_RUN |
| **F_OFF** | Fastest (LowLatency) | 1 frame | Always newest decoded frame | OFF | NOT_RUN |
| **F_ON**  | Fastest (LowLatency) | 1 frame | Always newest decoded frame | ON  | NOT_RUN |
| **C_OFF** | Custom (Custom) | 2 frames | 25 ms freshness threshold | OFF | NOT_RUN |
| **C_ON**  | Custom (Custom) | 2 frames | 25 ms freshness threshold | ON  | NOT_RUN |

---

## 2. Telemetry Metrics Summary (Native Pipeline T0–T7)

> **Lưu ý thống kê:**
> - **Mean của Window-P95/P50**: Các giá trị p50/p95 trong bảng là trung bình cộng của các p50/p95 đo trong từng cửa sổ 1 giây (Window-P50 / Window-P95 Distribution), không phải percentile gộp của toàn bộ frame trong phiên.
> - **T0–T7 Total Mean**: Trung bình cộng của rolling average T0–T7 qua các chu kỳ đo.
> - **Video Skip Rate**: Tỷ lệ khung hình bị bỏ qua / thay thế sau decode trên tổng số khung hình gửi tới Present (out_skip / (out_ok + out_skip)).

| Run ID | Actual Stream | Decoded FPS | Present FPS | Native T0–T7 Total (Mean) | Queue Dwell (Mean) | Window-P95 Queue | Out Age (P50) | Out Age (P95) | Video Skip Rate | Audio Drops | Audio Underruns | Status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **B_OFF** | *NOT_RUN* | — | — | — | — | — | — | — | — | — | — | *NOT_RUN* |
| **B_ON** | *NOT_RUN* | — | — | — | — | — | — | — | — | — | — | *NOT_RUN* |
| **F_OFF** | *NOT_RUN* | — | — | — | — | — | — | — | — | — | — | *NOT_RUN* |
| **F_ON** | *NOT_RUN* | — | — | — | — | — | — | — | — | — | — | *NOT_RUN* |
| **C_OFF** | *NOT_RUN* | — | — | — | — | — | — | — | — | — | — | *NOT_RUN* |
| **C_ON** | *NOT_RUN* | — | — | — | — | — | — | — | — | — | — | *NOT_RUN* |

---

## 3. Pipeline Stages Breakdown (T0–T7 Detailed Stages)

> **Ghi chú kỹ thuật về các mốc thời gian:**
> - **T0→T1**: RTP nội bộ đến khi lắp ráp Access Unit hoàn chỉnh.
> - **T1→T2**: Access Unit hoàn chỉnh đến khi nạp vào decoder input (MFT ProcessInput).
> - **T2→T3**: Decoder input đến khi decoder output xuất mẫu ảnh (MFT ProcessOutput). Đây là thời gian decoder xử lý thực tế, không phải Demux/NAL.
> - **T3→T4**: Decoder output đến khi đưa vào hàng đợi sau decode.
> - **T4→T5**: Chờ trong hàng đợi sau decode (Queue Dwell) đến khi bộ điều phối chọn frame.
> - **T5→T6**: Chọn frame đến khi VideoProcessorBlt trả về. Timestamp CPU quanh Blt đo thời gian gọi API CPU, không chứng minh GPU đã hoàn tất render.
> - **T6→T7**: Đến khi Present trả về. Timestamp CPU quanh Present đo thời gian gọi API CPU, không chứng minh GPU đã hoàn tất scanout lên màn hình.

| Run ID | T0–T1 (RTP to AU) | T1–T2 (AU to Dec In) | T2–T3 (Dec In to Out) | T3–T4 (Dec Out to Queue) | T4–T5 (Queue Dwell) | T5–T6 (Blt Ret) | T6–T7 (Present Ret) | Native T0–T7 Total (Mean) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **B_OFF** | — | — | — | — | — | — | — | — |
| **B_ON** | — | — | — | — | — | — | — | — |
| **F_OFF** | — | — | — | — | — | — | — | — |
| **F_ON** | — | — | — | — | — | — | — | — |
| **C_OFF** | — | — | — | — | — | — | — | — |
| **C_ON** | — | — | — | — | — | — | — | — |

---

## 4. Physical Glass-to-Glass Latency (External Camera 120/240 FPS)

| Run ID | Mode | Preview | Measured Events | Median Latency | P95 Latency | Provenance / Rig | Measurement Status |
|---|---|---|---:|---:|---:|---|---|
| **B_OFF** | — | OFF | 0 | — | — | None | **CHƯA ĐO** |
| **B_ON** | — | ON | 0 | — | — | None | **CHƯA ĐO** |
| **F_OFF** | — | OFF | 0 | — | — | None | **CHƯA ĐO** |
| **F_ON** | — | ON | 0 | — | — | None | **CHƯA ĐO** |
| **C_OFF** | — | OFF | 0 | — | — | None | **CHƯA ĐO** |
| **C_ON** | — | ON | 0 | — | — | None | **CHƯA ĐO** |

---

## 5. Phân tích kết quả và Khuyến nghị sử dụng

### 5.1. Đánh giá trạng thái thực nghiệm
- **Chưa có phiên phát thực tế hợp lệ**: Toàn bộ các cấu hình đo đang ở trạng thái chưa hoàn tất benchmark (NOT_RUN, NO_VALID_STREAM hoặc INVALID).
- **Không xếp hạng hay chọn chế độ tối ưu**: Báo cáo từ chối đưa ra kết luận so sánh hiệu năng hoặc chọn chế độ thắng cuộc khi chưa có dữ liệu telemetry hợp lệ từ phiên phát AirPlay thực tế.
- **Chỉ số Glass-to-Glass**: Ghi nhận trạng thái **CHƯA ĐO** do chưa có hệ thống camera ngoài 120/240 FPS ghi hình quang học đối chiếu.
