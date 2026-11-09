#include "assistant_ball.h"

#include "assistant_art.h"

#include "assistant/markdown_html.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace openoffice {
namespace {

constexpr int kBall = 58;     // visible ball diameter
constexpr int kBox = 116;     // window square around the ball: aura, ring, hover growth
constexpr int kWidth = 500;   // expanded window
constexpr int kHeight = 580;
constexpr int kTextWidth = 330; // widest reply text in a bubble
constexpr int kCapsuleHeight = 46;

// Apple-inspired system colours (light / dark), chosen by this project.
struct Colors {
    QColor surface, hairline, text, secondary, chip, chipHover, bubble, accentA, accentB, ok, warn, quote, disabled,
        history;
};

Colors colorsFor(bool dark)
{
    if (dark)
        return {QColor(44, 44, 46, 246), QColor(255, 255, 255, 30), QColor("#F5F5F7"), QColor("#98989D"),
                QColor("#3A3A3C"), QColor("#48484A"), QColor(44, 44, 46, 246), QColor("#0A84FF"),
                QColor("#5E5CE6"), QColor("#30D158"), QColor("#FF9F0A"), QColor("#232325"), QColor("#48484A"),
                QColor(28, 28, 30, 240)};
    return {QColor(255, 255, 255, 248), QColor(0, 0, 0, 24), QColor("#1D1D1F"), QColor("#6E6E73"),
            QColor("#FFFFFF"), QColor("#F2F2F7"), QColor("#FFFFFF"), QColor("#0A84FF"),
            QColor("#5E5CE6"), QColor("#34C759"), QColor("#FF9F0A"), QColor("#F7F7FA"), QColor("#D1D1D6"),
            QColor(242, 242, 247, 238)};
}

QString rgba(const QColor& color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

QEasingCurve springCurve()
{
    QEasingCurve curve;
    curve.setCustomType(&art::springEase);
    return curve;
}

// A floating window activates normally; this also covers the case where Qt
// already counts it active while Win32 focus sits in the CEF editor window.
void claimKeyboardFocus(QWidget* widget)
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(widget->window()->winId());
    if (::GetFocus() != window) ::SetFocus(window);
#else
    Q_UNUSED(widget);
#endif
}

QString slashExample(const QString& name)
{
    static const QHash<QString, QString> examples{
        {QStringLiteral("assistant.listCommands"), QStringLiteral("/命令")},
        {QStringLiteral("document.status"), QStringLiteral("/状态")},
        {QStringLiteral("document.create"), QStringLiteral("/新建 文档")},
        {QStringLiteral("document.save"), QStringLiteral("/保存")},
        {QStringLiteral("text.insert"), QStringLiteral("/插入 文字")},
        {QStringLiteral("selection.getText"), QStringLiteral("/选中")},
        {QStringLiteral("document.getText"), QStringLiteral("/全文")},
        {QStringLiteral("edit.undo"), QStringLiteral("/撤销")},
        {QStringLiteral("edit.redo"), QStringLiteral("/重做")},
    };
    return examples.value(name);
}

QLabel* label(const QString& text, const char* name, QWidget* parent)
{
    auto* widget = new QLabel(text, parent);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    return widget;
}

// Rounded bubble: accent gradient for the person, frosted fill for replies.
class Bubble final : public QFrame {
public:
    Bubble(bool fromUser, const bool* dark, QWidget* parent)
        : QFrame(parent), m_user(fromUser), m_dark(dark)
    {
        setMaximumWidth(kTextWidth + 30);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const Colors colors = colorsFor(*m_dark);
        if (m_user) {
            QLinearGradient fill(rect().topLeft(), rect().bottomRight());
            fill.setColorAt(0, colors.accentA);
            fill.setColorAt(1, colors.accentB);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
        } else {
            painter.setPen(QPen(colors.hairline, 1));
            painter.setBrush(colors.bubble);
        }
        painter.drawPath(art::continuousRoundedRect(shape, 18));
    }

private:
    bool m_user;
    const bool* m_dark;
};

// Quoted text sits on its own smooth-cornered surface inside the bubble.
class QuoteFrame final : public QFrame {
public:
    QuoteFrame(const bool* dark, QWidget* parent) : QFrame(parent), m_dark(dark) {}

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Colors colors = colorsFor(*m_dark);
        painter.setPen(QPen(colors.hairline, 1));
        painter.setBrush(colors.quote);
        painter.drawPath(art::continuousRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12));
    }

private:
    const bool* m_dark;
};

// Word-wrapped labels otherwise pick a narrow heuristic width; give each the
// width of its longest line, up to the bubble's limit.
void fitWidth(QLabel* label, int limit)
{
    QFont font = label->font();
    font.setPixelSize(13); // the bubble text size in the style sheet
    const QFontMetrics metrics(font);
    int widest = 0;
    for (const QString& line : label->text().split(QLatin1Char('\n')))
        widest = qMax(widest, metrics.horizontalAdvance(line));
    label->setMinimumWidth(qMin(widest + 6, limit));
}

class StatusMark final : public QWidget {
public:
    StatusMark(bool ok, const Colors& colors, QWidget* parent)
        : QWidget(parent), m_ok(ok), m_color(ok ? colors.ok : colors.warn)
    {
        setFixedSize(16, 16);
        setAccessibleName(ok ? QStringLiteral("成功") : QStringLiteral("未完成"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawEllipse(QRectF(0.5, 0.5, 15, 15));
        painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (m_ok) {
            painter.drawPolyline(QPolygonF{{4.6, 8.2}, {7.0, 10.6}, {11.4, 5.6}});
        } else {
            painter.drawLine(QPointF(8, 4.4), QPointF(8, 8.8));
            painter.drawPoint(QPointF(8, 11.4));
        }
    }

private:
    bool m_ok;
    QColor m_color;
};

} // namespace

// Circular send button with an up arrow; dips when pressed, springs back.
class SendButton final : public QAbstractButton {
public:
    explicit SendButton(QWidget* parent) : QAbstractButton(parent)
    {
        setFixedSize(32, 32);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(QStringLiteral("发送"));
        setToolTip(QStringLiteral("发送（Enter）"));
        m_press = new QVariantAnimation(this);
        connect(m_press, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_scale = value.toReal();
            update();
        });
        connect(this, &QAbstractButton::pressed, this, [this] { animateTo(0.86, 90, QEasingCurve(QEasingCurve::OutCubic)); });
        connect(this, &QAbstractButton::released, this, [this] { animateTo(1.0, 420, springCurve()); });
    }
    void setColors(const Colors& colors)
    {
        m_colors = colors;
        update();
    }
    void setStopMode(bool stop)
    {
        if (m_stop == stop) return;
        m_stop = stop;
        setAccessibleName(stop ? QStringLiteral("停止") : QStringLiteral("发送"));
        setToolTip(stop ? QStringLiteral("停止本地 AI") : QStringLiteral("发送（Enter）"));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF c = QRectF(rect()).center();
        painter.translate(c);
        painter.scale(m_scale, m_scale);
        painter.translate(-c);
        painter.setPen(Qt::NoPen);
        if (isEnabled()) {
            QLinearGradient fill(rect().topLeft(), rect().bottomRight());
            fill.setColorAt(0, m_colors.accentA);
            fill.setColorAt(1, m_colors.accentB);
            painter.setBrush(fill);
        } else {
            painter.setBrush(m_colors.disabled);
        }
        painter.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        if (m_stop) {
            painter.setBrush(Qt::white);
            painter.drawPath(art::continuousRoundedRect(QRectF(c.x() - 5.5, c.y() - 5.5, 11, 11), 2.5));
            return;
        }
        painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(c.x(), c.y() + 6.5), QPointF(c.x(), c.y() - 6));
        painter.drawPolyline(QPolygonF{{c.x() - 5, c.y() - 1}, {c.x(), c.y() - 6}, {c.x() + 5, c.y() - 1}});
    }

