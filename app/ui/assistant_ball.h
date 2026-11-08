#pragma once

#include "assistant/assistant_commands.h"

#include <QElapsedTimer>
#include <QPair>
#include <QPointer>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QScrollArea;
class QTimer;
class QVBoxLayout;
class QVariantAnimation;

namespace openoffice {

class SendButton;

// The assistant is a floating ball: a small translucent window owned by the
// main window, so it floats above the embedded editor. Drag it anywhere on the
// document page; it stays where it is dropped (dropped outside, it springs back
// to the page edge) and keeps that relative spot as the window resizes. Click
// it to open a capsule for
// typing; replies appear as bubbles above the capsule. Slash commands become
// commands of the assistant interface; other text goes to the local AI (see
// LocalAssistant) through freeTextSubmitted. Execution belongs to the window.
class AssistantBall final : public QWidget {
    Q_OBJECT

public:
    explicit AssistantBall(QWidget* anchor);

    void setBallVisible(bool visible); // the toolbar toggle
    // The region the ball lives in (the document page); default: the window's central widget.
    void setArea(QWidget* area) { m_area = area; }
    void followAnchor();               // keep the relative position as the window moves
    void expand();
    void collapse();
    bool expanded() const { return m_expanded; }
    void setDarkTheme(bool dark);
    void setWorking(bool working);
    bool working() const { return m_working; }
    void showOutcome(const AssistantCommand& command, const CommandOutcome& outcome);
    // Test/automation seam: exactly like typing the text and pressing Enter.
    void submitText(const QString& text);
    int messageCount() const { return m_messageCount; }
    QString lastMessageText() const { return m_lastMessageText; }

    // A reply card with buttons (setup steps, "run this?" confirmations).
    // Each button works once; afterwards the card's buttons are disabled.
    using CardAction = QPair<QString, std::function<void()>>;
    void showCard(const QString& title, const QString& text, const QList<CardAction>& actions, bool ok = true);
    // A streamed reply from the local AI.
    void beginReply(const QString& title);
    void appendReply(const QString& text);
    void replaceReply(const QString& text); // e.g. a progress line that updates in place
    void endReply(const QString& text, bool ok);
    // One line about the local AI in the welcome block, plus the input hint.
    void setStatus(const QString& text, bool ready);

signals:
    void commandSubmitted(openoffice::AssistantCommand command);
    void freeTextSubmitted(QString text);
    void setupRequested();
    void opened();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QRect anchorArea() const;
    QPoint restingCentre() const;
    QPoint clampToArea(const QPoint& centre) const;
    QRectF ballBox() const;
    QRectF capsuleRect() const;
    QRectF historyRect() const;
    void placeAround(const QPoint& ballCentre);
    void layoutContent();
    void tick();
    void ensureTimer();
    void animateScale(qreal target, int duration, bool spring);
    void submit();
    void addUserMessage(const QString& text);
    void addAssistantMessage(const QString& title, const QString& text, bool ok, const QString& quote = {});
    void appendMessage(QWidget* row);
    void setMessageFlow(bool upward);
    void applyStyle();
    void updateSendState();

    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_area;
    QWidget* m_content = nullptr; // capsule input and messages; fades with expansion
    QScrollArea* m_scroll = nullptr;
    QVBoxLayout* m_messages = nullptr;
    QWidget* m_welcome = nullptr;
    QLineEdit* m_input = nullptr;
    SendButton* m_send = nullptr;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    QVariantAnimation* m_expandAnimation = nullptr;
    QVariantAnimation* m_scaleAnimation = nullptr;
    QVariantAnimation* m_snapAnimation = nullptr;
    QMetaObject::Connection m_expandFinished;

    bool m_dockRight = true; // capsule grows toward the page centre: left when the ball is on the right
    bool m_openUp = true;    // capsule below the replies; false near the top: replies below
    QPointF m_position{0.94, 0.86}; // ball centre as a fraction of the area's width and height
    bool m_expanded = false;
    qreal m_progress = 0;    // 0 collapsed .. 1 fully open (spring may overshoot)
    bool m_dark = false;
    bool m_working = false;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_pressOffset;
    qreal m_scale = 1.0;
    qreal m_time = 2.4;
    qreal m_energy = 0;
    qreal m_ring = 0;
    int m_messageCount = 0;
    QString m_lastMessageText;
    QLabel* m_caption = nullptr;
    QPointer<QWidget> m_replyRow;
    QPointer<QLabel> m_replyBody;
    QPointer<QLabel> m_replyTitle;
    QString m_replyText;
};

} // namespace openoffice
