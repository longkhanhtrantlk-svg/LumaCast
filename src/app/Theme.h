#pragma once
class QApplication;

namespace lc {

// Giao dien toi cho moi truong van hanh (it choi mat, tuong phan ro). Mau theo quy uoc trang thai:
// Preview = xanh la, Program = do, Selected = xanh duong, Warning = vang.
void applyDarkTheme(QApplication& app);

namespace colors {
constexpr const char* kProgram = "#e5322d";
constexpr const char* kPreview = "#2fbf5b";
constexpr const char* kAccent = "#2f7de1";
constexpr const char* kWarn = "#e0a526";
constexpr const char* kPanel = "#1b1e27";
constexpr const char* kBg = "#12141a";
} // namespace colors

} // namespace lc