private:
    void animateTo(qreal target, int duration, const QEasingCurve& curve)
    {
        if (art::prefersReducedMotion()) return;
        m_press->stop();
        m_press->setStartValue(m_scale);
        m_press->setEndValue(target);
        m_press->setDuration(duration);
        m_press->setEasingCurve(curve);
        m_press->start();
    }

    Colors m_colors = colorsFor(false);
    bool m_stop = false;
    qreal m_scale = 1.0;
    QVariantAnimation* m_press = nullptr;
};

AssistantBall::AssistantBall(QWidget* anchor)
    : QWidget(anchor, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_anchor(anchor)
{
    setObjectName(QStringLiteral("assistantBall"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("助手"));
    setAccessibleDescription(QStringLiteral("点按展开助手；拖动可移动位置。"));
    resize(kBox, kBox);

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("assistantContent"));
    m_content->hide();

    m_scroll = new QScrollArea(m_content);
    m_scroll->setObjectName(QStringLiteral("assistantScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    auto* list = new QWidget(m_scroll);
    list->setObjectName(QStringLiteral("assistantMessages"));
    list->setAutoFillBackground(false);
    list->installEventFilter(this); // the history card follows the content height
    m_messages = new QVBoxLayout(list);
    m_messages->setContentsMargins(4, 4, 4, 4);
    m_messages->setSpacing(10);
    m_messages->addStretch(1); // moved by setMessageFlow so replies sit next to the capsule

    m_welcome = new QWidget(list);
    m_welcome->setObjectName(QStringLiteral("assistantWelcome"));
    auto* welcome = new QVBoxLayout(m_welcome);
    welcome->setContentsMargins(16, 14, 16, 14);
    welcome->setSpacing(8);
    welcome->addWidget(label(QStringLiteral("有什么可以帮你？"), "assistantHeadline", m_welcome));
    m_caption = label(QStringLiteral("正在检测本机的本地 AI…\n以 / 开头的指令随时可用。"), "assistantCaption", m_welcome);
    welcome->addWidget(m_caption);
    m_messages->addWidget(m_welcome);
    m_scroll->setWidget(list);

    // Quick actions next to the capsule: what they act on, then the chips.
    m_actionBar = new QWidget(m_content);
    m_actionBar->setObjectName(QStringLiteral("assistantActionBar"));
    auto* bar = new QVBoxLayout(m_actionBar);
    bar->setContentsMargins(6, 2, 6, 2);
    bar->setSpacing(6);
    m_actionContext = label(QString(), "assistantContext", m_actionBar);
    m_actionContext->setWordWrap(false);
    bar->addWidget(m_actionContext);
    m_actionRows = new QVBoxLayout;
    m_actionRows->setSpacing(6);
    bar->addLayout(m_actionRows);
    m_actionBar->installEventFilter(this);

    m_input = new QLineEdit(m_content);
    m_input->setObjectName(QStringLiteral("assistantInput"));
    m_input->setAccessibleName(QStringLiteral("助手输入框"));
    m_idlePlaceholder = QStringLiteral("输入指令，如 /插入 你好");
    m_input->setPlaceholderText(m_idlePlaceholder);
    m_input->setFrame(false);
    m_input->setMaxLength(20000 + 8); // the insert limit plus the command word
    connect(m_input, &QLineEdit::returnPressed, this, &AssistantBall::submit);
    connect(m_input, &QLineEdit::textChanged, this, &AssistantBall::updateSendState);
    m_send = new SendButton(m_content);
    connect(m_send, &QAbstractButton::clicked, this, &AssistantBall::submit);

    m_waitTimer = new QTimer(this);
    m_waitTimer->setInterval(1000);
    connect(m_waitTimer, &QTimer::timeout, this, &AssistantBall::updateWaitText);

    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &AssistantBall::tick);

    m_expandAnimation = new QVariantAnimation(this);
    connect(m_expandAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_progress = value.toReal();
        if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(m_content->graphicsEffect()))
            effect->setOpacity(qBound<qreal>(0, m_progress * 1.5, 1));
        layoutContent();
        update();
    });
    m_scaleAnimation = new QVariantAnimation(this);
    connect(m_scaleAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_scale = value.toReal();
        update();
    });
    m_snapAnimation = new QVariantAnimation(this);
    connect(m_snapAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        move(value.toPoint());
    });

    applyStyle();
    updateSendState();
}

QRect AssistantBall::anchorArea() const
{
    QWidget* area = m_area ? m_area.data() : m_anchor.data();
    if (!area) return {};
    if (!m_area)
        if (auto* window = qobject_cast<QMainWindow*>(area); window && window->centralWidget())
            area = window->centralWidget();
    return QRect(area->mapToGlobal(QPoint(0, 0)), area->size());
}

QPoint AssistantBall::clampToArea(const QPoint& centre) const
{
    const QRect area = anchorArea();
    const int margin = kBall / 2 + 8;
    const int right = qMax(area.left() + margin, area.right() - margin);
    const int bottom = qMax(area.top() + margin, area.bottom() - margin);
    return {qBound(area.left() + margin, centre.x(), right), qBound(area.top() + margin, centre.y(), bottom)};
}

QPoint AssistantBall::restingCentre() const
{
    const QRect area = anchorArea();
    return clampToArea(area.topLeft() + QPoint(qRound(m_position.x() * area.width()),
                                                qRound(m_position.y() * area.height())));
}

