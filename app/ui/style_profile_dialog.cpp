#include "style_profile_dialog.h"

#include "assistant/document_style.h"
#include "assistant/style_profile.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace openoffice {
namespace {

QLabel* label(const QString& value, const char* name, QWidget* parent)
{
    auto* widget = new QLabel(value, parent);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setWordWrap(true);
    widget->setTextFormat(Qt::PlainText);
    return widget;
}

QString standingText(const StyleStanding& standing)
{
    if (standing.manual > 0) return QStringLiteral("你设为常用：起草时总会作为候选，并优先考虑。");
    if (standing.manual < 0) return QStringLiteral("你设为不用：起草时不会再推荐。");
    const QString basis = QStringLiteral("（%1 次，%2 份文档）").arg(standing.count).arg(standing.documents);
    if (standing.count == 0) return QStringLiteral("还没有记录。");
    if (!standing.settled) return QStringLiteral("还在观察") + basis + QStringLiteral("：至少 3 次、2 份文档才算数。");
    if (standing.preference >= 0.15) return QStringLiteral("学到：你常选它") + basis + QStringLiteral("，起草时会多推荐。");
    if (standing.preference <= -0.15) return QStringLiteral("学到：你常换掉它") + basis + QStringLiteral("，起草时会少推荐。");
    return QStringLiteral("学到：没有明显偏好") + basis + QStringLiteral("。");
}

} // namespace

StyleProfileDialog::StyleProfileDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("styleProfileDialog"));
    setWindowTitle(QStringLiteral("排版风格档案"));
    setMinimumSize(560, 560);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(10);
    layout->addWidget(label(QStringLiteral(
        "起草时，助手会参考你用过的排版风格：插入时选中的风格加分（换过一次再选中的加得多），被你换掉的减分。"
        "同一种倾向至少出现 3 次、在 2 份文档里才算数，时间久了会慢慢淡化。你在这里设的“常用”“不用”优先于学到的。"
        "这些记录只保存在这台电脑上，只记风格和场景，不记文档内容。"),
        "styleProfileIntro", this));
    m_summary = label(QString(), "styleProfileSummary", this);
    layout->addWidget(m_summary);
    m_paused = new QCheckBox(QStringLiteral("暂停学习（已经学到的照样参考）"), this);
    m_paused->setObjectName(QStringLiteral("styleProfilePaused"));
    connect(m_paused, &QCheckBox::toggled, this, &StyleProfileDialog::setPaused);
    layout->addWidget(m_paused);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* list = new QWidget(scroll);
    m_rows = new QVBoxLayout(list);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(8);
    scroll->setWidget(list);
    layout->addWidget(scroll, 1);

    auto* buttons = new QHBoxLayout;
    auto* clear = new QPushButton(QStringLiteral("清除学到的记录…"), this);
    clear->setObjectName(QStringLiteral("styleProfileClear"));
    connect(clear, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(this, QStringLiteral("清除学到的记录"),
            QStringLiteral("清除助手学到的排版偏好？你设的“常用”“不用”会保留。清除后不能恢复。"));
        if (answer == QMessageBox::Yes) clearLearned();
    });
    buttons->addWidget(clear);
    buttons->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    close->setObjectName(QStringLiteral("styleProfileClose"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    setStyleSheet(QStringLiteral(
        "QLabel#styleProfileIntro, QLabel#styleProfileStanding, QLabel#styleProfileDescription { color: #6E6E73; }"
        "QLabel#styleProfileName { font-weight: 600; }"
        "QFrame#styleProfileRow { border: 1px solid rgba(0,0,0,0.12); border-radius: 10px; }"));
    rebuild();
}

void StyleProfileDialog::rebuild()
{
    const StyleProfile profile = loadStyleProfile();
    m_summary->setText(QStringLiteral("已记录 %1 次选择%2。").arg(profile.entries.size())
                           .arg(profile.paused ? QStringLiteral("，学习已暂停") : QString()));
    {
        const QSignalBlocker block(m_paused);
        m_paused->setChecked(profile.paused);
    }
    while (QLayoutItem* item = m_rows->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // A row's own choice may be what called this: taken out now, deleted later.
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    for (const StylePack& pack : builtinStylePacks()) {
        const StyleStanding standing = styleStanding(profile, pack.id);
        auto* row = new QFrame(this);
        row->setObjectName(QStringLiteral("styleProfileRow"));
        row->setProperty("pack", pack.id);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        auto* texts = new QVBoxLayout;
        texts->setSpacing(2);
        texts->addWidget(label(pack.name, "styleProfileName", row));
        texts->addWidget(label(pack.description, "styleProfileDescription", row));
        texts->addWidget(label(standingText(standing), "styleProfileStanding", row));
        rowLayout->addLayout(texts, 1);
        auto* choice = new QComboBox(row);
        choice->setObjectName(QStringLiteral("styleProfileManual"));
        choice->addItem(QStringLiteral("自动"), 0);
        choice->addItem(QStringLiteral("常用"), 1);
        choice->addItem(QStringLiteral("不用"), -1);
        choice->setCurrentIndex(standing.manual > 0 ? 1 : standing.manual < 0 ? 2 : 0);
        const QString id = pack.id;
        connect(choice, &QComboBox::activated, this, [this, choice, id] { setManual(id, choice->currentData().toInt()); });
        rowLayout->addWidget(choice, 0, Qt::AlignTop);
        m_rows->addWidget(row);
    }
    m_rows->addStretch(1);
}

QStringList StyleProfileDialog::rowTexts() const
{
    QStringList rows;
    for (QFrame* row : findChildren<QFrame*>(QStringLiteral("styleProfileRow"))) {
        if (row->property("pack").toString().isEmpty()) continue;
        QStringList parts{row->property("pack").toString()};
        for (QLabel* text : row->findChildren<QLabel*>()) parts << text->text();
        if (auto* choice = row->findChild<QComboBox*>()) parts << QLatin1Char('[') + choice->currentText() + QLatin1Char(']');
        rows << parts.join(QLatin1Char(' '));
    }
    return rows;
}

QString StyleProfileDialog::summaryText() const
{
    return m_summary->text();
}

void StyleProfileDialog::setManual(const QString& pack, int value)
{
    StyleProfile profile = loadStyleProfile();
    if (value == 0) profile.manual.remove(pack);
    else profile.manual.insert(pack, value > 0 ? 1 : -1);
    saveStyleProfile(profile);
    rebuild();
}

void StyleProfileDialog::setPaused(bool paused)
{
    StyleProfile profile = loadStyleProfile();
    profile.paused = paused;
    saveStyleProfile(profile);
    rebuild();
}

void StyleProfileDialog::clearLearned()
{
    StyleProfile profile = loadStyleProfile();
    profile.entries.clear();
    saveStyleProfile(profile);
    rebuild();
}

} // namespace openoffice
