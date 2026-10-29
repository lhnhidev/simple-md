# simple-md

Trình đọc và sửa Markdown nhỏ gọn cho Windows 10/11 x64, viết bằng C++/Win32.

## Chạy ứng dụng

Mở `simple-md.exe`. Không cần cài đặt hay chạy một server riêng.

- **Open / Ctrl+O:** mở một hoặc nhiều file `.md`, `.markdown`, `.mdown`, `.txt`.
- **Folder:** liệt kê tối đa 2.000 file Markdown ngay trong thư mục đã chọn. Nội dung chỉ được nạp khi chọn file; không quét các thư mục con.
- **New / Ctrl+N:** tạo tài liệu mới.
- **Read / Edit / Split:** đọc, sửa mã Markdown hoặc hiển thị song song. Phím tắt: `Ctrl+1`, `Ctrl+2`, `Ctrl+3`.
- **Save / Ctrl+S:** lưu. **Ctrl+Shift+S:** lưu sang file khác.
- **Ctrl+F / F3:** tìm / tìm tiếp.
- **Ctrl+Tab / Ctrl+Shift+Tab:** chuyển tài liệu.
- **Ctrl+W:** đóng tài liệu. Có sửa chưa lưu sẽ được hỏi trước khi đóng.

Ứng dụng giữ nguyên UTF-8, UTF-8 BOM, UTF-16 LE/BE và kiểu xuống dòng LF/CRLF của file. File dùng encoding khác cần chuyển sang Unicode trước. Việc lưu sử dụng file tạm rồi thay thế file gốc; nếu phát hiện file thay đổi ngoài ứng dụng, bạn được hỏi trước khi ghi đè.

## RAM và giới hạn

Cửa sổ trống chỉ khởi tạo thành phần cần thiết. RichEdit, preview, tìm kiếm, renderer và thư viện hộp thoại được nạp khi dùng. Khi đóng file cuối, editor và renderer được giải phóng.

Cache giữ tối đa 3 tài liệu sạch với ngân sách 8 MiB cho phần nội dung nguồn sạch. Tài liệu đang thao tác và nội dung chưa lưu được giữ để tránh mất dữ liệu. File lớn có lịch sử undo ngắn hơn.

- File tối đa 16 MiB.
- Preview tạm dừng trên 400.000 ký tự; vẫn có thể đọc và sửa toàn bộ bằng Edit.
- Preview hỗ trợ tiêu đề, nhấn mạnh, danh sách, checklist, trích dẫn, code và bảng dạng văn bản.
- Ảnh hiển thị bằng nhãn; không tải ảnh từ mạng. HTML giữ dạng chữ; liên kết chưa mở trực tiếp từ preview.
- Chưa có tự khôi phục nội dung chưa lưu khi ứng dụng hoặc Windows bị tắt đột ngột. Hãy dùng Ctrl+S thường xuyên.

Bài đo tự động trên máy phát triển: cửa sổ trống khoảng **1,05 MiB private working set / 10,42 MiB tổng working set**. Tổng working set gồm cả DLL Windows dùng chung. Mục tiêu **5 MB tổng working set chưa đạt**. Các con số là kết quả một bài đo với cửa sổ ẩn và dữ liệu mẫu, không phải giới hạn cố định cho mọi máy hoặc khi mở nhiều file.

## Kết nối Claude / Codex qua MCP

1. Mở file muốn chia sẻ trong simple-md.
2. Chọn **MCP: Off → Copy connection setup**. Ứng dụng sao chép đoạn cấu hình đúng đường dẫn `.exe` vào clipboard.
3. Dán phần cấu hình tương ứng vào cấu hình MCP của Claude Desktop hoặc Codex. Giữ lại các server đang có và khởi động lại client nếu cần.
4. Trong simple-md, chọn **Read open documents** để cho phép đọc hoặc **Edit open documents (unsaved)** để cho phép sửa.
5. AI có thể liệt kê tài liệu, đọc nội dung trong editor và sửa khi được cấp quyền Edit. Thay đổi từ AI **chưa được lưu xuống đĩa**; bạn xem lại và Ctrl+S.

MCP chỉ truy cập tài liệu đang mở, không có công cụ truy cập file tùy ý hay thực thi lệnh. Mỗi lần mở ứng dụng, MCP trở về **Off**. Chuyển Off để ngắt kết nối; nội dung đã được client đọc trước đó không thể thu hồi bằng thao tác này.

Đường dẫn cho bản build tại project: `D:\workspace\simple-md\dist\simple-md.exe`. Nếu chạy bản portable ở nơi khác, dùng Copy connection setup để lấy đường dẫn mới.

Mã nguồn: https://github.com/lhnhidev/simple-md