QRectF AssistantBall::ballBox() const
{
    if (width() <= kBox) return QRectF(0, 0, kBox, kBox);
    const qreal top = m_openUp ? height() - kBox : 0;
    return m_dockRight ? QRectF(width() - kBox, top, kBox, kBox) : QRectF(0, top, kBox, kBox);
}

QRectF AssistantBall::capsuleRect() const
{
    if (width() <= kBox) return {};
    // The capsule grows out of the ball; the spring may overshoot slightly.
    const qreal full = width() - kBox - 10;
    const qreal span = qMax<qreal>(0, full * qMin<qreal>(m_progress, 1.04));
    const qreal centreY = m_openUp ? height() - kBox / 2.0 : kBox / 2.0;
    const qreal top = centreY - kCapsuleHeight / 2.0;
    return m_dockRight ? QRectF(width() - kBox + 2 - span, top, span, kCapsuleHeight)
                       : QRectF(kBox - 2, top, span, kCapsuleHeight);
}

QRectF AssistantBall::historyRect() const
{
    if (!m_expanded || !m_scroll->isVisible() || !m_scroll->widget()) return {};
    // One card for the quick actions and the conversation: as tall as the
    // conversation's content (up to the scroll area), growing away from the capsule.
    const QRect scroll = m_scroll->geometry().translated(m_content->pos());
    const QRect bar = m_actionBar->geometry().translated(m_content->pos());
    const int content = m_scroll->widget()->layout()->sizeHint().height();
    const int shown = qMin(scroll.height(), content);
    const qreal top = m_openUp ? scroll.bottom() + 1 - shown - 8 : bar.top() - 8;
    const qreal bottom = m_openUp ? bar.bottom() + 9 : scroll.top() + shown + 8;
    return QRectF(scroll.left() - 8, top, scroll.width() + 12, bottom - top);
}

bool AssistantBall::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_scroll->widget() && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize))
        QTimer::singleShot(0, this, [this] { update(); });
    // The bar is placed by hand, so a change in what it holds (new chips, a new font) must move it.
    if (watched == m_actionBar && event->type() == QEvent::LayoutRequest)
        QTimer::singleShot(0, this, [this] {
            if (m_expanded) layoutContent();
            update();
        });
    return QWidget::eventFilter(watched, event);
}

void AssistantBall::placeAround(const QPoint& ballCentre)
{
    if (!m_expanded) {
        setGeometry(QRect(ballCentre - QPoint(kBox / 2, kBox / 2), QSize(kBox, kBox)));
        return;
    }
    const int localY = m_openUp ? kHeight - kBox / 2 : kBox / 2;
    const QPoint local(m_dockRight ? kWidth - kBox / 2 : kBox / 2, localY);
    setGeometry(QRect(ballCentre - local, QSize(kWidth, kHeight)));
}

void AssistantBall::layoutContent()
{
    // The content never covers the ball, so the ball keeps hover and drag.
    const QRect content = m_dockRight ? QRect(0, 0, qMax(0, width() - kBox + 6), height())
                                      : QRect(kBox - 6, 0, qMax(0, width() - kBox + 6), height());
    m_content->setGeometry(content);
    const QRectF capsule = capsuleRect().translated(-content.topLeft());
    const int send = 32;
    m_send->setGeometry(QRect(qRound(capsule.right()) - send - 7, qRound(capsule.center().y()) - send / 2, send, send));
    m_send->setVisible(capsule.width() > send + 24);
    m_input->setGeometry(capsule.adjusted(18, 8, -(send + 14), -8).toRect());
    m_input->setVisible(capsule.width() > 120);
    // Replies sit on the far side of the capsule from the window edge it is near;
    // the quick actions sit between them and the capsule.
    const int left = m_dockRight ? 10 : 0;
    const int width = content.width() - 10;
    const int barHeight = m_actionBar->sizeHint().height();
    const int barTop = m_openUp ? qRound(capsule.top()) - 12 - barHeight : qRound(capsule.bottom()) + 12;
    m_actionBar->setGeometry(QRect(left, barTop, width, barHeight));
    m_actionBar->setVisible(m_progress > 0.3);
    const int top = m_openUp ? 10 : barTop + barHeight + 4;
    const int bottom = m_openUp ? barTop - 4 : height() - 10;
    m_scroll->setGeometry(QRect(left, top, width, qMax(0, bottom - top)));
}

void AssistantBall::setBallVisible(bool visible)
{
    if (!visible) {
        if (m_expanded) {
            m_expandAnimation->stop();
            m_expanded = false;
            m_progress = 0;
            m_content->hide();
        }
        hide();
        return;
    }
    placeAround(restingCentre());
    show();
    raise();
}

void AssistantBall::followAnchor()
{
    if (!isVisible() || m_dragging) return;
    m_snapAnimation->stop();
    placeAround(restingCentre());
}

void AssistantBall::expand()
{
    if (m_expanded) return;
    if (!isVisible()) setBallVisible(true);
    const QPoint centre = mapToGlobal(ballBox().center().toPoint());
    const QRect area = anchorArea();
    // Open toward the page: left of a ball on the right half, and downward
    // when there is no room for the replies above it.
    m_dockRight = centre.x() >= area.center().x();
    m_openUp = centre.y() - area.top() >= kHeight - kBox / 2 || area.height() < kHeight;
    setMessageFlow(m_openUp);
    m_expanded = true;
    placeAround(centre);
    m_content->show();
    m_expandAnimation->stop();
    if (art::prefersReducedMotion()) {
        m_progress = 1.0;
        layoutContent();
    } else {
        m_content->setGraphicsEffect(new QGraphicsOpacityEffect(m_content));
        m_expandAnimation->setStartValue(0.0);
        m_expandAnimation->setEndValue(1.0);
        m_expandAnimation->setDuration(560);
        m_expandAnimation->setEasingCurve(springCurve());
        disconnect(m_expandFinished);
        m_expandFinished = connect(m_expandAnimation, &QVariantAnimation::finished, this,
                                   [this] { m_content->setGraphicsEffect(nullptr); });
        m_progress = 0;
        layoutContent();
        m_expandAnimation->start();
    }
    raise();
    activateWindow();
    m_input->setFocus();
    claimKeyboardFocus(m_input);
    update();
    emit opened();
}

