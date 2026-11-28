# Định dạng file project LumaCast (phiên bản 1)

Mã nguồn tham chiếu: `src/core/Project.cpp` (`saveProject`, `loadProject`). Kiểm thử: `tests/test_render.cpp`, mục "Lưu / mở project".

## 1. Tổng quan
- JSON UTF-8, phần mở rộng khuyến nghị `.lumacast.json`.
- Ghi **nguyên tử** (`QSaveFile`): mất điện giữa chừng không làm hỏng file cũ.
- Đường dẫn media lưu **tương đối** so với thư mục chứa file project; khi mở được đổi lại thành tuyệt đối.
- Tự lưu: `<tên project>.autosave` cạnh file; project chưa đặt tên: `%AppData%/LumaCast/untitled.autosave.json`.
  Lưu thành công sẽ xoá bản tự lưu cũ.
- Trường lạ bị bỏ qua; trường thiếu dùng giá trị mặc định (xem bảng).

**Thêm ở M5a (tất cả tuỳ chọn, `version` vẫn 1, project cũ mở bình thường):** `audio.device` ở gốc, `volume`/`mute` ở `channels[]`, `volume` ở `layers[]`. Giá trị mặc định (100 %, không mute, thiết bị mặc định) **không được ghi ra** nên file không đổi nếu không dùng âm thanh.

**Thêm ở M5b (tất cả tuỳ chọn, `version` vẫn 1):** nguồn `type: "ndi"` và `"camera"` ở `sources[]`, và Output `type: "ndi"` ở `outputs[]` (xem bảng). Project cũ mở bình thường; project có nguồn/Output NDI mở được cả trên máy **không có** NDI Runtime/camera (nguồn ở trạng thái "không khả dụng", Output NDI chờ — không phải lỗi nạp). Bản LumaCast cũ (trước M5b) mở project mới sẽ báo "Loai nguon khong biet" ở nguồn trực tiếp.

**Thêm sau M6 (tuỳ chọn, `version` vẫn 1):** nguồn `type: "window"` (slideshow PowerPoint / cửa sổ, xem bảng). Bản LumaCast cũ mở project có nguồn này sẽ báo "Loai nguon khong biet".

**Không đổi ở M5c:** vẫn `version` 1, không thêm trường. Cài đặt điều khiển ngoài (cổng, LAN, token), Khoá vận hành, tuỳ chọn tự khởi động và "project gần nhất" là **cài đặt theo máy/phiên** (`QSettings`), cố ý **không** lưu vào file project để mở project trên máy khác không mang theo token hay mở cổng mạng.

**Thêm cho overlay (SRC-08; tuỳ chọn, `version` vẫn 1):** `overlays[]` trong `channels[]` (mục 2.3a). Không có overlay thì không ghi ra, nên project cũ mở và lưu lại không đổi. Bản LumaCast cũ mở project có overlay sẽ bỏ qua chúng (và mất khi lưu lại).

## 2. Cấu trúc

```json
{
  "app": "LumaCast",
  "version": 1,
  "fps": 60,
  "sources":  [ ... ],
  "scenes":   [ ... ],
  "channels": [ ... ],
  "outputs":  [ ... ]
}
```

| Trường gốc | Kiểu | Bắt buộc | Mặc định | Ghi chú |
|---|---|---|---|---|
| `app` | chuỗi | có | — | phải đúng `"LumaCast"` |
| `version` | số nguyên | có | — | hiện phải bằng `1` |
| `fps` | số | không | 60 | nhịp master, 1–240 |
| `audio` | đối tượng | không | (không có) | `{ "device": "<id>" }`: id thiết bị âm thanh ra mong muốn (WASAPI: id endpoint của Windows). Vắng/rỗng = mặc định của hệ thống. Id không có trên máy đang mở → dùng mặc định (dock *Âm thanh* ghi chú "không có trên máy này"), id vẫn được giữ trong project |

