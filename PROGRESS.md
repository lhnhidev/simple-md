# simple-md — tiến độ

Project: `D:\workspace\simple-md`

- [x] Chọn desktop native C++/Win32 để giảm dung lượng và RAM.
- [x] Chuẩn bị bộ biên dịch, script build và thư viện Markdown/JSON.
- [x] Viết renderer Markdown sang Rich Text, không dùng WebView.
- [x] Viết MCP stdio và cầu nối local, phân quyền Read/Edit.
- [x] Hoàn thiện giao diện mở nhiều tài liệu, đọc/sửa/song song, tìm kiếm.
- [x] Hoàn thiện lưu an toàn, kiểm tra xung đột khi file thay đổi bên ngoài.
- [x] Biên dịch bản chạy: 487.424 bytes (476 KiB).
- [x] Kiểm tra 69 điều kiện về Unicode, encoding, lưu/đọc, undo, quyền MCP, revision, cache.
- [x] Kiểm tra MCP stdio: handshake, notifications, tool discovery, ping, lỗi, mặc định tắt.
- [x] Kiểm tra giao diện trống qua Computer Use; đã dừng theo trao đổi với người dùng.
- [x] Viết hướng dẫn build/chạy/kết nối MCP trong README.
- [ ] Kiểm tra toàn tuyến MCP qua named pipe.
- [ ] Đo và ghi nhận RAM cho nhiều trường hợp sử dụng.
- [ ] Đóng gói bản portable kèm license thư viện.

## Số đo sơ bộ (máy phát triển, 2026-10-29)

- Cửa sổ trống: khoảng 18 MiB working set, 2,8 MiB private memory.
- File mẫu 1.500.000 bytes: mở trong 284 ms trong bài kiểm thử native.
- Đây là số đo cho các tình huống cụ thể, không phải giới hạn RAM hay độ trễ cố định.

## Cách cập nhật

Commit và push lên `main` sau mỗi phần đã hoàn thành và kiểm tra được, theo yêu cầu người dùng.

MCP mặc định tắt. Khi được bật, chỉ tài liệu đang mở được chia sẻ.
MCP Edit tạo thay đổi chưa lưu để người dùng kiểm tra trước khi Ctrl+S.