void AssistantBall::collapse()
{
    if (!m_expanded) return;
    const QPoint centre = mapToGlobal(ballBox().center().toPoint());
    const auto finish = [this, centre] {
        m_expanded = false;
        m_progress = 0;
        m_content->hide();
        m_content->setGraphicsEffect(nullptr);
        placeAround(centre);
        update();
        // Give the keyboard back to the document window.
        if (m_anchor && isActiveWindow()) m_anchor->activateWindow();
    };
    m_expandAnimation->stop();
    if (art::prefersReducedMotion()) {
        finish();
        return;
    }
    m_content->setGraphicsEffect(new QGraphicsOpacityEffect(m_content));
    m_expandAnimation->setStartValue(qMin<qreal>(m_progress, 1.0));
    m_expandAnimation->setEndValue(0.0);
    m_expandAnimation->setDuration(180);
    m_expandAnimation->setEasingCurve(QEasingCurve::InCubic);
    disconnect(m_expandFinished);
    m_expandFinished = connect(m_expandAnimation, &QVariantAnimation::finished, this, finish);
    m_expandAnimation->start();
}

void AssistantBall::setDarkTheme(bool dark)
{
    m_dark = dark;
    applyStyle();
}

void AssistantBall::setWorking(bool working)
{
    m_working = working;
    if (art::prefersReducedMotion()) {
        m_energy = working ? 1.0 : 0.0;
        m_ring = working ? 0.8 : 0.0;
    }
    updateSendState();
    update();
}

void AssistantBall::ensureTimer()
{
    if (art::prefersReducedMotion() || !isVisible() || m_timer->isActive()) return;
    m_clock.restart();
    m_timer->start();
}

void AssistantBall::tick()
{
    const qreal dt = qMin<qreal>(0.1, m_clock.restart() / 1000.0);
    m_time += dt;
    const qreal target = m_working ? 1.0 : 0.0;
    m_energy += (target - m_energy) * qMin<qreal>(1.0, dt * 3.2);
    m_ring += (target - m_ring) * qMin<qreal>(1.0, dt * 4.0);
    // Only the ball animates continuously; repaint just its square.
    update(ballBox().toAlignedRect());
}

void AssistantBall::animateScale(qreal target, int duration, bool spring)
{
    if (art::prefersReducedMotion()) {
        m_scale = 1.0;
        return;
    }
    m_scaleAnimation->stop();
    m_scaleAnimation->setStartValue(m_scale);
    m_scaleAnimation->setEndValue(target);
    m_scaleAnimation->setDuration(duration);
    m_scaleAnimation->setEasingCurve(spring ? springCurve() : QEasingCurve(QEasingCurve::OutCubic));
    m_scaleAnimation->start();
}

void AssistantBall::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Colors colors = colorsFor(m_dark);

    const QRectF history = historyRect();
    if (history.height() > 20 && event->region().intersects(history.adjusted(-40, -40, 40, 40).toAlignedRect())) {
        // Frosted card behind the conversation, fading in with the capsule.
        painter.save();
        painter.setOpacity(qBound<qreal>(0, m_progress * 1.5, 1));
        const QPainterPath card = art::continuousRoundedRect(history, 24);
        art::paintSoftShadow(painter, card, 18, QPointF(0, 8), QColor(0, 0, 0, m_dark ? 120 : 34));
        painter.setPen(QPen(colors.hairline, 1));
        painter.setBrush(colors.history);
        painter.drawPath(card);
        painter.restore();
    }

    const QRectF capsule = capsuleRect();
    if (capsule.width() > 1 && event->region().intersects(capsule.adjusted(-30, -30, 30, 30).toAlignedRect())) {
        const qreal radius = kCapsuleHeight / 2.0;
        const QPainterPath shape = art::continuousRoundedRect(capsule, radius);
        painter.save();
        painter.setOpacity(qBound<qreal>(0, m_progress, 1));
        art::paintSoftShadow(painter, shape, 14, QPointF(0, 6), QColor(0, 0, 0, m_dark ? 110 : 40));
        painter.restore();
        if (m_input->hasFocus()) { // focus double ring: soft halo, then the accent line
            QColor halo = colors.accentA;
            halo.setAlphaF(0.24);
            painter.setPen(QPen(halo, 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(art::continuousRoundedRect(capsule.adjusted(-2, -2, 2, 2), radius + 2));
        }
        painter.setPen(m_input->hasFocus() ? QPen(colors.accentA, 1.6) : QPen(colors.hairline, 1));
        painter.setBrush(colors.surface);
        painter.drawPath(shape);
    }

    const QRectF box = ballBox();
    const QPointF centre = box.center();
    painter.translate(centre);
    painter.scale(m_scale, m_scale);
    painter.translate(-centre);
    art::paintBallPresence(painter, centre, kBall / 2.0, m_energy, m_dark);
    art::paintActivityArc(painter, centre, kBall / 2.0 + 6, m_time, m_ring, m_dark);
    art::paintBall(painter, QRectF(centre.x() - kBall / 2.0, centre.y() - kBall / 2.0, kBall, kBall), m_time, m_energy,
                   m_dark);
}

void AssistantBall::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_expanded) layoutContent();
}

void AssistantBall::mousePressEvent(QMouseEvent* event)
{
    const bool onBall = QLineF(event->position(), ballBox().center()).length() <= kBall / 2.0 + 8;
    if (event->button() != Qt::LeftButton || !onBall) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressOffset = m_pressGlobal - mapToGlobal(ballBox().center().toPoint());
    m_snapAnimation->stop();
    animateScale(0.9, 110, false);
}

void AssistantBall::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) {
        const bool onBall = QLineF(event->position(), ballBox().center()).length() <= kBall / 2.0 + 8;
        if (onBall != m_hover) {
            m_hover = onBall;
            setCursor(onBall ? Qt::PointingHandCursor : Qt::ArrowCursor);
            animateScale(onBall ? 1.07 : 1.0, 420, true);
        }
        return;
    }
    const QPoint global = event->globalPosition().toPoint();
    if (!m_dragging && (global - m_pressGlobal).manhattanLength() > 5) {
        m_dragging = true;
        if (m_expanded) { // drag the ball alone
            m_expandAnimation->stop();
            m_expanded = false;
            m_progress = 0;
            m_content->hide();
        }
    }
    if (m_dragging) placeAround(global - m_pressOffset);
}