### 2.1 `sources[]`
| Trường | Kiểu | Ghi chú |
|---|---|---|
| `id` | chuỗi | **duy nhất, không rỗng**; Layer tham chiếu theo `id` |
| `type` | chuỗi | `video` · `image` · `color` · `testpattern` · `ndi` · `camera` · `window` |
| `path` | chuỗi | `video`/`image`: đường dẫn tương đối |
| `color` | chuỗi `#AARRGGBB` | `color` |
| `label` | chuỗi | `color`, `testpattern` |
| `width`, `height` | số nguyên | `testpattern`, mặc định 1920×1080; `camera`: độ phân giải yêu cầu (mặc định 0×0 = để thiết bị chọn; giá trị âm/> 16384 bị đưa về 0×0) |
| `ndiName` | chuỗi | `ndi`: tên nguồn NDI như NDI báo (`MÁY (Tên)`); **bắt buộc, rỗng là lỗi nạp** |
| `ndiUrl` | chuỗi | `ndi`: địa chỉ nguồn (tuỳ chọn) |
| `device` | chuỗi | `camera`: id thiết bị (Windows: symbolic link của Media Foundation) |
| `deviceName` | chuỗi | `camera`: tên hiển thị; dùng nhận lại thiết bị khi `device` đổi (cổng USB khác). Cần ít nhất một trong `device`/`deviceName` |
| `fps` | số | `camera`: fps yêu cầu (0 = để thiết bị chọn; giá trị không hợp lệ → 0) |
| `app` | chuỗi | `window`: tên file chạy của cửa sổ cần bắt (vd. `notepad.exe`), hoặc `powerpoint-slideshow` = bất kỳ cửa sổ slideshow PowerPoint nào; **bắt buộc** |
| `windowClass`, `title` | chuỗi | `window`: lớp và tiêu đề cửa sổ để tìm lại (không dùng cho `powerpoint-slideshow`). Nhiều cửa sổ cùng lớp mà không cái nào trùng tiêu đề thì không bắt cái nào. Không lưu định danh phiên (HWND) |
| `label` | chuỗi | cũng dùng cho `ndi`/`camera`/`window` (tên hiển thị của nguồn / tên Scene tạo ra) |

### 2.2 `scenes[]`
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `name` | chuỗi | `"Scene"` | |
| `width`, `height` | số nguyên ≥ 1 | 1920, 1080 | độ phân giải thiết kế |
| `background` | `#AARRGGBB` | `#ff000000` | |
| `duration` | số ≥ 0,1 | 10 | giây; thời lượng đồng hồ Scene dùng cho keyframe (giá trị không hợp lệ → 10) |
| `loop` | bool | true | lặp lại khi hết thời lượng |
| `restartOnAir` | bool | true | chạy lại đồng hồ từ 0 mỗi khi Scene lên Program / bắt đầu chuyển vào |
| `volume` | số | 1 | âm lượng riêng của Scene (0–2, 1 = nguyên gốc), nhân thêm vào âm lượng từng Layer; chỉ ghi khi khác 1 (tuỳ chọn, vẫn `version` 1) |
| `mute` | bool | false | tắt tiếng riêng của Scene; chỉ ghi khi `true` |
| `layers[]` | mảng | — | **thứ tự vẽ từ dưới lên**: phần tử cuối nằm trên cùng |

`layers[]`:
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `source` | chuỗi | — | phải trùng một `sources[].id` (nếu không: lỗi nạp) |
| `x`, `y`, `w`, `h` | số | 0, 0, 1, 1 | chuẩn hoá theo Scene, gốc ở góc trên-trái |
| `opacity` | số 0–1 | 1 | |
| `fit` | chuỗi | `fit` | `stretch` · `fit` · `fill` |
| `visible` | bool | true | |
| `effects[]` | mảng | (rỗng) | chuỗi Effect, chạy **từ phần tử đầu đến cuối** trước khi Layer được vẽ vào Scene (trường tuỳ chọn, thêm ở M4a, vẫn `version` 1) |

