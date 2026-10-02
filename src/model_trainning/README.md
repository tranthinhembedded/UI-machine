# Mô hình phát hiện khuyết tật vải bằng YOLOv8m

Thư mục này lưu lại kết quả huấn luyện và thử nghiệm mô hình **YOLOv8m** cho bài toán phát hiện khuyết tật trên bề mặt vải. Bộ dữ liệu trong kết quả đánh giá gồm hai nhãn:

- `hole`: lỗ thủng trên vải.
- `yarn defect`: lỗi liên quan đến sợi vải.

## 1. Kết quả huấn luyện

![Kết quả huấn luyện YOLOv8m](images/trainning.jpg)

Ảnh `trainning.jpg` chụp màn hình khi quá trình huấn luyện hoàn thành **150 epoch**. Thời gian huấn luyện được ghi nhận là khoảng **6,128 giờ**. Mô hình sau huấn luyện có **92 lớp**, khoảng **25,84 triệu tham số** và yêu cầu **78,7 GFLOPs**.

Kết quả đánh giá từ ảnh:

| Lớp | Số ảnh | Số đối tượng | Precision | Recall | mAP@50 | mAP@50–95 |
|---|---:|---:|---:|---:|---:|---:|
| Tất cả (`all`) | 1.632 | 2.312 | 0,907 | 0,836 | 0,868 | 0,543 |
| `hole` | 550 | 853 | 0,885 | 0,790 | 0,822 | 0,475 |
| `yarn defect` | 1.124 | 1.459 | 0,928 | 0,881 | 0,914 | 0,611 |

Ý nghĩa các chỉ số:

- **Precision**: tỷ lệ dự đoán khuyết tật đúng trong tổng số dự đoán của mô hình.
- **Recall**: khả năng tìm được các khuyết tật thực sự có trong ảnh.
- **mAP@50**: độ chính xác trung bình khi ngưỡng IoU bằng 0,5.
- **mAP@50–95**: độ chính xác trung bình trên nhiều ngưỡng IoU từ 0,5 đến 0,95; đây là tiêu chí khắt khe hơn.

Kết quả cho thấy mô hình nhận diện lớp `yarn defect` tốt hơn lớp `hole`. Chỉ số tổng thể **mAP@50 = 0,868** cho thấy mô hình đã học được tương đối tốt đặc trưng của hai loại khuyết tật, trong khi **mAP@50–95 = 0,543** cho thấy vị trí hoặc kích thước bounding box vẫn còn khả năng cải thiện ở các yêu cầu IoU cao.

## 2. Kết quả phát hiện lỗ thủng

![Kết quả phát hiện lỗ thủng](images/detected.jpg)

Ảnh `detected.jpg` minh họa kết quả suy luận của mô hình trên một mẫu vải. Mô hình phát hiện **6 vùng khuyết tật** thuộc lớp `hole` ở trong điều kiện ánh sáng cho phép.

## Nhận xét

Model đạt được mục tiêu nhận diện ban đầu, do đây chỉ là demo trên mô hình chưa ra máy thuật nên điều khiện ánh sáng ở các phần tối chưa được tốt để nhận diện.