void AssistantBall::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_pressed || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_pressed = false;
    animateScale(m_hover ? 1.07 : 1.0, 460, true);
    if (!m_dragging) {
        if (m_expanded) collapse();
        else expand();
        return;
    }
    m_dragging = false;
    // Free placement: the ball stays where it is dropped. Only a drop outside
    // the document page springs back to its edge. The spot is remembered as a
    // fraction of the page, so it holds when the window is resized.
    const QPoint centre = mapToGlobal(ballBox().center().toPoint());
    const QPoint settled = clampToArea(centre);
    const QRect area = anchorArea();
    if (area.width() > 0 && area.height() > 0)
        m_position = QPointF((settled.x() - area.left()) / qreal(area.width()),
                             (settled.y() - area.top()) / qreal(area.height()));
    if (settled == centre) return;
    const QPoint target = settled - QPoint(kBox / 2, kBox / 2);
    if (art::prefersReducedMotion()) {
        move(target);
        return;
    }
    m_snapAnimation->stop();
    m_snapAnimation->setStartValue(pos());
    m_snapAnimation->setEndValue(target);
    m_snapAnimation->setDuration(520);
    m_snapAnimation->setEasingCurve(springCurve());
    m_snapAnimation->start();
}

void AssistantBall::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (m_hover && !m_pressed) {
        m_hover = false;
        setCursor(Qt::ArrowCursor);
        animateScale(1.0, 420, true);
    }
}

void AssistantBall::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_draftId.isEmpty()) setDraftMode(QString(), QString(), QString()); // leave drafting first
        else collapse();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AssistantBall::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || isActiveWindow()) return;
    // Clicking elsewhere tucks the capsule away, as Spotlight does; a running
    // command keeps it open so its result stays visible.
    QTimer::singleShot(160, this, [this] {
        if (m_expanded && !isActiveWindow() && !m_working && !m_pressed) collapse();
    });
}

void AssistantBall::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureTimer();
}

void AssistantBall::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QWidget::hideEvent(event);
}

void AssistantBall::setStoppable(bool stoppable)
{
    m_stoppable = stoppable;
    updateSendState();
}

void AssistantBall::requestStop()
{
    if (m_working && m_stoppable) emit stopRequested();
}

void AssistantBall::updateWaitText()
{
    if (!m_replyBody || !m_replyText.isEmpty() || !m_replyClock.isValid()) {
        m_waitTimer->stop();
        return;
    }
    const qint64 seconds = m_replyClock.elapsed() / 1000;
    QString text = seconds < 3 ? QStringLiteral("正在思考…") : QStringLiteral("正在思考… %1 秒").arg(seconds);
    if (seconds >= 15 && !m_waitHint.isEmpty()) text += QLatin1Char('\n') + m_waitHint;
    m_replyBody->setTextFormat(Qt::PlainText);
    m_replyBody->setText(text);
    fitWidth(m_replyBody, kTextWidth);
}

void AssistantBall::submit()
{
    if (m_working && m_stoppable) { // the button is a stop button now
        emit stopRequested();
        return;
    }
    const QString text = m_input->text().trimmed();
    if (text.isEmpty() || m_working) return;
    m_input->clear();
    submitText(text);
}

void AssistantBall::submitText(const QString& text)
{
    if (m_working) return;
    if (!m_expanded) expand();
    m_welcome->hide();
    addUserMessage(text);
    const bool slash = text.trimmed().startsWith(QLatin1Char('/')) || text.trimmed().startsWith(QChar(0xFF0F));
    if (!m_draftId.isEmpty() && !slash) {
        const QString id = m_draftId;
        setDraftMode(QString(), QString(), QString());
        emit draftSubmitted(id, text);
        return;
    }
    QString error;
    if (const auto command = parseSlashCommand(text, &error)) {
        emit commandSubmitted(*command);
        return;
    }
    if (!error.isEmpty()) {
        addAssistantMessage(QStringLiteral("无法识别的指令"), error, false);
        return;
    }
    emit freeTextSubmitted(text); // for the local AI; the window answers either way
}

QVBoxLayout* AssistantBall::lastBubbleLayout(QWidget** bubbleOut) const
{
    // The newest message is the last widget row (a stretch may follow it).
    for (int i = m_messages->count() - 1; i >= 0; --i) {
        QWidget* row = m_messages->itemAt(i)->widget();
        if (!row || row == m_welcome) continue;
        auto* bubble = row->findChild<QFrame*>();
        if (bubbleOut) *bubbleOut = bubble;
        return bubble ? qobject_cast<QVBoxLayout*>(bubble->layout()) : nullptr;
    }
    return nullptr;
}

void AssistantBall::showCard(const QString& title, const QString& text, const QList<CardAction>& actions, bool ok)
{
    if (!m_expanded) expand();
    m_welcome->hide();
    addAssistantMessage(title, text, ok);
    if (actions.isEmpty()) return;
    // The new card is the last row; give it a row of buttons.
    QWidget* bubble = nullptr;
    auto* inner = lastBubbleLayout(&bubble);
    if (!inner) return;
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    QList<QPointer<QPushButton>> created;
    for (int i = 0; i < actions.size(); ++i) {
        auto* button = new QPushButton(actions[i].first, bubble);
        button->setObjectName(i == 0 ? QStringLiteral("assistantPrimary") : QStringLiteral("assistantSecondary"));
        button->setCursor(Qt::PointingHandCursor);
        created.append(button);
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    inner->addLayout(buttons);
    for (int i = 0; i < actions.size(); ++i) {
        const std::function<void()> action = actions[i].second;
        connect(created[i], &QPushButton::clicked, this, [created, action] {
            for (const QPointer<QPushButton>& button : created)
                if (button) button->setEnabled(false); // a card's choice is made once
            if (action) action();
        });
    }
}

void AssistantBall::beginReply(const QString& title, bool markdown)
{
    if (m_replyRow) endReply(QString(), true);
    if (!m_expanded) expand();
    m_welcome->hide();
    addAssistantMessage(title, QStringLiteral("正在思考…"), true);
    QWidget* bubble = nullptr;
    lastBubbleLayout(&bubble);
    QWidget* row = bubble ? bubble->parentWidget() : nullptr;
    m_replyRow = row;
    m_replyBody = row ? row->findChild<QLabel*>(QStringLiteral("assistantBody")) : nullptr;
    m_replyTitle = row ? row->findChild<QLabel*>(QStringLiteral("assistantBubbleTitle")) : nullptr;
    m_replyText.clear();
    m_replyMarkdown = markdown;
    m_replyClock.start();
    m_waitTimer->start();
}

void AssistantBall::appendReply(const QString& text)
{
    if (!m_replyBody) beginReply(QStringLiteral("本地 AI"));
    m_replyText += text;
    m_waitTimer->stop(); // the answer has started
    if (m_replyMarkdown) { // formatted as it streams: lists and tables take shape
        m_replyBody->setTextFormat(Qt::RichText);
        m_replyBody->setText(markdownToHtml(m_replyText, MarkdownTarget::Bubble));
        m_replyBody->setMinimumWidth(kTextWidth);
    } else {
        m_replyBody->setText(m_replyText); // tokens flow in as they arrive
        fitWidth(m_replyBody, kTextWidth);
    }
    m_lastMessageText = m_replyText;
    QTimer::singleShot(0, this, [this] { m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); });
}

