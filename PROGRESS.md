# simple-md — tiến độ

Project: `D:\workspace\simple-md`

- [x] Chọn desktop native C++/Win32 để giảm dung lượng và RAM.
- [x] Chuẩn bị bộ biên dịch, script build và thư viện Markdown/JSON.
- [x] Viết renderer Markdown sang Rich Text, không dùng WebView.
- [x] Viết MCP stdio và cầu nối local, phân quyền Read/Edit.
- [x] Hoàn thiện giao diện mở nhiều tài liệu, đọc/sửa/song song, tìm kiếm.
- [x] Hoàn thiện lưu an toàn, kiểm tra xung đột khi file thay đổi bên ngoài.
- [x] Biên dịch bản chạy: 488.448 bytes (477 KiB).
- [x] Kiểm tra 80 điều kiện về Unicode, encoding, lưu/đọc, undo, quyền MCP, revision, cache và named pipe.
- [x] Kiểm tra MCP stdio: handshake, notifications, tool discovery, ping, lỗi, mặc định tắt.
- [x] Kiểm tra giao diện trống qua Computer Use; đã dừng theo trao đổi với người dùng.
- [x] Viết hướng dẫn build/chạy/kết nối MCP trong README.
- [x] Kiểm tra named pipe: list/read/edit, Read chặn sửa, Off đóng listener, khởi động lại và ngắt client treo.
- [x] Đo và ghi nhận RAM cho 5 trường hợp sử dụng; có script tái chạy và JSON kết quả.
- [ ] Đóng gói bản portable kèm license thư viện.

## Số đo sơ bộ (máy phát triển, 2026-10-29)

- Cửa sổ trống: khoảng 18 MiB working set, 2,8 MiB private memory.
- File mẫu 1.500.000 bytes: mở trong 284 ms trong bài kiểm thử native.
- Benchmark tự động, mỗi trường hợp dùng một tiến trình ẩn mới:
  - Trống: 15,07 MiB working set / 2,39 MiB private committed.
  - Một file nhỏ: 17,55 / 2,74 MiB.
  - Danh sách 30 file, nạp file đầu: 17,58 / 2,77 MiB.
  - Xem trước file 178 KB: 19,15 / 4,29 MiB.
  - File 1,78 MB, dừng preview: 25,06 / 10,55 MiB.
- CPU khi rảnh: ghi nhận 0 ms trong mẫu 1 giây ở cả 5 trường hợp.
- Số liệu đầy đủ: `benchmarks/memory-baseline.json`; cách đo: `tests/measure-memory.ps1`.
- Đây là số đo cho các tình huống cụ thể, không phải giới hạn RAM hay độ trễ cố định.

## Cập nhật MCP

- Giữ một pipe instance suốt phiên bật MCP để không có khoảng trống giành tên pipe.
- Đảm bảo client nhận hết phản hồi trước khi disconnect.
- Hủy được kết nối client không gửi dữ liệu khi chuyển Off.
- Báo lỗi bật kết nối nếu không tạo được listener.

## Cách cập nhật

Commit và push lên `main` sau mỗi phần đã hoàn thành và kiểm tra được, theo yêu cầu người dùng.

## Tối ưu cache

- Giữ tối đa 3 tài liệu sạch đã nạp và áp dụng ngân sách 8 MiB nội dung nguồn sạch.
- Không đẩy tài liệu đang thao tác hoặc có sửa chưa lưu ra khỏi bộ nhớ.
- Giảm lịch sử undo cho tài liệu lớn; bỏ cập nhật danh sách theo từng file khi mở theo lô.
- File bị đẩy khỏi cache rồi thay đổi trên đĩa sẽ tăng revision khi được nạp lại.
- Mục tiêu tiếp theo theo yêu cầu: giảm working set cửa sổ trống về gần 5 MB bằng cách trì hoãn nạp thành phần Windows.

## Tối ưu cửa sổ trống — lượt 1

- Chỉ nạp RichEdit khi mở/tạo tài liệu; chỉ tạo preview, tìm kiếm và luồng render khi được dùng.
- Delay-load thư viện shell, hộp thoại, COM và quyền MCP; không khởi tạo COM khi cửa sổ còn trống.
- Dùng bộ phân tích tham số Unicode của CRT, tránh nạp shell chỉ để đọc command line.
- Working set cửa sổ trống giảm từ 15,07 xuống 10,48 MiB; private committed còn 1,74 MiB.
- Bản chạy 492.032 bytes. Kết quả đầy đủ: `benchmarks/memory-deferred.json`.
- Mục tiêu 5 MB tổng working set chưa đạt; đang phân tích resident pages theo DLL bằng `tests/inspect-memory.ps1`.

MCP mặc định tắt. Khi được bật, chỉ tài liệu đang mở được chia sẻ.
MCP Edit tạo thay đổi chưa lưu để người dùng kiểm tra trước khi Ctrl+S.
