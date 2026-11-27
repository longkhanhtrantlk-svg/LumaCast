#pragma once
#include "Shortcuts.h"
#include "control/Midi.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QTableWidget;

namespace lc {

// Hop thoai "Phim tat va MIDI": moi lenh live mot dong (phim tat + nut MIDI hoc bang cach bam tren thiet bi).
// Trung phim / trung nut / phim bi giu cho: hien loi, khong luu. OK = luu theo may (QSettings) va ap dung ngay.
// Trong luc hop thoai mo, nut MIDI KHONG chay lenh (giong phim tat bi hop thoai chan); Huy = tra lai cau hinh cu.
class InputDialog : public QDialog {
    Q_OBJECT
public:
    InputDialog(MidiService& midi, const QStringList& outputNames, const KeyMap& keys, QWidget* parent = nullptr);
    ~InputDialog() override;

    // Kiem tra + luu + ap dung MIDI; tra ve loi (rong = thanh cong). Cung la diem vao cho test.
    QString applyAndSave();
    KeyMap keys() const;
    QString learning() const { return learning_; }       // ma lenh dang cho bam nut (rong = khong)
    void startLearn(const QString& actionId);
    void stopLearn();

public slots:
    void accept() override;
    void done(int r) override;

private:
    struct Row { QString id; QString name; QKeySequenceEdit* key = nullptr; QLabel* midi = nullptr; QPushButton* learn = nullptr; };
    QString validate();                 // to do cac dong loi; tra ve mo ta
    void refreshRows();
    void refreshDevices();
    void applyDevice();
    void finish();                      // het che do gan nut

    MidiService& midi_;
    MidiConfig original_;
    MidiConfig draft_;
    QList<Row> rows_;
    QStringList deviceList_;
    QString learning_;
    bool saved_ = false, finished_ = false;
    QCheckBox* midiOn_ = nullptr;
    QComboBox* device_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* last_ = nullptr;
    QLabel* err_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace lc