void AssistantBall::replaceReply(const QString& text)
{
    if (!m_replyBody) beginReply(QStringLiteral("本地 AI"));
    m_replyText = text;
    m_replyBody->setText(text);
    fitWidth(m_replyBody, kTextWidth);
    m_lastMessageText = text;
}

void AssistantBall::endReply(const QString& text, bool ok)
{
    m_waitTimer->stop();
    if (!m_replyRow) {
        if (!text.trimmed().isEmpty()) addAssistantMessage(QStringLiteral("本地 AI"), text, ok);
        return;
    }
    const QString finalText = !text.trimmed().isEmpty() ? text : m_replyText;
    if (finalText.trimmed().isEmpty()) {
        // Nothing to say (for example the model went straight to a command).
        delete m_replyRow.data();
        --m_messageCount;
    } else {
        if (m_replyMarkdown && ok) {
            m_replyBody->setTextFormat(Qt::RichText);
            m_replyBody->setText(markdownToHtml(finalText, MarkdownTarget::Bubble));
            m_replyBody->setMinimumWidth(kTextWidth);
        } else {
            m_replyBody->setTextFormat(Qt::PlainText);
            m_replyBody->setText(finalText);
            fitWidth(m_replyBody, kTextWidth);
        }
        if (!ok && m_replyTitle) m_replyTitle->setText(m_replyTitle->text() + QStringLiteral(" · 没有完成"));
        m_lastMessageText = finalText;
    }
    m_replyRow = nullptr;
    m_replyBody = nullptr;
    m_replyTitle = nullptr;
    m_replyText.clear();
    QTimer::singleShot(0, this, [this] { m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); });
}

void AssistantBall::endReply(const QString& text, bool ok, const QList<ReplyAction>& actions)
{
    const QPointer<QWidget> row = m_replyRow;
    endReply(text, ok);
    // An empty reply removes its bubble; the buttons must not land on an older one.
    if (row && !actions.isEmpty()) addButtons(actions);
}

void AssistantBall::addButtons(const QList<ReplyAction>& actions)
{
    QWidget* bubble = nullptr;
    auto* inner = lastBubbleLayout(&bubble);
    if (!inner || !bubble) return;
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    QList<QPointer<QPushButton>> exclusive;
    QList<QPushButton*> created;
    for (int i = 0; i < actions.size(); ++i) {
        auto* button = new QPushButton(actions[i].title, bubble);
        button->setObjectName(i == 0 ? QStringLiteral("assistantPrimary") : QStringLiteral("assistantSecondary"));
        button->setCursor(Qt::PointingHandCursor);
        if (actions[i].once) exclusive.append(button);
        created.append(button);
        row->addWidget(button);
    }
    row->addStretch(1);
    inner->addLayout(row);
    for (int i = 0; i < actions.size(); ++i) {
        const ReplyAction action = actions[i];
        QPushButton* button = created[i];
        connect(button, &QPushButton::clicked, this, [exclusive, action, button] {
            if (action.once) {
                for (const QPointer<QPushButton>& other : exclusive)
                    if (other) other->setEnabled(false); // replace, insert, retry: one of them
            } else {
                // Repeatable (copy): confirm in place, then return to the resting label.
                const QString title = button->text();
                button->setText(QStringLiteral("✓ ") + title);
                QTimer::singleShot(1500, button, [button, title] { button->setText(title); });
            }
            if (action.run) action.run();
        });
    }
}

void AssistantBall::setStatus(const QString& text, bool ready)
{
    m_caption->setText(text + QLatin1Char('\n') + (ready ? QStringLiteral("选中文字后可以一键润色、纠错、翻译；也可以直接用中文提问。")
                                                         : QStringLiteral("点下方“本地 AI”按提示配置；以 / 开头的指令随时可用。")));
    m_idlePlaceholder = ready ? QStringLiteral("问点什么，或输入 / 使用指令") : QStringLiteral("输入指令，如 /插入 你好");
    if (m_draftId.isEmpty()) m_input->setPlaceholderText(m_idlePlaceholder);
}

void AssistantBall::setQuickActions(const QString& context, const QList<QuickAction>& actions)
{
    m_quickContext = context;
    m_quickActions = actions;
    rebuildQuickActions();
}

QStringList AssistantBall::quickActionIds() const
{
    QStringList ids;
    for (const QuickAction& action : m_quickActions) ids.append(action.id);
    return ids;
}

QString AssistantBall::quickContext() const
{
    return m_actionContext->text();
}

void AssistantBall::triggerQuickAction(const QString& id)
{
    if (m_working) return;
    emit quickActionTriggered(id);
}

void AssistantBall::rebuildQuickActions()
{
    while (QLayoutItem* item = m_actionRows->takeAt(0)) {
        if (QLayout* row = item->layout()) {
            while (QLayoutItem* chip = row->takeAt(0)) {
                if (QWidget* widget = chip->widget()) {
                    widget->hide(); // gone now, not when the deletion comes round
                    widget->deleteLater();
                }
                delete chip;
            }
        }
        delete item;
    }
    const QString context = m_draftId.isEmpty()
        ? m_quickContext
        : QStringLiteral("起草「%1」：在下面写几句要点，按回车生成（Esc 取消）").arg(m_draftTitle);
    m_actionContext->setText(m_actionContext->fontMetrics().elidedText(context, Qt::ElideRight, kWidth - kBox - 40));
    m_actionContext->setToolTip(context);
    // Chips flow into rows as wide as the bar.
    const int available = kWidth - kBox - 30;
    QHBoxLayout* row = nullptr;
    int used = 0;
    for (const QuickAction& action : m_quickActions) {
        auto* chip = new QPushButton(action.title, m_actionBar);
        chip->setObjectName(QStringLiteral("assistantChip"));
        chip->setProperty("actionId", action.id);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setEnabled(!m_working);
        const QString id = action.id;
        connect(chip, &QPushButton::clicked, this, [this, id] { triggerQuickAction(id); });
        chip->ensurePolished(); // measured with the style sheet's font and padding
        const int width = chip->sizeHint().width();
        if (!row || used + width > available) {
            row = new QHBoxLayout;
            row->setSpacing(6);
            row->addStretch(1); // chips go before it, so they stay left-aligned
            m_actionRows->addLayout(row);
            used = 0;
        }
        row->insertWidget(row->count() - 1, chip);
        // A new child of a shown bar stays hidden until a queued show, and layouts skip hidden widgets:
        // the bar would be measured without its chips and squash them once they appear.
        chip->show();
        used += width + 6;
    }
    // Lay the new chips out first, so the bar's height covers every row.
    m_actionBar->layout()->invalidate();
    m_actionBar->layout()->activate();
    m_actionBar->adjustSize();
    if (m_expanded) layoutContent();
    update();
}

