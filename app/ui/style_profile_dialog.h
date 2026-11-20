#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QVBoxLayout;

namespace openoffice {

// "排版风格档案": what the assistant learned about the looks the person keeps for drafts,
// pack by pack, and what the person set by hand (常用 / 不用 / 自动). Learning can be paused
// and what was learned cleared; everything here stays on this computer.
class StyleProfileDialog final : public QDialog {
    Q_OBJECT

public:
    explicit StyleProfileDialog(QWidget* parent = nullptr);

    // Test/automation seams: what the rows say, and what their controls do.
    QStringList rowTexts() const;
    QString summaryText() const;
    void setManual(const QString& pack, int value); // +1 常用, -1 不用, 0 自动
    void setPaused(bool paused);
    void clearLearned(); // what 清除学到的记录 does once confirmed

private:
    void rebuild();

    QLabel* m_summary = nullptr;
    QCheckBox* m_paused = nullptr;
    QVBoxLayout* m_rows = nullptr;
};

} // namespace openoffice
