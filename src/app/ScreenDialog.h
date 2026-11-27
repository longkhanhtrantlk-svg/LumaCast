#pragma once
#include "core/ScreenSetup.h"
#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace lc {

// Thiet lap man hinh (UI-08): Output (them/xoa/ten, man hinh, cua so / toan man hinh, kich thuoc, vung LED trong tin hieu),
// khung hinh (canvas) cua kenh, video wall, cau hinh LED luu san theo may, canh bao co gian / cat. Moi thu chi la BAN NHAP
// cho den khi bam "Ap dung" (mot buoc Hoan tac).
class ScreenDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreenDialog(Project& project, QWidget* parent = nullptr);

    // Do cua so chinh cung cap.
    std::function<ScreenSetup()> current;                                    // trang thai dang chay
    std::function<bool(const ScreenSetup&, const std::vector<char>& allScenes)> apply;
    std::function<void(bool)> testPattern;                                   // bat/tat mau thu moi Output dang chay
    std::function<bool(const QString&)> confirm;                             // hoi truoc khi lam viec co the che mat bang dieu khien

    void reload();                                                           // doc lai ban nhap tu trang thai dang chay
    bool applyDraft();                                                       // nut "Ap dung"
    const ScreenSetup& draft() const { return draft_; }
    int selectedOutput() const;
    void selectOutput(int i);
    QStringList warnings() const;
    // Cau hinh LED luu theo may (khong nam trong project).
    void savePreset(const QString& name);
    bool loadPreset(const QString& name);
    void deletePreset(const QString& name);
    QStringList presetNames() const;
    void makeWallFromFields();                                               // nut "Tao video wall"

private:
    void build();
    void syncList(int select);
    void syncForm();
    void updateWarnings();
    void edit(const std::function<void(OutputConfig&)>& mutate);
    void setCanvas(int channel, QSize s);
    void setArea(const QRect& r);
    void followArea();
    void refreshPresets();
    QSize canvasOfSelected() const;
    int channelOfSelected() const;

    Project& project_;
    ScreenSetup draft_;
    std::vector<char> allScenes_;
    bool syncing_ = false;

    QListWidget* list_ = nullptr;
    QToolButton *addB_ = nullptr, *removeB_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox *channel_ = nullptr, *mode_ = nullptr, *screen_ = nullptr, *scale_ = nullptr, *presets_ = nullptr;
    QSpinBox *w_ = nullptr, *h_ = nullptr, *areaX_ = nullptr, *areaY_ = nullptr, *areaW_ = nullptr, *areaH_ = nullptr;
    QSpinBox *canvasW_ = nullptr, *canvasH_ = nullptr;
    QSpinBox *cols_ = nullptr, *rows_ = nullptr, *cellW_ = nullptr, *cellH_ = nullptr, *gapX_ = nullptr, *gapY_ = nullptr;
    QDoubleSpinBox *metersW_ = nullptr, *metersH_ = nullptr, *pitch_ = nullptr;
    QGroupBox* areaBox_ = nullptr;
    QCheckBox *follow_ = nullptr, *allScenesBox_ = nullptr;
    QLabel *ledPixels_ = nullptr, *warnings_ = nullptr, *canvasTitle_ = nullptr;
    QPushButton *applyB_ = nullptr, *patternB_ = nullptr;
};

// "Project moi": kich thuoc khung hinh, tuy chon video wall (kem khe).
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);
    Project project() const;                                   // project moi theo cac o dang chon
    QString problem() const;                                   // rong = tao duoc

private:
    QSpinBox *w_ = nullptr, *h_ = nullptr, *cols_ = nullptr, *rows_ = nullptr, *gapX_ = nullptr, *gapY_ = nullptr;
    QGroupBox* wall_ = nullptr;
    QCheckBox* fullscreen_ = nullptr;
    QLabel* info_ = nullptr;
};

} // namespace lc