void AssistantBall::setDraftMode(const QString& id, const QString& title, const QString& placeholder)
{
    m_draftId = id;
    m_draftTitle = title;
    m_input->setPlaceholderText(id.isEmpty() ? m_idlePlaceholder : placeholder);
    rebuildQuickActions();
    if (!id.isEmpty()) {
        if (!m_expanded) expand();
        m_input->setFocus();
        claimKeyboardFocus(m_input);
    }
}

void AssistantBall::addRequest(const QString& text)
{
    if (!m_expanded) expand();
    m_welcome->hide();
    addUserMessage(text);
}

void AssistantBall::showCorrections(const QString& title, const QList<CorrectionItem>& items)
{
    for (const QPointer<QPushButton>& button : m_correctionButtons)
        if (button) button->setEnabled(false); // only the newest list can still be applied
    if (m_correctionAll) m_correctionAll->setEnabled(false);
    m_correctionButtons.clear();
    if (!m_expanded) expand();
    m_welcome->hide();
    addAssistantMessage(title, QStringLiteral("找到 %1 处可能的问题。可以逐条改，也可以全部改正；改完都能撤销。").arg(items.size()), true);
    QWidget* bubble = nullptr;
    auto* inner = lastBubbleLayout(&bubble);
    if (!inner || !bubble) return;
    const Colors colors = colorsFor(m_dark);
    for (int i = 0; i < items.size(); ++i) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* text = new QLabel(bubble);
        text->setObjectName(QStringLiteral("assistantBody"));
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        text->setText(QStringLiteral("<span style=\"text-decoration:line-through\">%1</span> → <b>%2</b>"
                                     "<br><span style=\"color:%3\">%4</span>")
                          .arg(items[i].original.toHtmlEscaped(), items[i].corrected.toHtmlEscaped(),
                               colors.secondary.name(), items[i].reason.toHtmlEscaped()));
        text->setMinimumWidth(kTextWidth - 70);
        row->addWidget(text, 1);
        auto* fix = new QPushButton(QStringLiteral("改"), bubble);
        fix->setObjectName(QStringLiteral("assistantSecondary"));
        fix->setCursor(Qt::PointingHandCursor);
        connect(fix, &QPushButton::clicked, this, [this, i, fix] {
            fix->setEnabled(false);
            emit correctionRequested(i);
        });
        row->addWidget(fix, 0, Qt::AlignTop);
        inner->addLayout(row);
        m_correctionButtons.append(fix);
    }
    auto* all = new QPushButton(QStringLiteral("全部改正"), bubble);
    all->setObjectName(QStringLiteral("assistantPrimary"));
    all->setCursor(Qt::PointingHandCursor);
    connect(all, &QPushButton::clicked, this, [this, all] {
        all->setEnabled(false);
        for (const QPointer<QPushButton>& button : m_correctionButtons)
            if (button) button->setEnabled(false);
        emit allCorrectionsRequested();
    });
    inner->addWidget(all, 0, Qt::AlignLeft);
    m_correctionAll = all;
}

void AssistantBall::markCorrection(int index, bool ok)
{
    if (index < 0 || index >= m_correctionButtons.size() || !m_correctionButtons[index]) return;
    m_correctionButtons[index]->setEnabled(false);
    m_correctionButtons[index]->setText(ok ? QStringLiteral("✓ 已改") : QStringLiteral("没改成"));
    bool remaining = false;
    for (const QPointer<QPushButton>& button : m_correctionButtons)
        if (button && button->text() == QStringLiteral("改")) remaining = true;
    if (!remaining && m_correctionAll) m_correctionAll->setEnabled(false);
}

void AssistantBall::showOutcome(const AssistantCommand& command, const CommandOutcome& outcome)
{
    if (!m_expanded) expand(); // results are always shown next to the ball
    const CommandSpec* spec = findCommand(command.name);
    const QString title = spec ? spec->title : command.name;
    QString text = outcome.message;
    QString quote;
    if (outcome.ok && command.name == QStringLiteral("assistant.listCommands")) {
        QStringList lines;
        for (const CommandSpec& entry : commandCatalog()) {
            const QString example = slashExample(entry.name);
            lines << entry.title + QStringLiteral("  ") + (example.isEmpty() ? QStringLiteral("（供 AI 使用）") : example);
        }
        text = outcome.message + QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
    } else if (outcome.ok && command.name == QStringLiteral("selection.getText")) {
        quote = outcome.value.value(QStringLiteral("text")).toString();
    }
    addAssistantMessage(title, text, outcome.ok, quote);
}

void AssistantBall::addUserMessage(const QString& text)
{
    auto* row = new QWidget;
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addStretch(1);
    auto* bubble = new Bubble(true, &m_dark, row);
    auto* inner = new QVBoxLayout(bubble);
    inner->setContentsMargins(14, 9, 14, 9);
    auto* body = label(text, "assistantUserText", bubble);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fitWidth(body, kTextWidth);
    inner->addWidget(body);
    rowLayout->addWidget(bubble);
    m_lastMessageText = text;
    appendMessage(row);
}

void AssistantBall::addAssistantMessage(const QString& title, const QString& text, bool ok, const QString& quote)
{
    const Colors colors = colorsFor(m_dark);
    auto* row = new QWidget;
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto* bubble = new Bubble(false, &m_dark, row);
    auto* inner = new QVBoxLayout(bubble);
    inner->setContentsMargins(14, 10, 14, 11);
    inner->setSpacing(6);
    auto* heading = new QHBoxLayout;
    heading->setSpacing(7);
    heading->addWidget(new StatusMark(ok, colors, bubble));
    heading->addWidget(label(title, "assistantBubbleTitle", bubble), 1);
    inner->addLayout(heading);
    auto* body = label(text, "assistantBody", bubble);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fitWidth(body, kTextWidth);
    inner->addWidget(body);
    if (!quote.isEmpty()) {
        const QString shown = quote.size() > 600 ? quote.left(600) + QStringLiteral("…") : quote;
        auto* quoteFrame = new QuoteFrame(&m_dark, bubble);
        auto* quoteLayout = new QVBoxLayout(quoteFrame);
        quoteLayout->setContentsMargins(10, 8, 10, 8);
        auto* quoteLabel = label(shown, "assistantQuote", quoteFrame);
        quoteLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fitWidth(quoteLabel, kTextWidth - 26);
        quoteLayout->addWidget(quoteLabel);
        inner->addWidget(quoteFrame);
        auto* copy = new QPushButton(QStringLiteral("拷贝"), bubble);
        copy->setObjectName(QStringLiteral("assistantCopy"));
        copy->setCursor(Qt::PointingHandCursor);
        connect(copy, &QPushButton::clicked, copy, [copy, quote] {
            // Copy feedback: confirm in place, then return to the resting label.
            QGuiApplication::clipboard()->setText(quote);
            copy->setText(QStringLiteral("✓ 已拷贝"));
            QTimer::singleShot(1600, copy, [copy] { copy->setText(QStringLiteral("拷贝")); });
        });
        inner->addWidget(copy, 0, Qt::AlignLeft);
    }
    rowLayout->addWidget(bubble);
    rowLayout->addStretch(1);
    m_lastMessageText = text;
    appendMessage(row);
}