`effects[]`:
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `id` | chuỗi | — | `builtin.blur`, `builtin.glow`, `builtin.colorgrade`, `builtin.rgbsplit`, `builtin.pixelate`, `builtin.noise` hoặc `id` của plug-in; thiếu plug-in **không** làm lỗi nạp (Effect bị bỏ qua, dữ liệu được giữ) |
| `enabled` | bool | true | |
| `params` | đối tượng | (mặc định của Effect) | tên tham số → giá trị: số (`float`/`int`/`choice` theo chỉ số), `true/false`, `"#AARRGGBB"` (màu), `[x, y]` (điểm). Giá trị lạ/thiếu được kẹp hoặc lấy mặc định khi dùng |

Các trường tuỳ chọn của Layer thêm ở M4b (mặc định = không đổi):
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `rotation` | số | 0 | độ, **theo chiều kim đồng hồ**, quanh tâm layer |
| `crop` | `[x, y, w, h]` | `[0,0,1,1]` | vùng của nguồn được dùng, chuẩn hoá, gốc trên-trái, **tính theo hình đã xoay** (video điện thoại); `w`,`h` ≤ 0 → bỏ qua |
| `blend` | chuỗi | `normal` | `normal` · `add` · `multiply` · `screen` (giá trị lạ → `normal`) |
| `volume` | số | 1 | âm lượng nguồn trong Layer (0–2, 1 = nguyên gốc); ngoài khoảng bị ép vào 0–2, không phải số → 1. Không có tác dụng với nguồn không có tiếng |
| `tracks[]` | mảng | (rỗng) | keyframe, xem dưới |

`tracks[]`: mỗi phần tử `{ "prop": "x"|"y"|"w"|"h"|"opacity"|"rotation", "keys": [ { "t": giây, "v": giá trị, "i": kiểu nối } ] }`.
`i` là cách nối từ key này đến key kế tiếp: `linear` (mặc định) · `easein` · `easeout` · `easeinout` · `hold`.
Thuộc tính có track thì **track ghi đè** giá trị tĩnh; trước key đầu và sau key cuối giữ giá trị của key đó. Khi nạp: key tự sắp xếp theo `t`, `t` âm → 0,
giá trị không phải số bị bỏ, `prop` lạ hoặc trùng và track rỗng bị bỏ, `i` lạ → `linear`.

Chưa lưu trong project: Transition đang chọn (kiểu, tham số, easing, Stinger) — chỉ là trạng thái phiên làm việc.

### 2.3 `channels[]`
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `name` | chuỗi | `"Channel"` | |
| `width`, `height` | số nguyên | 1920, 1080 | kích thước khi chạy transition |
| `scenes` | mảng số | — | chỉ số vào `scenes[]` của gốc; chỉ số sai là lỗi nạp |
| `program`, `preview` | số | 0, 1 | chỉ số **trong `scenes` của Channel**; giá trị sai được đưa về hợp lệ |
| `volume` | số | 1 | âm lượng bus của Channel (0–2; UI 0–150 %); ép vào 0–2 |
| `mute` | bool | false | tắt tiếng Channel (hình không đổi). Trạng thái mute/âm lượng được lưu cùng project; bật project lên là đúng như lúc lưu |

### 2.3a `channels[].overlays[]`
Thứ tự mảng = thứ tự vẽ (phần tử cuối nằm trên cùng). Tối đa 32 mục. `type` lạ hoặc phần tử không phải đối tượng bị bỏ; giá trị ngoài khoảng bị kẹp.

| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `id` | số nguyên > 0 | tự đánh | duy nhất trong Channel; thiếu/trùng được đánh lại |
| `type` | chuỗi | — | `title` · `ticker` · `countdown` · `logo` |
| `name` | chuỗi | theo loại | |
| `x`, `y`, `w`, `h` | số | theo loại | hộp, chuẩn hoá theo canvas Channel, gốc trên-trái |
| `opacity` | số 0–1 | 1 | |
| `fade` | số 0–10 | 0.3 | giây hiện/ẩn; 0 = cắt ngay |
| `align` | chuỗi | `center` | `left` · `center` · `right` (chữ chạy không dùng) |
| `background` | `#AARRGGBB` | trong suốt | nền của cả hộp |
| `on` | bool | false | đang hiện lúc lưu (chỉ ghi khi `true`) |
| `text` | chuỗi ≤ 2000 | rỗng | `title`/`ticker`: nội dung; `countdown`: chữ trước đồng hồ |
| `font` | chuỗi | (mặc định hệ thống) | tên họ font |
| `fontSize` | số 4–2000 | theo loại | px theo canvas Channel |
| `bold`, `italic` | bool | false | |
| `color`, `outlineColor` | `#AARRGGBB` | trắng, đen | |
| `outlineWidth` | số 0–50 | 0 | px; 0 = không viền |
| `speed` | số 1–5000 | 150 | `ticker`: px/giây |
| `seconds` | số 1–359999 | 300 | `countdown`: thời lượng. Trạng thái đang chạy không lưu |
| `alert` | bool | false | `countdown`: bật tiếng nhắc (chỉ ghi khi `true`) |
| `alertSeconds` | số 1–3600 | 10 | còn bấy nhiêu giây thì bắt đầu nhắc, mỗi giây một tiếng (chỉ ghi khi khác mặc định, như hai trường dưới) |
| `fastSeconds` | số 0–`alertSeconds` | 5 | còn bấy nhiêu giây thì tiếng cao hơn, hai tiếng/giây; 0 = không |
| `alertVolume` | số 0–100 | 80 | âm lượng tiếng nhắc (%) |
| `image` | chuỗi | rỗng | `logo`: đường dẫn tương đối; thiếu file **không** làm lỗi nạp |

### 2.4 `outputs[]`
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `name` | chuỗi | `"Output N"` | |
| `type` | chuỗi | (không có = màn hình) | `"ndi"`: Output NDI (M5b). Không ghi ra cho Output màn hình |
| `ndiName` | chuỗi | (dùng `name`) | `type: "ndi"`: tên nguồn hiện trên mạng NDI |
| `ndiAudio` | bool | true | `type: "ndi"`: gửi kèm âm thanh bus của Channel (chỉ ghi ra khi `false`) |
| `channel` | số nguyên | 0 | chỉ số `channels[]`; sai là lỗi nạp |
| `screen` | số nguyên | -1 | chỉ số màn hình hệ điều hành (khi toàn màn hình); chỉ dùng khi không có `screenName`; chỉ số vượt quá số màn = Output **không hiển thị** |
| `screenName` | chuỗi | (rỗng) | tên màn hình (`QScreen::name()`, ví dụ `\\.\DISPLAY2`), **ưu tiên hơn `screen`**; tự ghi nhớ khi Output khởi động toàn màn hình. Không còn màn đó = không hiển thị |
| `fullscreen` | bool | false | |
| `width`, `height` | số nguyên | 1920, 1080 | kích thước khi chạy ở chế độ cửa sổ; Output NDI: kích thước khung gửi (16…8192, ngoài khoảng là lỗi nạp) |
| `scale` | chuỗi | `fit` | `fit` · `fill` · `stretch` · `pixel` |
| `rotation` | số | 0 | được đưa về bội số của 90 (0/90/180/270), chiều kim đồng hồ |
| `crop` | `[x, y, w, h]` | `[0,0,1,1]` | vùng của Channel, chuẩn hoá, gốc trên-trái; `w`,`h` phải > 0 |
| `area` | `[x, y, w, h]` | (không có) | vùng LED trong tín hiệu, **điểm ảnh**, gốc trên-trái: hình chỉ đặt trong vùng này, ngoài vùng đen. Toàn màn hình: điểm ảnh của màn; cửa sổ: theo `width`×`height`. Giá trị âm / rỗng / > 16384 bị bỏ qua. Tuỳ chọn, `version` vẫn 1; bản LumaCast cũ mở được nhưng hiện cả khung |
| `calibration` | đối tượng | mặc định = không đổi | xem dưới |