void AssistantBall::setMessageFlow(bool upward)
{
    // Chronological order either way; only the free space moves: above the
    // replies when the capsule is below them, below them when it is above.
    for (int i = 0; i < m_messages->count(); ++i) {
        if (m_messages->itemAt(i)->spacerItem()) {
            delete m_messages->takeAt(i);
            break;
        }
    }
    if (upward) m_messages->insertStretch(0, 1);
    else m_messages->addStretch(1);
}

void AssistantBall::appendMessage(QWidget* row)
{
    const bool trailingStretch = m_messages->count() > 0
        && m_messages->itemAt(m_messages->count() - 1)->spacerItem();
    m_messages->insertWidget(trailingStretch ? m_messages->count() - 1 : m_messages->count(), row);
    ++m_messageCount;
    // Keep the floating conversation short: the oldest bubbles leave first.
    int rows = 0;
    for (int i = 0; i < m_messages->count(); ++i) {
        QWidget* widget = m_messages->itemAt(i)->widget();
        if (widget && widget != m_welcome) ++rows;
    }
    for (int i = 0; rows > 40 && i < m_messages->count();) {
        QWidget* widget = m_messages->itemAt(i)->widget();
        if (!widget || widget == m_welcome) {
            ++i;
            continue;
        }
        delete m_messages->takeAt(i);
        widget->deleteLater();
        --rows;
    }
    if (!art::prefersReducedMotion()) {
        // Flowing entrance: fade in while rising a few pixels.
        auto* effect = new QGraphicsOpacityEffect(row);
        effect->setOpacity(0);
        row->setGraphicsEffect(effect);
        auto* animation = new QVariantAnimation(row);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setDuration(340);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, row, [row, effect](const QVariant& value) {
            effect->setOpacity(value.toReal());
            row->setContentsMargins(0, qRound((1.0 - value.toReal()) * 10), 0, 0);
        });
        connect(animation, &QVariantAnimation::finished, row, [row] {
            row->setGraphicsEffect(nullptr);
            row->setContentsMargins(0, 0, 0, 0);
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
    for (const int delay : {0, 360})
        QTimer::singleShot(delay, this, [this] {
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
        });
}

void AssistantBall::updateSendState()
{
    const bool stop = m_working && m_stoppable;
    m_send->setStopMode(stop);
    m_send->setEnabled(stop || (!m_working && !m_input->text().trimmed().isEmpty()));
    for (QPushButton* chip : m_actionBar->findChildren<QPushButton*>(QStringLiteral("assistantChip")))
        chip->setEnabled(!m_working);
}

void AssistantBall::applyStyle()
{
    const Colors c = colorsFor(m_dark);
    m_send->setColors(c);
    setStyleSheet(QStringLiteral(R"(
        QWidget#assistantContent QWidget { font-family: "Segoe UI Variable Text", "Microsoft YaHei UI"; }
        QLabel { background: transparent; }
        QLabel#assistantHeadline { font-size: 17px; font-weight: 600; color: %1; }
        QLabel#assistantCaption { font-size: 12px; color: %2; }
        QLabel#assistantContext { font-size: 12px; color: %2; padding-left: 2px; }
        QLabel#assistantBody, QLabel#assistantBubbleTitle { font-size: 13px; color: %1; }
        QLabel#assistantBubbleTitle { font-weight: 600; }
        QLabel#assistantUserText { font-size: 13px; color: #FFFFFF; }
        QLabel#assistantQuote { font-size: 12px; color: %1; background: transparent; }
        QPushButton#assistantChip:disabled { background: %3; color: %2; }
        QWidget#assistantWelcome { background: transparent; border: none; }
        QPushButton#assistantChip { background: %5; color: %1; border: none; border-radius: 15px; padding: 7px 10px; font-size: 12px; min-height: 16px; }
        QPushButton#assistantChip:hover { background: %6; }
        QPushButton#assistantChip:pressed { background: %4; }
        QPushButton#assistantChip:focus { border: 2px solid %7; padding: 5px 8px; }
        QPushButton#assistantCopy { background: transparent; color: %7; border: none; padding: 2px 2px; font-size: 12px; min-height: 0; }
        QPushButton#assistantPrimary { background: %7; color: #FFFFFF; border: none; border-radius: 14px; padding: 6px 14px; font-size: 12px; min-height: 16px; }
        QPushButton#assistantSecondary { background: %5; color: %1; border: 1px solid %4; border-radius: 14px; padding: 6px 14px; font-size: 12px; min-height: 16px; }
        QPushButton#assistantPrimary:disabled, QPushButton#assistantSecondary:disabled { background: %4; color: %2; }
        QPushButton#assistantPrimary:focus, QPushButton#assistantSecondary:focus { border: 2px solid %7; padding: 4px 12px; }
        QLineEdit#assistantInput { background: transparent; border: none; color: %1; font-size: 14px; selection-background-color: %7; }
        QScrollArea#assistantScroll, QWidget#assistantMessages { background: transparent; border: none; }
        QScrollArea#assistantScroll QScrollBar:vertical { width: 6px; background: transparent; margin: 2px; }
        QScrollArea#assistantScroll QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 30px; }
        QScrollArea#assistantScroll QScrollBar::add-line:vertical, QScrollArea#assistantScroll QScrollBar::sub-line:vertical { height: 0; }
        QScrollArea#assistantScroll QScrollBar::add-page:vertical, QScrollArea#assistantScroll QScrollBar::sub-page:vertical { background: none; }
    )").arg(c.text.name(), c.secondary.name(), c.quote.name(), rgba(c.hairline), c.chip.name(), c.chipHover.name(),
            c.accentA.name()));
    update();
}

} // namespace openoffice