**Bù khe video wall** (OUT-04) không có trường riêng (`version` vẫn 1, bản LumaCast cũ mở được và hiện đúng): `width`/`height` của Channel và Scene bằng `cột × ô + (cột − 1) × khe` (tương tự theo chiều cao), và `crop` của ô cột `c` là `[c·(ô+khe)/canvas, …, ô/canvas, …]`. Khi mở, giao diện suy lại khe từ các giá trị này.

`calibration`: `brightness` (1), `contrast` (1), `gamma` (1), `gain` = `[r, g, b]` (mỗi giá trị 1).
Công thức: `out = pow(clamp(((c − 0.5)·contrast + 0.5)·brightness·gain, 0, 1), 1/gamma)`.

### 2.5 `channels[].playlist`
Tuỳ chọn, thêm cho playlist/slide tự chuyển (CHN-10), `version` vẫn 1. Playlist chưa dùng (không có mục, cài đặt mặc định) **không được ghi ra**; project cũ mở bình thường. Trạng thái đang chạy không lưu: mở project thì playlist luôn dừng.

| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `items[]` | mảng | (rỗng) | theo thứ tự phát: `{ "scene": chỉ số vào scenes[] của gốc, "seconds": số }`. `seconds` vắng/≤ 0 = tự động (video: hết thì chuyển; còn lại: `imageSeconds`), khác 0 thì ép vào 0,5–3600. Chỉ số Scene sai → bỏ mục đó (ghi log), **không** làm lỗi nạp |
| `loop` | bool | true | lặp lại khi hết danh sách |
| `imageSeconds` | số | 8 | thời lượng cho mục tự động không có video (0,5–3600) |
| `transition` | chuỗi | `fade` | `cut` · `fade` · `dip` · `wipeleft` · `wiperight` · `wipeup` · `wipedown` · `plugin`; giá trị lạ → `fade` |
| `transitionPlugin` | chuỗi | — | id Transition khi `transition` = `plugin` |
| `transitionSeconds` | số | 1 | 0,1–10 |

## 3. Quy tắc kiểm tra khi nạp
Nạp thất bại (trả thông báo lỗi, không crash) nếu: JSON hỏng · `app` sai · `version` ≠ 1 · `fps` ngoài 1–240 ·
`id` nguồn rỗng hoặc trùng · nguồn `video`/`image` không mở được · Layer tham chiếu nguồn không tồn tại ·
kích thước Scene < 1 · chỉ số Scene của Channel sai · Output trỏ Channel không tồn tại · `crop` có `w` hoặc `h` ≤ 0 ·
không có Channel nào · không có Output nào.

## 4. Ví dụ tối giản
```json
{
  "app": "LumaCast", "version": 1, "fps": 60,
  "sources": [ { "id": "src1", "type": "testpattern", "width": 1920, "height": 1080, "label": "Test" } ],
  "scenes":  [ { "name": "Test", "width": 1920, "height": 1080,
                 "layers": [ { "source": "src1", "fit": "stretch" } ] } ],
  "channels": [ { "name": "Channel 1", "width": 1920, "height": 1080, "scenes": [0], "program": 0, "preview": 0 } ],
  "outputs":  [ { "name": "LED 1", "channel": 0, "width": 1920, "height": 1080, "fullscreen": true, "screen": 1 } ]
}
```

## 5. Quy tắc phiên bản (chưa triển khai bộ di trú)
- Thay đổi **tương thích** (thêm trường tuỳ chọn có mặc định): giữ `version`, bản cũ vẫn mở được.
- Thay đổi **phá vỡ** (đổi nghĩa/xoá trường): tăng `version`; bản mới phải **di trú** được các phiên bản cũ; bản cũ gặp
  `version` mới hơn phải từ chối với thông báo rõ.
- Khi thêm tính năng cần lưu (Effect, keyframe, âm thanh, plug-in): cập nhật tài liệu này và thêm test vòng lưu–mở.
