#include "main_window.h"

#include "assistant/local_assistant.h"
#include "assistant/markdown_html.h"
#include "engine/office_engine.h"
#include "ui/ai_settings_dialog.h"
#include "ui/assistant_art.h"
#include "ui/assistant_ball.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace openoffice {
namespace {

enum class Glyph { Home, Folder, Document, Grid, Slides, Save, Theme, Arrow };

QIcon lineIcon(Glyph glyph, const QColor& color = QColor("#5377D4"), int size = 22)
{
    QPixmap image(size * 2, size * 2);
    image.setDevicePixelRatio(2);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (glyph) {
    case Glyph::Home:
        painter.drawPolyline(QPolygonF{{3, 11}, {12, 3}, {21, 11}});
        painter.drawPolyline(QPolygonF{{5, 10}, {5, 21}, {10, 21}, {10, 14}, {14, 14}, {14, 21}, {19, 21}, {19, 10}});
        break;
    case Glyph::Folder:
        painter.drawPolyline(QPolygonF{{3, 8}, {3, 5}, {9, 5}, {11, 8}, {21, 8}, {21, 20}, {3, 20}, {3, 8}});
        painter.drawLine(QPointF(3, 10), QPointF(21, 10));
        break;
    case Glyph::Document:
        painter.drawPolyline(QPolygonF{{6, 2}, {14, 2}, {19, 7}, {19, 22}, {5, 22}, {5, 2}, {6, 2}});
        painter.drawPolyline(QPolygonF{{14, 2}, {14, 8}, {19, 8}});
        painter.drawLine(QPointF(8, 12), QPointF(16, 12));
        painter.drawLine(QPointF(8, 16), QPointF(16, 16));
        break;
    case Glyph::Grid:
        painter.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        painter.drawLine(QPointF(3, 9), QPointF(21, 9));
        painter.drawLine(QPointF(3, 15), QPointF(21, 15));
        painter.drawLine(QPointF(10, 3), QPointF(10, 21));
        break;
    case Glyph::Slides:
        painter.drawRoundedRect(QRectF(2, 3, 20, 14), 2, 2);
        painter.drawLine(QPointF(12, 17), QPointF(12, 21));
        painter.drawLine(QPointF(8, 22), QPointF(16, 22));
        painter.drawLine(QPointF(6, 8), QPointF(17, 8));
        painter.drawLine(QPointF(6, 12), QPointF(13, 12));
        break;
    case Glyph::Save:
        painter.drawPolyline(QPolygonF{{4, 3}, {17, 3}, {21, 7}, {21, 21}, {3, 21}, {3, 3}, {4, 3}});
        painter.drawRect(QRectF(7, 3, 9, 6));
        painter.drawRect(QRectF(7, 14, 10, 7));
        break;
    case Glyph::Theme:
        painter.drawEllipse(QRectF(6, 6, 12, 12));
        for (int angle = 0; angle < 360; angle += 45) {
            painter.save();
            painter.translate(12, 12);
            painter.rotate(angle);
            painter.drawLine(QPointF(0, -9), QPointF(0, -11));
            painter.restore();
        }
        break;
    case Glyph::Arrow:
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.drawPolyline(QPolygonF{{14, 6}, {20, 12}, {14, 18}});
        break;
    }
    return QIcon(image);
}

QLabel* textLabel(const QString& text, const char* objectName, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setTextFormat(Qt::PlainText);
    return label;
}

QString kindName(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Text: return QStringLiteral("DOCX 文档");
    case DocumentKind::Spreadsheet: return QStringLiteral("XLSX 表格");
    case DocumentKind::Presentation: return QStringLiteral("PPTX 演示");
    case DocumentKind::Pdf: return QStringLiteral("PDF 文档");
    default: return QStringLiteral("未知格式");
    }
}

QString extensionFor(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Spreadsheet: return QStringLiteral("xlsx");
    case DocumentKind::Presentation: return QStringLiteral("pptx");
    case DocumentKind::Pdf: return QStringLiteral("pdf");
    default: return QStringLiteral("docx");
    }
}

// Word-style names for new documents: 文档1.docx, 工作簿1.xlsx, 演示文稿1.pptx.
QString untitledStem(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Spreadsheet: return QStringLiteral("工作簿");
    case DocumentKind::Presentation: return QStringLiteral("演示文稿");
    default: return QStringLiteral("文档");
    }
}

// Standard Save/Discard/Cancel buttons with Chinese labels: no Qt translation
// catalog is shipped, so QMessageBox would otherwise show English text.
QMessageBox::StandardButton askToSaveChanges(QWidget* parent, const QString& text)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("保存修改"), text,
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, parent);
    box.setDefaultButton(QMessageBox::Cancel);
    box.button(QMessageBox::Save)->setText(QStringLiteral("保存"));
    box.button(QMessageBox::Discard)->setText(QStringLiteral("不保存"));
    box.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

bool sameFile(const QString& first, const QString& second)
{
    if (first.isEmpty() || second.isEmpty()) return false;
    const QString a = QFileInfo(first).canonicalFilePath();
    return !a.isEmpty() && a.compare(QFileInfo(second).canonicalFilePath(), Qt::CaseInsensitive) == 0;
}

QString fileSize(qint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}

QPushButton* actionButton(QAction* action, const char* name, QWidget* parent)
{
    auto* button = new QPushButton(action->icon(), action->text(), parent);
    button->setObjectName(QString::fromLatin1(name));
    button->setCursor(Qt::PointingHandCursor);
    button->setEnabled(action->isEnabled());
    button->setToolTip(action->toolTip());
    QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
    QObject::connect(action, &QAction::changed, button, [button, action] {
        button->setEnabled(action->isEnabled());
        button->setText(action->text());
        button->setToolTip(action->toolTip());
    });
    return button;
}

} // namespace

MainWindow::MainWindow(OfficeEngine* engine, QWidget* parent)
    : QMainWindow(parent), m_engine(engine), m_openedKind(DocumentKind::Unknown),
      m_pendingOperation(EngineOperation::Open)
{
    setObjectName(QStringLiteral("openOfficeMainWindow"));
    setWindowTitle(QStringLiteral("Open Office · 开发原型"));
    setWindowIcon(lineIcon(Glyph::Document));
    setMinimumSize(960, 640);
    resize(1280, 800);
    buildActions();
    buildInterface();
    m_closeAfterSaveTimer = new QTimer(this);
    m_closeAfterSaveTimer->setSingleShot(true);
    m_closeAfterSaveTimer->setInterval(5000);
    connect(m_closeAfterSaveTimer, &QTimer::timeout, this, [this] {
        if (!m_waitingForSavedState) return;
        const bool closing = m_closeAfterSave;
        resetCloseAfterSave();
        refreshCapabilities();
        showFeedback(closing
            ? QStringLiteral("保存回执已收到，但未及时确认文档状态。已恢复编辑，请重试关闭。")
            : QStringLiteral("保存回执已收到，但未及时确认文档状态。已恢复编辑，请重试打开或新建。"), true);
    });
    if (m_engine) {
        m_documentModified = m_engine->documentModified();
        connect(m_engine, &OfficeEngine::operationFinished, this,
                &MainWindow::handleOperationFinished, Qt::QueuedConnection);
        connect(m_engine, &OfficeEngine::capabilitiesChanged, this,
                &MainWindow::refreshCapabilities, Qt::QueuedConnection);
        connect(m_engine, &OfficeEngine::modifiedChanged, this, [this](bool modified) {
            m_documentModified = modified;
            updateDocumentTitle();
            if (m_waitingForSavedState && (m_closeAfterSave || m_replaceAfterSave) && !modified
                && !m_engine->documentModified()) {
                // The renderer, not a successful disk-write callback, confirms
                // that its current document state is clean.
                m_closeAfterSaveTimer->stop();
                m_waitingForSavedState = false;
                if (!m_closeAfterSave) {
                    runReplaceAfterSave();
                    return;
                }
                m_closeAfterSave = false;
                refreshCapabilities();
                close();
                return;
            }
            refreshCapabilities();
        }, Qt::QueuedConnection);
        connect(m_engine, &OfficeEngine::saveDestinationRequested, this,
                &MainWindow::answerSaveDestination, Qt::QueuedConnection);
        connect(m_engine, &OfficeEngine::editorCommandFinished, this,
                &MainWindow::handleEditorCommandFinished, Qt::QueuedConnection);
    }
    refreshCapabilities();
    applyTheme();
}

void MainWindow::buildActions()
{
    m_homeAction = new QAction(lineIcon(Glyph::Home), QStringLiteral("开始"), this);
    m_homeAction->setObjectName(QStringLiteral("homeAction"));
    connect(m_homeAction, &QAction::triggered, this, [this] { m_workspace->setCurrentWidget(m_homePage); });

    m_selectFileAction = new QAction(lineIcon(Glyph::Folder), QStringLiteral("选择本地文件"), this);
    m_selectFileAction->setObjectName(QStringLiteral("selectFileAction"));
    m_selectFileAction->setShortcut(QKeySequence::Open);
    m_selectFileAction->setToolTip(QStringLiteral("选择文件并查看基本信息（Ctrl+O）"));
    connect(m_selectFileAction, &QAction::triggered, this, &MainWindow::chooseDocument);

    m_openDocumentAction = new QAction(lineIcon(Glyph::Arrow), QStringLiteral("打开所选文件"), this);
    m_openDocumentAction->setObjectName(QStringLiteral("openDocumentAction"));
    connect(m_openDocumentAction, &QAction::triggered, this, &MainWindow::openSelectedDocument);

    m_saveDocumentAction = new QAction(lineIcon(Glyph::Save), QStringLiteral("保存"), this);
    m_saveDocumentAction->setObjectName(QStringLiteral("saveDocumentAction"));
    m_saveDocumentAction->setShortcut(QKeySequence::Save);
    connect(m_saveDocumentAction, &QAction::triggered, this, &MainWindow::saveDocument);

    m_saveAsAction = new QAction(QStringLiteral("另存为…"), this);
    m_saveAsAction->setObjectName(QStringLiteral("saveAsDocumentAction"));
    m_saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);

    m_editorPageAction = new QAction(lineIcon(Glyph::Document), QStringLiteral("编辑器"), this);
    m_editorPageAction->setObjectName(QStringLiteral("editorPageAction"));
    connect(m_editorPageAction, &QAction::triggered, this, [this] {
        if (m_documentOpened || m_pendingRequestId) m_workspace->setCurrentWidget(m_editorPage);
    });

    m_createTextAction = new QAction(QStringLiteral("新建文档"), this);
    m_createTextAction->setObjectName(QStringLiteral("createTextAction"));
    connect(m_createTextAction, &QAction::triggered, this, [this] { createDocument(DocumentKind::Text); });
    m_createSpreadsheetAction = new QAction(QStringLiteral("新建表格"), this);
    m_createSpreadsheetAction->setObjectName(QStringLiteral("createSpreadsheetAction"));
    connect(m_createSpreadsheetAction, &QAction::triggered, this, [this] { createDocument(DocumentKind::Spreadsheet); });
    m_createPresentationAction = new QAction(QStringLiteral("新建演示"), this);
    m_createPresentationAction->setObjectName(QStringLiteral("createPresentationAction"));
    connect(m_createPresentationAction, &QAction::triggered, this, [this] { createDocument(DocumentKind::Presentation); });

    m_assistantAction = new QAction(art::assistantIcon(18), QStringLiteral("助手"), this);
    m_assistantAction->setObjectName(QStringLiteral("assistantAction"));
    m_assistantAction->setCheckable(true);
    m_assistantAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    m_assistantAction->setToolTip(QStringLiteral("显示或隐藏助手悬浮球（Ctrl+Shift+A）：本地指令，尚未接入模型"));
    m_assistantAction->setChecked(true); // the ball is part of the window by default
    connect(m_assistantAction, &QAction::toggled, this, [this] { updateAssistantVisibility(); });

    m_themeAction = new QAction(lineIcon(Glyph::Theme), QStringLiteral("深色外观"), this);
    m_themeAction->setObjectName(QStringLiteral("themeAction"));
    m_themeAction->setCheckable(true);
    connect(m_themeAction, &QAction::toggled, this, [this](bool checked) {
        m_darkTheme = checked;
        m_themeAction->setText(checked ? QStringLiteral("浅色外观") : QStringLiteral("深色外观"));
        applyTheme();
    });

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(m_selectFileAction);
    fileMenu->addAction(m_openDocumentAction);
    fileMenu->addSeparator();
    auto* newMenu = fileMenu->addMenu(QStringLiteral("新建"));
    newMenu->addActions({m_createTextAction, m_createSpreadsheetAction, m_createPresentationAction});
    fileMenu->addAction(m_saveDocumentAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(QStringLiteral("退出"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(m_homeAction);
    viewMenu->addAction(m_editorPageAction);
    viewMenu->addAction(m_assistantAction);
    viewMenu->addAction(m_themeAction);
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto* aboutAction = helpMenu->addAction(QStringLiteral("关于 Open Office"));
    aboutAction->setObjectName(QStringLiteral("aboutAction"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    auto* toolbar = addToolBar(QStringLiteral("常用操作"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->addAction(m_homeAction);
    toolbar->addAction(m_editorPageAction);
    toolbar->addSeparator();
    toolbar->addAction(m_selectFileAction);
    toolbar->addAction(m_openDocumentAction);
    toolbar->addAction(m_saveDocumentAction);
    toolbar->addAction(m_saveAsAction);
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    toolbar->addAction(m_assistantAction);
    toolbar->addAction(m_themeAction);
}

void MainWindow::buildInterface()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("applicationSurface"));
    auto* horizontal = new QHBoxLayout(central);
    horizontal->setContentsMargins(0, 0, 0, 0);
    horizontal->setSpacing(0);

    auto* sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(204);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(20, 28, 20, 20);
    sideLayout->setSpacing(12);
    sideLayout->addWidget(textLabel(QStringLiteral("OPEN OFFICE"), "brandLabel", sidebar));
    sideLayout->addWidget(textLabel(QStringLiteral("你的桌面工作空间"), "mutedLabel", sidebar));
    sideLayout->addSpacing(26);
    auto* homeButton = actionButton(m_homeAction, "homeNavButton", sidebar);
    homeButton->setProperty("navActive", true);
    sideLayout->addWidget(homeButton);
    auto* localButton = actionButton(m_selectFileAction, "localFilesNavButton", sidebar);
    localButton->setText(QStringLiteral("本地文件"));
    sideLayout->addWidget(localButton);
    sideLayout->addWidget(actionButton(m_editorPageAction, "editorNavButton", sidebar));
    sideLayout->addStretch();
    auto* localNote = textLabel(QStringLiteral("本地优先\nWindows 桌面应用"), "sidebarFootnote", sidebar);
    localNote->setWordWrap(true);
    sideLayout->addWidget(localNote);
    horizontal->addWidget(sidebar);

    m_workspace = new QStackedWidget(central);
    m_workspace->setObjectName(QStringLiteral("workspaceStack"));
    m_homePage = new QWidget(m_workspace);
    auto* pageLayout = new QVBoxLayout(m_homePage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(m_homePage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("homeContent"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 22, 32, 18);
    layout->setSpacing(14);

    auto* headingLayout = new QVBoxLayout;
    headingLayout->setSpacing(6);
    auto* heading = textLabel(QStringLiteral("从一份文档开始"), "pageHeading", content);
    headingLayout->addWidget(heading);
    auto* intro = textLabel(QStringLiteral("为文档、表格和演示准备的桌面工作空间。"), "mutedLabel", content);
    headingLayout->addWidget(intro);
    layout->addLayout(headingLayout);

    auto* engineBanner = new QFrame(content);
    engineBanner->setObjectName(QStringLiteral("engineBanner"));
    auto* bannerLayout = new QVBoxLayout(engineBanner);
    bannerLayout->setContentsMargins(18, 12, 18, 12);
    bannerLayout->setSpacing(7);
    m_engineStatusLabel = textLabel(QString(), "engineStatusLabel", engineBanner);
    m_engineStatusLabel->setWordWrap(true);
    m_engineDescriptionLabel = textLabel(QString(), "engineDescriptionLabel", engineBanner);
    m_engineDescriptionLabel->setWordWrap(true);
    bannerLayout->addWidget(m_engineStatusLabel);
    bannerLayout->addWidget(m_engineDescriptionLabel);
    layout->addWidget(engineBanner);

    auto* cardsLayout = new QHBoxLayout;
    cardsLayout->setSpacing(14);
    const auto addCard = [&](const QString& title, const QString& details, Glyph glyph,
                             const QColor& color, QAction* action, const char* name) {
        auto* card = new QFrame(content);
        card->setObjectName(QStringLiteral("documentCard"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(18, 18, 18, 16);
        cardLayout->setSpacing(10);
        auto* titleLayout = new QHBoxLayout;
        titleLayout->setSpacing(11);
        auto* icon = new QLabel(card);
        icon->setPixmap(lineIcon(glyph, color, 26).pixmap(26, 26));
        icon->setFixedSize(28, 28);
        icon->setAccessibleName(title);
        titleLayout->addWidget(icon);
        titleLayout->addWidget(textLabel(title, "cardHeading", card));
        titleLayout->addStretch();
        cardLayout->addLayout(titleLayout);
        cardLayout->addWidget(textLabel(details, "mutedLabel", card));
        cardLayout->addWidget(actionButton(action, name, card));
        cardsLayout->addWidget(card, 1);
    };
    addCard(QStringLiteral("文档"), QStringLiteral("文字与排版 · DOCX"), Glyph::Document,
            QColor("#5377D4"), m_createTextAction, "createTextButton");
    addCard(QStringLiteral("表格"), QStringLiteral("数据与计算 · XLSX"), Glyph::Grid,
            QColor("#378A6C"), m_createSpreadsheetAction, "createSpreadsheetButton");
    addCard(QStringLiteral("演示"), QStringLiteral("思路与表达 · PPTX"), Glyph::Slides,
            QColor("#B97843"), m_createPresentationAction, "createPresentationButton");
    layout->addLayout(cardsLayout);

    auto* selectionHeader = new QHBoxLayout;
    selectionHeader->addWidget(textLabel(QStringLiteral("本地文件"), "sectionHeading", content));
    selectionHeader->addStretch();
    auto* selectButton = actionButton(m_selectFileAction, "selectFileButton", content);
    selectButton->setProperty("primary", true);
    selectButton->setIcon(lineIcon(Glyph::Folder, QColor("#FFFFFF")));
    selectionHeader->addWidget(selectButton);
    layout->addLayout(selectionHeader);

    auto* selectionCard = new QFrame(content);
    selectionCard->setObjectName(QStringLiteral("selectionCard"));
    auto* selectionLayout = new QVBoxLayout(selectionCard);
    selectionLayout->setContentsMargins(20, 16, 20, 16);
    selectionLayout->setSpacing(8);
    m_selectedNameLabel = textLabel(QStringLiteral("还没有选择文件"), "selectedFileNameLabel", selectionCard);
    m_selectedNameLabel->setWordWrap(true);
    m_selectedPathLabel = textLabel(QStringLiteral("选择一个 DOCX、XLSX、PPTX 或 PDF 文件。"), "selectedFilePathLabel", selectionCard);
    m_selectedPathLabel->setWordWrap(true);
    m_selectedPathLabel->setMinimumWidth(0);
    m_selectedPathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_selectedPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_selectedInfoLabel = textLabel(QStringLiteral("选择操作只读取基本信息，不会改动文件。"), "selectedFileInfoLabel", selectionCard);
    m_selectedInfoLabel->setWordWrap(true);
    auto* selectionTitle = new QHBoxLayout;
    selectionTitle->addWidget(m_selectedNameLabel, 1);
    m_selectionStateLabel = textLabel(QStringLiteral("等待选择"), "selectionStateLabel", selectionCard);
    selectionTitle->addWidget(m_selectionStateLabel);
    selectionLayout->addLayout(selectionTitle);
    selectionLayout->addWidget(m_selectedPathLabel);
    auto* selectionBottom = new QHBoxLayout;
    selectionBottom->addWidget(m_selectedInfoLabel, 1);
    selectionBottom->addStretch();
    m_openButton = actionButton(m_openDocumentAction, "openDocumentButton", selectionCard);
    selectionBottom->addWidget(m_openButton);
    selectionLayout->addLayout(selectionBottom);
    layout->addWidget(selectionCard);

    m_feedbackLabel = textLabel(m_engine && m_engine->capabilities().available
        ? QStringLiteral("选择本地文件后，点击打开进入编辑器。")
        : QStringLiteral("文件编辑能力接入后，将在此窗口内打开文档。"), "activityStatusLabel", content);
    m_feedbackLabel->setWordWrap(true);
    layout->addWidget(m_feedbackLabel);
    layout->addStretch(1);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);
    m_workspace->addWidget(m_homePage);

    m_editorPage = new QWidget(m_workspace);
    m_editorPage->setObjectName(QStringLiteral("editorHostPage"));
    m_editorLayout = new QVBoxLayout(m_editorPage);
    m_editorLayout->setContentsMargins(16, 12, 16, 12);
    m_editorTitleLabel = textLabel(QString(), "editorDocumentTitleLabel", m_editorPage);
    m_editorLayout->addWidget(m_editorTitleLabel);
    m_editorFeedbackLabel = textLabel(QString(), "editorActivityStatusLabel", m_editorPage);
    m_editorFeedbackLabel->setWordWrap(true);
    m_editorLayout->addWidget(m_editorFeedbackLabel);
    m_workspace->addWidget(m_editorPage);
    horizontal->addWidget(m_workspace, 1);
    setCentralWidget(central);
    // A floating window owned by this one, so it stays above the embedded editor.
    m_assistantBall = new AssistantBall(this);
    m_assistantBall->setArea(m_editorPage); // it belongs to the document page
    connect(m_assistantBall, &AssistantBall::commandSubmitted, this,
            [this](const AssistantCommand& command) { runAssistantCommand(command); });
    connect(m_workspace, &QStackedWidget::currentChanged, this, [this] { updateAssistantVisibility(); });

    // The local AI is detected lazily: only when the person opens the ball.
    m_localAi = new LocalAssistant(this);
    connect(m_assistantBall, &AssistantBall::freeTextSubmitted, this, &MainWindow::askLocalAi);
    connect(m_assistantBall, &AssistantBall::setupRequested, this, [this] { openAiSettings(); });
    connect(m_assistantBall, &AssistantBall::opened, this, [this] {
        if (m_localAi->state() == LocalAiState::Unknown) m_localAi->refresh();
        else m_localAi->warmUp(); // the first answer should not wait for the model to load
        refreshQuickActions();
    });
    connect(m_assistantBall, &AssistantBall::quickActionTriggered, this, &MainWindow::runQuickAction);
    connect(m_assistantBall, &AssistantBall::draftSubmitted, this, [this](const QString& id, const QString& text) {
        const TaskSpec* spec = findTask(id);
        if (!spec) return;
        whenLocalAiReady([this, spec, text] { startTask(*spec, {QString(), text, currentDocumentName()}); });
    });
    connect(m_assistantBall, &AssistantBall::correctionRequested, this, [this](int index) {
        applyCorrection(index, [this, index](bool ok) { m_assistantBall->markCorrection(index, ok); });
    });
    connect(m_assistantBall, &AssistantBall::allCorrectionsRequested, this, [this] { applyRemainingCorrections(0); });
    connect(m_assistantBall, &AssistantBall::stopRequested, this, [this] { m_localAi->cancel(); });
    showQuickActionsFor(QString());
    // Conversations: what the ball shows is kept; a new one begins with each start and each other document.
    m_conversation = ConversationStore::create(QString(), QString());
    connect(m_assistantBall, &AssistantBall::messageRecorded, this, &MainWindow::recordConversationMessage);
    connect(m_assistantBall, &AssistantBall::newConversationRequested, this, [this] {
        startConversation();
        refreshQuickActions();
    });
    connect(m_assistantBall, &AssistantBall::conversationListRequested, this, &MainWindow::showConversations);
    connect(m_assistantBall, &AssistantBall::conversationOpenRequested, this, &MainWindow::openConversation);
    connect(m_assistantBall, &AssistantBall::conversationRenameRequested, this, &MainWindow::renameConversation);
    connect(m_assistantBall, &AssistantBall::conversationDeleteRequested, this, &MainWindow::deleteConversation);
    connect(m_assistantBall, &AssistantBall::otherConversationsDeleteRequested, this, [this] {
        m_conversations.removeAllExcept(m_conversation.id);
        showConversations();
    });
    connect(m_localAi, &LocalAssistant::stateChanged, this, [this] {
        const LocalAiState state = m_localAi->state();
        m_assistantBall->setStatus(m_localAi->statusText(), state == LocalAiState::Ready || state == LocalAiState::Thinking);
        updateAssistantWorking();
        if (state == LocalAiState::Checking || state == LocalAiState::Thinking) return;
        if (state == LocalAiState::Downloading) return;
        if (m_downloadShown) {
            m_downloadShown = false;
            m_assistantBall->endReply(m_localAi->downloadStatus(), state == LocalAiState::Ready);
            m_cardOnNextState = true;
        }
        if (m_whenReady) {
            const std::function<void()> action = std::move(m_whenReady);
            m_whenReady = {};
            if (state == LocalAiState::Ready) {
                action();
                return;
            }
            m_cardOnNextState = true; // explain what is missing instead
        }
        if (state == LocalAiState::Ready && m_assistantBall->expanded()) {
            m_localAi->warmUp();
            refreshQuickActions();
        }
        if (m_cardOnNextState) {
            m_cardOnNextState = false;
            showLocalAiCard();
        }
    });
    connect(m_localAi, &LocalAssistant::replyStarted, this, [this] {
        m_assistantBall->setWaitHint(waitHint());
        m_assistantBall->beginReply(QStringLiteral("本地 AI"), true);
    });
    connect(m_localAi, &LocalAssistant::replyDelta, m_assistantBall, &AssistantBall::appendReply);
    connect(m_localAi, &LocalAssistant::replyFinished, this, [this] {
        m_conversation.modelHistory = m_localAi->conversation();
        saveConversation();
    });
    connect(m_localAi, &LocalAssistant::replyFinished, this, [this](const QString& text, bool ok) {
        m_assistantBall->endReply(text, ok);
        updateAssistantWorking();
        adviseIfSlow();
    });
    connect(m_localAi, &LocalAssistant::commandProposed, this, &MainWindow::handleProposedCommand);
    connect(m_localAi, &LocalAssistant::taskStarted, this, [this](const QString& id) {
        const TaskSpec* spec = findTask(id);
        m_assistantBall->setWaitHint(waitHint());
        m_assistantBall->beginReply(spec ? spec->title : QStringLiteral("本地 AI"),
                                    spec && spec->output == TaskOutput::Markdown);
        updateAssistantWorking();
    });
    connect(m_localAi, &LocalAssistant::taskDelta, this, [this](const QString& text) {
        const TaskSpec* spec = findTask(m_localAi->runningTask());
        if (spec && spec->output == TaskOutput::Corrections) return; // raw JSON is not for reading
        m_assistantBall->appendReply(text);
    });
    connect(m_localAi, &LocalAssistant::taskFinished, this, &MainWindow::finishTask);
    connect(m_localAi, &LocalAssistant::downloadProgress, this, [this](const QString& text) {
        if (!m_downloadShown) {
            m_downloadShown = true;
            m_assistantBall->beginReply(QStringLiteral("下载本地模型"));
        }
        m_assistantBall->replaceReply(text);
    });

    statusBar()->setSizeGripEnabled(true);
    m_buildStatusLabel = textLabel(QString(), "statusPrototypeLabel", this);
    m_attributionLabel = textLabel(QString(), "attributionStatusLabel", this);
    statusBar()->addWidget(m_buildStatusLabel);
    statusBar()->addPermanentWidget(m_attributionLabel);
}

void MainWindow::chooseDocument()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择本地办公文件"), QString(),
        QStringLiteral("办公文件 (*.docx *.xlsx *.pptx *.pdf);;Word 文档 (*.docx);;Excel 表格 (*.xlsx);;PowerPoint 演示 (*.pptx);;PDF 文档 (*.pdf)"));
    if (!path.isEmpty()) selectDocument(path);
}

void MainWindow::selectDocument(const QString& path)
{
    if (m_pendingRequestId || m_waitingForSavedState) {
        showFeedback(QStringLiteral("请等待当前文档操作结束后再选择其他文件。"), true);
        return;
    }
    const auto capabilities = m_engine ? m_engine->capabilities() : EngineCapabilities{};
    if (capabilities.available && !capabilities.open) {
        showFeedback(m_documentOpened
            ? QStringLiteral("当前窗口已打开一份文档。请在新实例中打开其他文件。")
            : QStringLiteral("当前引擎暂不可打开文档，未更改文件选择。"), true);
        return;
    }
    const QFileInfo info(path);
    const DocumentKind kind = classifyDocument(path);
    if (kind == DocumentKind::Unknown) {
        showFeedback(QStringLiteral("无法选择该文件：请使用存在且可读取的 DOCX、XLSX、PPTX 或 PDF 文件。"), true);
        return;
    }
    m_selectedPath = info.absoluteFilePath();
    m_selectedNameLabel->setText(info.fileName());
    m_selectedPathLabel->setText(m_selectedPath);
    m_selectedInfoLabel->setText(kindName(kind) + QStringLiteral("  ·  ") + fileSize(info.size())
        + QStringLiteral("  ·  修改于 ") + info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    m_selectionStateLabel->setText(QStringLiteral("文件已选择 · 尚未打开"));
    m_workspace->setCurrentWidget(m_homePage);
    refreshCapabilities();
    showFeedback(QStringLiteral("已读取文件基本信息，尚未调用编辑引擎。"));
}

void MainWindow::openSelectedDocument()
{
    if (m_pendingRequestId || m_waitingForSavedState) return;
    const auto capabilities = m_engine ? m_engine->capabilities() : EngineCapabilities{};
    if (!capabilities.available) {
        showFeedback(QStringLiteral("ONLYOFFICE 引擎尚未接入，文件未打开。"), true);
        return;
    }
    if (!capabilities.open) {
        showFeedback(m_documentOpened
            ? QStringLiteral("当前窗口已打开一份文档。请在新实例中打开其他文件。")
            : QStringLiteral("当前引擎暂不可打开文档。"), true);
        return;
    }
    if (classifyDocument(m_selectedPath) == DocumentKind::Unknown) {
        showFeedback(QStringLiteral("所选文件已不可读取，请重新选择。"), true);
        return;
    }
    if (m_documentOpened && sameFile(m_selectedPath, m_openedPath)) {
        m_workspace->setCurrentWidget(m_editorPage);
        showFeedback(QStringLiteral("该文档已在编辑器中打开。"));
        return;
    }
    bool discardChanges = false;
    if (!prepareReplacement([this] { openSelectedDocument(); }, discardChanges)) return;
    if (!attachEditorView()) return;
    const auto requestId = beginRequest(EngineOperation::Open);
    handleRequestAccepted(m_engine->requestOpen(requestId, m_selectedPath, discardChanges));
}

bool MainWindow::attachEditorView()
{
    // One editor container per session. The backend swaps document views inside
    // it; CEF view destruction belongs to the backend's lifecycle, and replacing
    // QWidget children is not a safe way to close CEF.
    if (m_editorView) return true;
    QWidget* editor = m_engine->createEditorWidget(m_editorPage);
    if (!editor) {
        showFeedback(QStringLiteral("引擎没有提供编辑视图，未进入编辑状态。"), true);
        return false;
    }
    m_editorView = editor;
    m_editorLayout->addWidget(editor, 1);
    return true;
}

void MainWindow::createDocument(DocumentKind kind)
{
    if (m_pendingRequestId || m_waitingForSavedState) return;
    const auto capabilities = m_engine ? m_engine->capabilities() : EngineCapabilities{};
    if (!capabilities.available || !m_engine->canCreate(kind)) {
        showFeedback(capabilities.available
            ? QStringLiteral("当前版本暂不支持新建此类文档。")
            : QStringLiteral("ONLYOFFICE 引擎尚未接入，无法创建文档。"), true);
        return;
    }
    bool discardChanges = false;
    if (!prepareReplacement([this, kind] { createDocument(kind); }, discardChanges)) return;
    if (!attachEditorView()) return;
    m_pendingTitle = untitledStem(kind) + QString::number(m_untitledCounts.value(static_cast<int>(kind)) + 1)
        + QLatin1Char('.') + extensionFor(kind);
    const auto requestId = beginRequest(EngineOperation::Create);
    handleRequestAccepted(m_engine->requestCreate(requestId, kind, m_pendingTitle, discardChanges));
}

void MainWindow::saveDocument()
{
    const auto capabilities = m_engine ? m_engine->capabilities() : EngineCapabilities{};
    if (m_pendingRequestId || m_waitingForSavedState || !m_documentOpened || !capabilities.available || !capabilities.save) {
        resetCloseAfterSave();
        showFeedback(QStringLiteral("当前没有可保存的编辑文档。"), true);
        return;
    }
    QString destination = m_openedPath;
    if (destination.isEmpty()) {
        // A new document has no path yet: its first save chooses one.
        destination = chooseSaveDestination(m_untitledTitle, m_openedKind, QStringLiteral("保存文档"));
        if (destination.isEmpty()) {
            resetCloseAfterSave();
            showFeedback(QStringLiteral("已取消保存。"));
            return;
        }
    }
    const auto requestId = beginRequest(EngineOperation::Save);
    handleRequestAccepted(m_engine->requestSave(requestId, destination));
}

void MainWindow::saveDocumentAs()
{
    const auto capabilities = m_engine ? m_engine->capabilities() : EngineCapabilities{};
    if (m_pendingRequestId || m_waitingForSavedState || !m_documentOpened || !capabilities.available
        || !capabilities.saveAs) {
        showFeedback(QStringLiteral("当前没有可另存的编辑文档。"), true);
        return;
    }
    const QString destination = chooseSaveDestination(m_openedPath.isEmpty() ? m_untitledTitle : m_openedPath,
                                                      m_openedKind, QStringLiteral("另存为"));
    if (destination.isEmpty()) return;
    // The session continues with the new file only after the engine confirms it.
    const auto requestId = beginRequest(EngineOperation::Save);
    handleRequestAccepted(m_engine->requestSave(requestId, destination));
}

void MainWindow::answerSaveDestination(const QString& suggestedPath, DocumentKind kind)
{
    // The embedded editor waits for this answer (its own Save As command, or
    // saving a new document from inside the editor).
    if (m_pendingRequestId || m_waitingForSavedState || !m_documentOpened) {
        m_engine->cancelSaveDestination();
        return;
    }
    const QString suggestion = QFileInfo(suggestedPath).fileName().isEmpty() ? m_untitledTitle : suggestedPath;
    const QString destination = chooseSaveDestination(suggestion, kind, QStringLiteral("保存文档"));
    if (destination.isEmpty()) {
        m_engine->cancelSaveDestination();
        showFeedback(QStringLiteral("已取消保存。"));
        return;
    }
    const auto requestId = beginRequest(EngineOperation::Save);
    handleRequestAccepted(m_engine->requestSave(requestId, destination));
}

QString MainWindow::chooseSaveDestination(const QString& suggestedPath, DocumentKind kind, const QString& caption)
{
    if (m_saveDestinationChooser) return m_saveDestinationChooser(suggestedPath, kind);
    QString start = suggestedPath.isEmpty() ? untitledStem(kind) + QStringLiteral("1.") + extensionFor(kind)
                                            : QDir::fromNativeSeparators(suggestedPath);
    if (QFileInfo(start).isRelative())
        start = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(start);
    const QString extension = extensionFor(kind);
    QFileDialog dialog(this, caption, start, kindName(kind) + QStringLiteral(" (*.") + extension + QLatin1Char(')'));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(extension); // Overwrite confirmation stays on.
    dialog.selectFile(start);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
    return dialog.selectedFiles().constFirst();
}

bool MainWindow::prepareReplacement(const std::function<void()>& retry, bool& discardChanges)
{
    discardChanges = false;
    if (m_pendingRequestId || m_waitingForSavedState) return false;
    if (!m_documentOpened || !(m_documentModified || m_engine->documentModified())) return true;
    const auto choice = askToSaveChanges(this,
        QStringLiteral("当前文档有未保存的修改。打开或新建其他文档前是否保存？"));
    if (choice == QMessageBox::Discard) {
        discardChanges = true;
        return true;
    }
    if (choice == QMessageBox::Save) {
        // Resume the open/create only after the save is confirmed and the
        // editor reports a clean document.
        m_replaceAfterSave = retry;
        saveDocument();
    }
    return false;
}

void MainWindow::runAssistantCommand(const AssistantCommand& command)
{
    runAssistantCommand(command, {}, false);
}

void MainWindow::readForAssistant(const QString& name, CommandDone done)
{
    runAssistantCommand({name, {}, CommandSource::Assistant}, std::move(done), true);
}

void MainWindow::runAssistantCommand(const AssistantCommand& command, CommandDone done, bool quiet)
{
    const auto finishNow = [this, &command, &done, quiet](const CommandOutcome& outcome) {
        finishAssistantCommand(command, outcome, done, quiet);
    };
    if (m_assistantEditorRequest || m_assistantHostRequest) {
        // Reads made for the AI take a moment; they and whatever meets them
        // wait their turn. Two of the person's own commands still never overlap.
        if (quiet || (m_assistantEditorRequest && m_assistantQuiet)) {
            m_assistantQueue.append([this, command, done, quiet] { runAssistantCommand(command, done, quiet); });
            return;
        }
        finishNow({false, QStringLiteral("assistant_busy"), QStringLiteral("上一条命令还在执行。"), {}});
        return;
    }
    const CommandOutcome valid = validateCommand(command);
    if (!valid.ok) {
        finishNow(valid);
        return;
    }
    const CommandSpec* spec = findCommand(command.name);
    if (spec->needsDocument && !m_documentOpened) {
        finishNow({false, QStringLiteral("no_document"), QStringLiteral("请先打开或新建文档。"), {}});
        return;
    }
    if (spec->target == CommandTarget::Editor) {
        if (m_pendingRequestId || m_waitingForSavedState) {
            finishNow({false, QStringLiteral("engine_busy"), QStringLiteral("文档操作进行中，请稍后再试。"), {}});
            return;
        }
        if (!m_engine || !m_engine->supportsEditorCommand(command.name)) {
            finishNow({false, QStringLiteral("unsupported_command"), QStringLiteral("当前编辑引擎不支持这个命令。"), {}});
            return;
        }
        QJsonObject engineArguments = command.arguments;
        // Markdown becomes real formatting: the editor pastes the converted HTML.
        // A model that wrote Markdown without saying so gets formatting too, not "###" in the text.
        const QString insertText = command.arguments.value(QStringLiteral("text")).toString();
        const QString format = command.arguments.value(QStringLiteral("format")).toString();
        if (command.name == QStringLiteral("text.insert")
            && (format == QStringLiteral("markdown")
                || (format.isEmpty() && command.source == CommandSource::Assistant && looksLikeMarkdown(insertText))))
            engineArguments.insert(QStringLiteral("html"), markdownToHtml(insertText));
        const quint64 requestId = ++m_nextRequestId;
        const EngineResult accepted = m_engine->requestEditorCommand(requestId, command.name, engineArguments);
        if (!accepted.ok) {
            finishNow({false, accepted.code, accepted.message, {}});
            return;
        }
        // Accepted is not done: the panel waits for the editor's own report.
        m_assistantEditorRequest = requestId;
        m_assistantCommand = command;
        m_assistantDone = std::move(done);
        m_assistantQuiet = quiet;
        m_assistantBall->setWorking(true);
        return;
    }
    if (command.name == QStringLiteral("assistant.listCommands")) {
        finishNow({true, QStringLiteral("listed"), QStringLiteral("共 %1 条命令：").arg(commandCatalog().size()),
                   {{QStringLiteral("commands"), commandCatalogJson()}}});
    } else if (command.name == QStringLiteral("document.status")) {
        QJsonObject value{{QStringLiteral("opened"), m_documentOpened}};
        QString message = QStringLiteral("当前没有打开的文档。");
        if (m_documentOpened) {
            const bool modified = m_documentModified || (m_engine && m_engine->documentModified());
            const QString name = !m_openedPath.isEmpty() ? QFileInfo(m_openedPath).fileName()
                : !m_untitledTitle.isEmpty() ? m_untitledTitle : QStringLiteral("未命名.") + extensionFor(m_openedKind);
            value.insert(QStringLiteral("name"), name); // the file name only, never the folder
            value.insert(QStringLiteral("kind"), kindName(m_openedKind));
            value.insert(QStringLiteral("saved_to_disk"), !m_openedPath.isEmpty());
            value.insert(QStringLiteral("modified"), modified);
            message = name + QStringLiteral(" · ") + kindName(m_openedKind)
                + (modified ? QStringLiteral(" · 有未保存的修改") : QStringLiteral(" · 没有未保存的修改"));
        }
        finishNow({true, QStringLiteral("status"), message, value});
    } else if (command.name == QStringLiteral("document.create")) {
        const QString word = command.arguments.value(QStringLiteral("kind")).toString();
        const DocumentKind kind = word == QStringLiteral("spreadsheet") ? DocumentKind::Spreadsheet
            : word == QStringLiteral("presentation") ? DocumentKind::Presentation : DocumentKind::Text;
        if (!m_engine || !m_engine->canCreate(kind)) {
            finishNow({false, QStringLiteral("create_unsupported"), QStringLiteral("当前版本只开放新建文字文档。"), {}});
            return;
        }
        startHostCommand(command, EngineOperation::Create, [this, kind] { createDocument(kind); });
    } else if (command.name == QStringLiteral("document.save")) {
        startHostCommand(command, EngineOperation::Save, [this] { saveDocument(); });
    }
}

void MainWindow::startHostCommand(const AssistantCommand& command, EngineOperation expected,
                                  const std::function<void()>& action)
{
    // Host commands go through the same UI paths as the toolbar, so prompts,
    // dialogs and dirty-state protection stay with the person.
    const quint64 before = m_nextRequestId;
    action();
    if (m_pendingRequestId > before && m_pendingOperation == expected) {
        m_assistantHostRequest = m_pendingRequestId;
        m_assistantCommand = command;
        m_assistantBall->setWorking(true);
        return;
    }
    if (m_pendingRequestId > before) {
        finishAssistantCommand(command, {false, QStringLiteral("deferred"),
            QStringLiteral("已先开始保存当前文档；保存完成后会继续，结果见窗口状态。"), {}});
        return;
    }
    finishAssistantCommand(command, {false, QStringLiteral("not_started"),
        QStringLiteral("没有开始：") + m_feedbackLabel->text(), {}});
}

void MainWindow::handleEditorCommandFinished(quint64 requestId, const EngineResult& result, const QJsonObject& value)
{
    if (!m_assistantEditorRequest || requestId != m_assistantEditorRequest) return;
    m_assistantEditorRequest = 0;
    const CommandDone done = std::move(m_assistantDone);
    m_assistantDone = {};
    const bool quiet = m_assistantQuiet;
    m_assistantQuiet = false;
    finishAssistantCommand(m_assistantCommand, {result.ok, result.code, result.message, value}, done, quiet);
}

void MainWindow::finishAssistantCommand(const AssistantCommand& command, const CommandOutcome& outcome,
                                        const CommandDone& done, bool quiet)
{
    // A refused overlapping command must not end the one still running.
    updateAssistantWorking();
    if (!m_assistantJournalPath.isEmpty()) {
        QFile journal(m_assistantJournalPath);
        if (journal.open(QIODevice::WriteOnly | QIODevice::Append)) {
            const QJsonObject entry{
                {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {QStringLiteral("source"), sourceName(command.source)},
                {QStringLiteral("command"), command.name},
                {QStringLiteral("ok"), outcome.ok},
                {QStringLiteral("code"), outcome.code},
            };
            journal.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
        }
    }
    if (!quiet) m_assistantBall->showOutcome(command, outcome);
    if (done) done(outcome);
    // A command the local AI asked for: its outcome goes back to the model.
    else if (command.source == CommandSource::Assistant) m_localAi->commandFinished(command, outcome);
    if (!m_assistantQueue.isEmpty() && !m_assistantEditorRequest && !m_assistantHostRequest)
        QTimer::singleShot(0, this, m_assistantQueue.takeFirst());
}

QString MainWindow::waitHint() const
{
    if (m_localAi->slowModel())
        return m_localAi->model() + QStringLiteral(" 在这台电脑上比较慢。等不及可以点右下角的 ■ 停止；"
                                                   "点“本地 AI”可以看看怎么换成更快的模型。");
    return QStringLiteral("第一次使用要先把模型读进内存，可能要等十几秒到一分钟。等不及可以点 ■ 停止。");
}

void MainWindow::adviseIfSlow()
{
    // Once per session, after an answer: say plainly why it was slow and what helps.
    if (m_speedAdviceShown || !m_localAi->slowModel()) return;
    const QString advice = m_localAi->speedAdvice();
    if (advice.isEmpty()) return;
    m_speedAdviceShown = true;
    QList<AssistantBall::CardAction> actions;
    if (!m_localAi->recommendationInstalled() && m_localAi->recommendedName() != m_localAi->model()) {
        actions.append({QStringLiteral("下载 ") + m_localAi->recommendedName(), [this] { m_localAi->downloadRecommended(); }});
    } else if (m_localAi->recommendationInstalled()) {
        const QString better = m_localAi->recommendedName();
        actions.append({QStringLiteral("改用 ") + better, [this, better] {
            m_localAi->setModel(better);
            showLocalAiCard();
        }});
    }
    m_assistantBall->showCard(QStringLiteral("回答有点慢？"), advice, actions, false);
}

void MainWindow::updateAssistantWorking()
{
    const bool thinking = m_localAi && m_localAi->state() == LocalAiState::Thinking;
    m_assistantBall->setWorking(m_assistantEditorRequest || m_assistantHostRequest || thinking);
    m_assistantBall->setStoppable(thinking);
}

QString MainWindow::documentContext() const
{
    if (!m_documentOpened) return QStringLiteral("没有打开的文档");
    const bool modified = m_documentModified || (m_engine && m_engine->documentModified());
    const QString name = !m_openedPath.isEmpty() ? QFileInfo(m_openedPath).fileName()
        : !m_untitledTitle.isEmpty() ? m_untitledTitle : QStringLiteral("未命名.") + extensionFor(m_openedKind);
    return name + QStringLiteral("（") + kindName(m_openedKind)
        + (modified ? QStringLiteral("，有未保存的修改）") : QStringLiteral("）"));
}

QString MainWindow::currentDocumentName() const
{
    if (!m_documentOpened) return {};
    return !m_openedPath.isEmpty() ? QFileInfo(m_openedPath).fileName()
        : !m_untitledTitle.isEmpty() ? m_untitledTitle : QStringLiteral("未命名.") + extensionFor(m_openedKind);
}

QString MainWindow::assistantContext(const QString& documentText, const QString& selection) const
{
    constexpr int kDocumentLimit = 8000;
    QString context = QStringLiteral("当前文档：") + documentContext();
    if (!documentText.trimmed().isEmpty()) {
        context += QStringLiteral("\n文档内容（共 ") + QString::number(documentText.size()) + QStringLiteral(" 字")
            + (documentText.size() > kDocumentLimit ? QStringLiteral("，这里只给出前 8000 字") : QString())
            + QStringLiteral("）：\n<<<\n") + documentText.left(kDocumentLimit) + QStringLiteral("\n>>>");
    } else if (m_documentOpened) {
        context += QStringLiteral("\n文档目前是空的。");
    }
    context += selection.trimmed().isEmpty()
        ? QStringLiteral("\n用户没有选中文字。")
        : QStringLiteral("\n用户选中的文字：\n<<<\n") + selection.left(4000) + QStringLiteral("\n>>>");
    return context;
}

void MainWindow::whenLocalAiReady(std::function<void()> action)
{
    switch (m_localAi->state()) {
    case LocalAiState::Ready:
        action();
        return;
    case LocalAiState::Unknown:
    case LocalAiState::Checking:
        m_whenReady = std::move(action); // done as soon as detection finishes
        if (m_localAi->state() == LocalAiState::Unknown) m_localAi->refresh();
        return;
    case LocalAiState::Thinking:
        m_assistantBall->showCard(QStringLiteral("请稍等"), QStringLiteral("本地 AI 还在处理上一个请求。"), {}, false);
        return;
    default:
        showLocalAiCard();
    }
}

void MainWindow::askLocalAi(const QString& text)
{
    whenLocalAiReady([this, text] {
        // The model answers about the real document: its text and the selection go along.
        const auto ask = [this, text](const QString& documentText, const QString& selection) {
            if (!m_localAi->ask(text, assistantContext(documentText, selection))) showLocalAiCard();
            updateAssistantWorking();
        };
        if (!m_documentOpened) {
            ask(QString(), QString());
            return;
        }
        readForAssistant(QStringLiteral("document.getText"), [this, ask, text](const CommandOutcome& document) {
            const QString documentText = document.ok ? document.value.value(QStringLiteral("text")).toString() : QString();
            readForAssistant(QStringLiteral("selection.getText"), [this, ask, text, documentText](const CommandOutcome& read) {
                const QString selection = read.ok ? read.value.value(QStringLiteral("text")).toString() : QString();
                // Instructions for the selection and requests to write something are one-step
                // tasks whose result is shown first; everything else is a conversation.
                switch (classifyRequest(text, !selection.trimmed().isEmpty())) {
                case RequestKind::ReworkSelection:
                    startTask(*findTask(QStringLiteral("rework")), {selection, text, currentDocumentName()});
                    return;
                case RequestKind::Write:
                    startTask(*findTask(QStringLiteral("write")), {QString(), text, currentDocumentName()});
                    return;
                case RequestKind::Chat:
                    break;
                }
                ask(documentText, selection);
            });
        });
    });
}

void MainWindow::refreshQuickActions()
{
    // Without a ready local AI the actions only lead to its setup; no need to read.
    if (!m_documentOpened || m_localAi->state() != LocalAiState::Ready
        || m_assistantEditorRequest || m_assistantHostRequest) {
        showQuickActionsFor(QString());
        return;
    }
    readForAssistant(QStringLiteral("selection.getText"), [this](const CommandOutcome& outcome) {
        showQuickActionsFor(outcome.ok ? outcome.value.value(QStringLiteral("text")).toString() : QString());
    });
}

void MainWindow::showQuickActionsFor(const QString& selection)
{
    QList<AssistantBall::QuickAction> actions;
    QString context;
    const QString trimmed = selection.trimmed();
    if (!trimmed.isEmpty()) {
        const QString flat = trimmed.simplified();
        context = QStringLiteral("已选中 ") + QString::number(trimmed.size()) + QStringLiteral(" 字：")
            + (flat.size() > 36 ? flat.left(36) + QStringLiteral("…") : flat);
        for (const TaskSpec* spec : tasksFor(TaskScope::Selection)) actions.append({spec->id, spec->title});
    } else {
        context = QStringLiteral("处理整篇文档，或起草新内容（选中文字后还能润色、纠错、翻译）");
        actions.append({QStringLiteral("draft"), QStringLiteral("起草…")});
        for (const TaskSpec* spec : tasksFor(TaskScope::Document)) actions.append({spec->id, spec->title});
        actions.append({QStringLiteral("setup"), QStringLiteral("本地 AI")});
    }
    m_assistantBall->setQuickActions(context, actions);
}

void MainWindow::runQuickAction(const QString& id)
{
    if (id == QStringLiteral("setup")) {
        if (m_localAi->state() == LocalAiState::Unknown) m_localAi->refresh();
        openAiSettings();
        return;
    }
    if (id == QStringLiteral("draft")) {
        QList<AssistantBall::QuickAction> actions;
        for (const TaskSpec* spec : tasksFor(TaskScope::Writing)) actions.append({spec->id, spec->title});
        actions.append({QStringLiteral("back"), QStringLiteral("‹ 返回")});
        m_assistantBall->setQuickActions(QStringLiteral("起草什么？选一种，再写几句要点"), actions);
        return;
    }
    if (id == QStringLiteral("back")) {
        m_assistantBall->setDraftMode(QString(), QString(), QString());
        refreshQuickActions();
        return;
    }
    const TaskSpec* spec = findTask(id);
    if (!spec) return;
    if (spec->scope == TaskScope::Writing) {
        m_assistantBall->setDraftMode(spec->id, spec->title, spec->placeholder);
        return;
    }
    whenLocalAiReady([this, spec] {
        if (spec->scope == TaskScope::Selection) {
            // Read the selection now: it may have changed since the bar was set.
            readForAssistant(QStringLiteral("selection.getText"), [this, spec](const CommandOutcome& outcome) {
                const QString text = outcome.ok ? outcome.value.value(QStringLiteral("text")).toString() : QString();
                if (text.trimmed().isEmpty()) {
                    m_assistantBall->showCard(spec->title,
                        QStringLiteral("请先在文档里选中要处理的文字，再点“") + spec->title + QStringLiteral("”。"), {}, false);
                    showQuickActionsFor(QString());
                    return;
                }
                m_assistantBall->addRequest(spec->title + QStringLiteral("选中的文字（") + QString::number(text.trimmed().size())
                                            + QStringLiteral(" 字）"));
                startTask(*spec, {text, QString(), currentDocumentName()});
            });
            return;
        }
        readForAssistant(QStringLiteral("document.getText"), [this, spec](const CommandOutcome& outcome) {
            const QString text = outcome.ok ? outcome.value.value(QStringLiteral("text")).toString() : QString();
            if (!outcome.ok || text.trimmed().isEmpty()) {
                m_assistantBall->showCard(spec->title, outcome.ok ? QStringLiteral("文档还是空的，先写点内容吧。") : outcome.message,
                                          {}, false);
                return;
            }
            m_assistantBall->addRequest(spec->title);
            startTask(*spec, {text, QString(), currentDocumentName()});
        });
    });
}

void MainWindow::startTask(const TaskSpec& spec, const TaskInput& input)
{
    m_lastTaskId = spec.id;
    m_lastTaskInput = input;
    if (!m_localAi->runTask(spec, input)) showLocalAiCard();
    updateAssistantWorking();
}

void MainWindow::finishTask(const QString& id, const QString& text, bool ok, const QString& error)
{
    updateAssistantWorking();
    QTimer::singleShot(0, this, [this] { adviseIfSlow(); }); // after the result, never before it
    const TaskSpec* spec = findTask(id);
    if (!spec) return;
    using Action = AssistantBall::ReplyAction;
    const auto retry = [this, spec] {
        m_assistantBall->addRequest(QStringLiteral("再来一次"));
        whenLocalAiReady([this, spec] { startTask(*spec, m_lastTaskInput); });
    };
    if (!ok) {
        m_assistantBall->endReply(error, false, {Action{QStringLiteral("重试"), retry}});
        return;
    }
    if (spec->output == TaskOutput::Rewrite) {
        const QString result = cleanRewrite(text);
        if (result.isEmpty()) {
            m_assistantBall->endReply(QStringLiteral("模型没有给出结果。"), false, {Action{QStringLiteral("重试"), retry}});
            return;
        }
        const QString original = m_lastTaskInput.text;
        // A small model sometimes hands the original back. Then there is nothing to replace:
        // try once more, told plainly; after that say so instead of offering a no-op.
        const QString preset = spec->id == QStringLiteral("rework") ? presetForInstruction(m_lastTaskInput.detail) : spec->id;
        if (rewriteUnchanged(original, result, preset)) {
            if (preset == QStringLiteral("correct")) {
                m_assistantBall->endReply(QStringLiteral("没有发现需要改的地方。"), true);
                return;
            }
            if (!m_lastTaskInput.insist) {
                m_assistantBall->endReply(QStringLiteral("这次的结果和原文几乎一样，换个说法再改一次…"), true);
                TaskInput again = m_lastTaskInput;
                again.insist = true;
                whenLocalAiReady([this, spec, again] { startTask(*spec, again); });
                return;
            }
            m_assistantBall->endReply(
                QStringLiteral("模型两次都没有真正改动这段文字（小模型有时会照抄原文）。可以换个说法再试，"
                               "比如“扩写成两倍长”“加上具体的例子”，或者用上面的快捷按钮。"),
                false, {Action{QStringLiteral("重试"), retry}});
            return;
        }
        m_assistantBall->endReply(result, true, {
            Action{QStringLiteral("替换原文"), [this, result, original] {
                runAssistantCommand({QStringLiteral("selection.replace"),
                                     {{QStringLiteral("text"), result}, {QStringLiteral("expected"), original}},
                                     CommandSource::Assistant}, {}, false);
            }},
            Action{QStringLiteral("插到后面"), [this, result] {
                runAssistantCommand({QStringLiteral("text.insert"),
                                     {{QStringLiteral("text"), result}, {QStringLiteral("position"), QStringLiteral("after")}},
                                     CommandSource::Assistant}, {}, false);
            }},
            Action{QStringLiteral("复制"), [result] { QGuiApplication::clipboard()->setText(result); }, false},
            Action{QStringLiteral("重写"), retry}});
        return;
    }
    if (spec->output == TaskOutput::Markdown) {
        const QString result = stripThinking(text).trimmed();
        if (result.isEmpty()) {
            m_assistantBall->endReply(QStringLiteral("模型没有给出结果。"), false, {Action{QStringLiteral("重试"), retry}});
            return;
        }
        QList<Action> actions{
            Action{QStringLiteral("插入文档"), [this, result] {
                runAssistantCommand({QStringLiteral("text.insert"),
                                     {{QStringLiteral("text"), result.left(20000)}, {QStringLiteral("format"), QStringLiteral("markdown")},
                                      {QStringLiteral("position"), QStringLiteral("after")}},
                                     CommandSource::Assistant}, {}, false);
            }},
            Action{QStringLiteral("复制"), [result] { QGuiApplication::clipboard()->setText(result); }, false}};
        if (spec->scope == TaskScope::Writing) actions.append(Action{QStringLiteral("重写"), retry});
        m_assistantBall->endReply(result, true, actions);
        return;
    }
    // Corrections: only fixes that can really be found in the document are offered.
    m_assistantBall->endReply(QString(), true); // the "thinking" bubble gives way to the list
    m_corrections = parseCorrections(text, m_lastTaskInput.text);
    m_correctionState = QList<int>(m_corrections.size(), 0);
    if (m_corrections.isEmpty()) {
        m_assistantBall->showCard(spec->title, QStringLiteral("没有发现明显的错别字、标点错误或语病。"), {}, true);
        return;
    }
    QList<AssistantBall::CorrectionItem> items;
    for (const Correction& correction : m_corrections)
        items.append({correction.original, correction.corrected, correction.reason});
    m_assistantBall->showCorrections(spec->title, items);
}

void MainWindow::applyCorrection(int index, std::function<void(bool)> done)
{
    if (index < 0 || index >= m_corrections.size()) return;
    if (m_correctionState[index] == 1) {
        done(true);
        return;
    }
    const Correction correction = m_corrections[index];
    runAssistantCommand({QStringLiteral("document.replaceText"),
                         {{QStringLiteral("find"), correction.original}, {QStringLiteral("replace"), correction.corrected}},
                         CommandSource::Assistant},
                        [this, index, done](const CommandOutcome& outcome) {
                            if (index < m_correctionState.size()) m_correctionState[index] = outcome.ok ? 1 : 2;
                            done(outcome.ok);
                        }, true);
}

void MainWindow::applyRemainingCorrections(int fixed)
{
    for (int i = 0; i < m_corrections.size(); ++i) {
        if (m_correctionState[i] != 0) continue;
        applyCorrection(i, [this, i, fixed](bool ok) {
            m_assistantBall->markCorrection(i, ok);
            applyRemainingCorrections(fixed + (ok ? 1 : 0));
        });
        return;
    }
    m_assistantBall->showCard(QStringLiteral("全文校对"),
        QStringLiteral("已改正 ") + QString::number(fixed) + QStringLiteral(" 处。改得不对的地方可以用撤销恢复。"), {}, true);
}

AiSettingsDialog* MainWindow::openAiSettings()
{
    if (!m_aiSettings) {
        m_aiSettings = new AiSettingsDialog(m_localAi, this);
        m_aiSettings->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_aiSettings, &AiSettingsDialog::relocateRequested, this, &MainWindow::relocateApplication);
    }
    m_aiSettings->show();
    m_aiSettings->raise();
    m_aiSettings->activateWindow();
    return m_aiSettings;
}

void MainWindow::relocateApplication()
{
    QWidget* parent = m_aiSettings ? static_cast<QWidget*>(m_aiSettings) : this;
    const QString title = QStringLiteral("迁移 Open Office");
    if (m_localAi->downloader()->active()) {
        QMessageBox::information(parent, title, QStringLiteral("正在下载模型。请先暂停下载，再迁移。"));
        return;
    }
    if (m_pendingRequestId || m_waitingForSavedState) {
        QMessageBox::information(parent, title, QStringLiteral("请等当前的文档操作结束后再迁移。"));
        return;
    }
    if (m_documentOpened && (m_documentModified || (m_engine && m_engine->documentModified()))) {
        QMessageBox::information(parent, title, QStringLiteral("当前文档还有没保存的修改。请先保存，再迁移。"));
        return;
    }
    const QString source = QCoreApplication::applicationDirPath();
    const QString start = QFileInfo(source).absolutePath();
    const QString name = QFileInfo(source).fileName();
    const QString chosen = m_relocationFolderChooser
        ? m_relocationFolderChooser(start)
        : QFileDialog::getExistingDirectory(parent, QStringLiteral("选择新位置（会在里面放一个 %1 文件夹）").arg(name), start);
    if (chosen.isEmpty()) return;
    QString error;
    const RelocationPlan plan = planRelocation(source, chosen, &error);
    if (!plan.valid()) {
        QMessageBox::warning(parent, title, error);
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const qint64 bytes = folderBytes(source);
    QApplication::restoreOverrideCursor();
    const auto gigabytes = [](qint64 value) { return QString::number(value / double(1LL << 30), 'f', 1); };
    if (!plan.sameVolume) {
        const qint64 free = QStorageInfo(chosen).bytesAvailable();
        if (free >= 0 && free < bytes + (512LL << 20)) {
            QMessageBox::warning(parent, title, QStringLiteral("软件连同模型约 %1 GB，%2 只剩 %3 GB。请换个位置，或先清理出空间。")
                                                    .arg(gigabytes(bytes), QDir::toNativeSeparators(chosen), gigabytes(free)));
            return;
        }
    }
    QMessageBox box(QMessageBox::Question, title,
                    QStringLiteral("把 Open Office（约 %1 GB，含下载的模型）从\n%2\n搬到\n%3\n\n"
                                   "Open Office 会先关闭，搬好后自动重新打开，桌面和开始菜单的快捷方式会跟着改。%4搬的时候请不要关电脑。")
                        .arg(gigabytes(bytes), QDir::toNativeSeparators(source), QDir::toNativeSeparators(plan.target),
                             plan.sameVolume ? QStringLiteral("在同一个盘里搬，几秒就好。")
                                             : QStringLiteral("要复制到另一个盘，可能要几分钟。")),
                    QMessageBox::NoButton, parent);
    box.setObjectName(QStringLiteral("relocationConfirm"));
    QPushButton* go = box.addButton(QStringLiteral("迁移并重新打开"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != go) return;
    m_relocation = plan;
    if (m_aiSettings) m_aiSettings->close();
    showFeedback(QStringLiteral("正在关闭，关闭后迁移到 %1…").arg(QDir::toNativeSeparators(plan.target)));
    QTimer::singleShot(0, this, [this] { close(); });
}

void MainWindow::dropRelocation()
{
    if (!m_relocation.valid()) return;
    m_relocation = {};
    QTimer::singleShot(0, this, [this] { showFeedback(QStringLiteral("没有关闭成功，迁移已取消。"), true); });
}

void MainWindow::startConversation()
{
    m_conversation = ConversationStore::create(m_openedPath, m_documentOpened ? currentDocumentName() : QString());
    m_localAi->setConversation({}); // the AI forgets the old one; it stays in 历史
    m_assistantBall->clearConversation();
}

void MainWindow::recordConversationMessage(const ConversationMessage& message)
{
    if (m_conversation.messages.isEmpty() && m_conversation.documentName.isEmpty() && m_documentOpened) {
        m_conversation.documentPath = m_openedPath;
        m_conversation.documentName = currentDocumentName();
    }
    m_conversation.messages.append(message);
    if (!m_conversation.titleChosen && m_conversation.title.isEmpty())
        m_conversation.title = ConversationStore::titleFrom(message.role == QStringLiteral("user") ? message.text : message.title);
    m_conversation.updated = QDateTime::currentDateTime();
    m_conversation.modelHistory = m_localAi->conversation();
    saveConversation();
}

void MainWindow::saveConversation()
{
    if (m_conversation.messages.isEmpty()) return; // an empty conversation is not kept
    if (!m_conversations.save(m_conversation))
        showFeedback(QStringLiteral("对话没能保存到 %1。").arg(QDir::toNativeSeparators(m_conversations.folder())), true);
}

void MainWindow::showConversations()
{
    const QDate today = QDate::currentDate();
    const auto when = [today](const QDateTime& time) {
        const QDateTime local = time.toLocalTime();
        const QString clock = local.toString(QStringLiteral("HH:mm"));
        if (local.date() == today) return QStringLiteral("今天 ") + clock;
        if (local.date() == today.addDays(-1)) return QStringLiteral("昨天 ") + clock;
        if (local.date().year() == today.year()) return local.toString(QStringLiteral("M月d日 ")) + clock;
        return local.toString(QStringLiteral("yyyy年M月d日"));
    };
    QList<AssistantBall::ConversationEntry> entries;
    for (const Conversation& saved : m_conversations.list())
        entries.append({saved.id, saved.title, saved.documentName, when(saved.updated), int(saved.messages.size()),
                        saved.id == m_conversation.id});
    m_assistantBall->showConversationList(entries);
}

void MainWindow::openConversation(const QString& id)
{
    if (m_assistantBall->working()) return; // the buttons are off then; an answer must finish first
    const auto loaded = m_conversations.load(id);
    if (!loaded) {
        showConversations(); // deleted meanwhile
        return;
    }
    m_conversation = *loaded;
    m_localAi->setConversation(m_conversation.modelHistory);
    m_assistantBall->restoreConversation(m_conversation.messages);
    // Say which file it was about when that is not the open document.
    if (sameFile(m_conversation.documentPath, m_openedPath)) return;
    const QString name = m_conversation.documentName.isEmpty() ? QStringLiteral("一份未保存的新文档") : m_conversation.documentName;
    QList<AssistantBall::CardAction> actions;
    const QString path = m_conversation.documentPath;
    if (!path.isEmpty() && QFileInfo::exists(path))
        actions.append({QStringLiteral("打开 ") + QFileInfo(path).fileName(), [this, path] {
                            selectDocument(path);
                            openSelectedDocument();
                        }});
    m_assistantBall->showCard(QStringLiteral("工作文件"),
                              QStringLiteral("这个对话是关于 %1 的，现在打开的是 %2。")
                                  .arg(name, m_documentOpened ? currentDocumentName() : QStringLiteral("首页")),
                              actions);
}

void MainWindow::renameConversation(const QString& id, const QString& title)
{
    if (id == m_conversation.id) {
        m_conversation.title = title;
        m_conversation.titleChosen = true;
        saveConversation();
    } else {
        m_conversations.rename(id, title);
    }
    showConversations();
}

void MainWindow::deleteConversation(const QString& id)
{
    m_conversations.remove(id);
    if (id == m_conversation.id) startConversation(); // its bubbles go too
    showConversations();
}

void MainWindow::followDocumentWithConversation(EngineOperation operation, const QString& previousPath)
{
    const QString name = currentDocumentName();
    if (operation == EngineOperation::Save) {
        // 另存为 renames the document the conversation is about; it stays the same conversation.
        if (m_conversation.documentPath.isEmpty() || sameFile(m_conversation.documentPath, previousPath)) {
            m_conversation.documentPath = m_openedPath;
            m_conversation.documentName = name;
            saveConversation();
        }
        return;
    }
    if (operation != EngineOperation::Open && operation != EngineOperation::Create) return;
    // Another document begins its own conversation (the old one is in 历史), unless this
    // conversation is about the document just opened.
    if (!m_conversation.messages.isEmpty() && !sameFile(m_openedPath, m_conversation.documentPath)) {
        startConversation();
        return;
    }
    m_conversation.documentPath = m_openedPath;
    m_conversation.documentName = name;
    saveConversation();
}

void MainWindow::showLocalAiCard()
{
    using Action = AssistantBall::CardAction;
    const auto recheck = Action{QStringLiteral("重新检测"), [this] {
        m_cardOnNextState = true;
        m_localAi->refresh();
    }};
    const auto settings = Action{QStringLiteral("打开设置"), [this] { openAiSettings(); }};
    if (m_localAi->builtin()) {
        switch (m_localAi->state()) {
        case LocalAiState::NotInstalled:
            m_assistantBall->showCard(QStringLiteral("找不到内置 AI 引擎"),
                QStringLiteral("程序目录里没有 ai-engine 文件夹，可能安装不完整。可以重新安装 Open Office；"
                               "也可以在设置的“高级”里改用本机的 Ollama。"), {settings}, false);
            return;
        case LocalAiState::NoModel: {
            const CatalogModel& model = recommendedCatalogModel(m_localAi->memory(), m_localAi->videoMemory());
            const QString memory = QString::number(m_localAi->memory() / double(1LL << 30), 'f', 0);
            const QString hardware = m_localAi->videoMemory() > 0
                ? QStringLiteral("显卡（%1 GB 显存）和内存（%2 GB）")
                      .arg(QString::number(m_localAi->videoMemory() / double(1LL << 30), 'f', 0), memory)
                : QStringLiteral("内存（%1 GB，没有可用的独立显卡）").arg(memory);
            m_assistantBall->showCard(QStringLiteral("下载一个本地模型"),
                QStringLiteral("本地 AI 需要一个模型文件，只下载一次，之后不联网也能用。按这台电脑的%1，推荐 %2（约 %3 GB，"
                               "来自魔搭 ModelScope），会放在软件文件夹里（%4）。也可以在设置里选别的模型，或把整个软件迁移到别的盘。")
                    .arg(hardware, model.title, QString::number(model.gigabytes, 'f', 1),
                         QDir::toNativeSeparators(modelFolder())),
                {Action{QStringLiteral("下载 ") + model.title, [this] { m_localAi->downloadRecommended(); }}, settings}, false);
            return;
        }
        case LocalAiState::Ready: {
            QString text = QStringLiteral("正在使用 %1（内置引擎，%2）。对话和文档内容只在这台电脑上处理。")
                               .arg(m_localAi->model(), m_localAi->engineText());
            QList<Action> actions{settings};
            const QString advice = m_localAi->speedAdvice();
            if (!advice.isEmpty()) {
                text += QLatin1Char('\n') + advice;
                if (!m_localAi->recommendationInstalled() && m_localAi->recommendedName() != m_localAi->model())
                    actions.prepend(Action{QStringLiteral("下载 ") + m_localAi->recommendedName(),
                                           [this] { m_localAi->downloadRecommended(); }});
            }
            m_assistantBall->showCard(QStringLiteral("本地 AI 已就绪"), text, actions, advice.isEmpty());
            return;
        }
        case LocalAiState::Downloading:
            m_assistantBall->showCard(QStringLiteral("正在下载模型"), m_localAi->statusText(), {settings}, true);
            return;
        case LocalAiState::Thinking:
            m_assistantBall->showCard(QStringLiteral("请稍等"), QStringLiteral("本地 AI 还在处理上一个请求。"), {}, true);
            return;
        default:
            m_cardOnNextState = true;
            m_localAi->refresh();
            return;
        }
    }
    switch (m_localAi->state()) {
    case LocalAiState::NotInstalled:
        m_assistantBall->showCard(QStringLiteral("安装本地 AI 引擎"),
            QStringLiteral("AI 助手使用免费软件 Ollama 在这台电脑上运行，对话和文档内容不会离开这台电脑。\n"
                           "1. 点“打开下载页”，下载并安装 Ollama。\n2. 装好后回到这里，点“重新检测”。"),
            {Action{QStringLiteral("打开下载页"), [] {
                 QDesktopServices::openUrl(QUrl(QStringLiteral("https://ollama.com/download/windows")));
             }}, recheck}, false);
        return;
    case LocalAiState::NotRunning:
        m_assistantBall->showCard(QStringLiteral("启动本地 AI 引擎"),
            QStringLiteral("Ollama 已经装好，但现在没有运行。点“启动 Ollama”，稍等几秒就能用。"),
            {Action{QStringLiteral("启动 Ollama"), [this] {
                 if (!m_localAi->launchOllama()) {
                     m_assistantBall->showCard(QStringLiteral("没能启动 Ollama"),
                         QStringLiteral("请从开始菜单打开 Ollama，然后点“重新检测”。"), {}, false);
                     return;
                 }
                 m_cardOnNextState = true;
                 QTimer::singleShot(4000, m_localAi, [this] { m_localAi->refresh(); });
             }}, recheck}, false);
        return;
    case LocalAiState::NoModel: {
        const ModelRecommendation model = m_localAi->recommendation(); // Ollama's own model names
        const QString memory = QString::number(m_localAi->memory() / double(1LL << 30), 'f', 0);
        const QString hardware = m_localAi->videoMemory() > 0
            ? QStringLiteral("显卡（%1 GB 显存）和内存（%2 GB）")
                  .arg(QString::number(m_localAi->videoMemory() / double(1LL << 30), 'f', 0), memory)
            : QStringLiteral("内存（%1 GB，没有可用的独立显卡）").arg(memory);
        m_assistantBall->showCard(QStringLiteral("下载一个本地模型"),
            QStringLiteral("还需要一个本地模型，只需下载一次，之后不联网也能用。按这台电脑的%1，推荐 %2，"
                           "大约 %3 GB，来自 Ollama 官方模型库。")
                .arg(hardware, model.name, QString::number(model.approximateGigabytes, 'f', 1)),
            {Action{QStringLiteral("下载 ") + model.name, [this] { m_localAi->downloadRecommended(); }}, recheck}, false);
        return;
    }
    case LocalAiState::Ready: {
        QString text = QStringLiteral("正在使用 %1。对话和文档内容只在这台电脑上处理。").arg(m_localAi->model());
        QList<Action> actions;
        for (const LocalModel& model : m_localAi->models()) {
            if (model.name == m_localAi->model()) {
                if (!model.tools) text += QStringLiteral("\n注意：这个模型不能操作文档，只能聊天。");
                continue;
            }
            if (actions.size() < 3) {
                const QString name = model.name;
                actions.append(Action{QStringLiteral("改用 ") + name, [this, name] {
                    m_localAi->setModel(name);
                    showLocalAiCard();
                }});
            }
        }
        if (!actions.isEmpty()) text += QStringLiteral("\n也可以改用本机已有的其他模型：");
        const QString advice = m_localAi->speedAdvice();
        if (!advice.isEmpty()) {
            text += QLatin1Char('\n') + advice;
            if (!m_localAi->recommendationInstalled() && m_localAi->recommendedName() != m_localAi->model())
                actions.prepend(Action{QStringLiteral("下载 ") + m_localAi->recommendedName(),
                                       [this] { m_localAi->downloadRecommended(); }});
        }
        m_assistantBall->showCard(QStringLiteral("本地 AI 已就绪"), text, actions, advice.isEmpty());
        return;
    }
    case LocalAiState::Downloading:
        m_assistantBall->showCard(QStringLiteral("正在下载模型"), m_localAi->statusText(), {}, true);
        return;
    case LocalAiState::Thinking:
        m_assistantBall->showCard(QStringLiteral("请稍等"), QStringLiteral("本地 AI 还在回答上一个问题。"), {}, true);
        return;
    default:
        m_cardOnNextState = true;
        m_localAi->refresh();
    }
}

void MainWindow::handleProposedCommand(const AssistantCommand& command)
{
    // Reading is immediate; anything that changes or saves the document waits
    // for the person's "执行" (the change stays undoable either way).
    // An unknown or malformed call is refused by validation and reported back to the model.
    const CommandSpec* spec = findCommand(command.name);
    const bool confirm = spec && (spec->changesDocument || command.name == QStringLiteral("document.save")
                                  || command.name == QStringLiteral("document.create"));
    if (!confirm) {
        runAssistantCommand(command);
        return;
    }
    QString preview;
    const QString text = command.arguments.value(QStringLiteral("text")).toString();
    const QString shown = text.size() > 300 ? text.left(300) + QStringLiteral("…") : text;
    if (command.name == QStringLiteral("text.insert")) {
        preview = (command.arguments.value(QStringLiteral("position")).toString() == QStringLiteral("after")
                       ? QStringLiteral("在当前段落后面插入：\n") : QStringLiteral("在光标处插入：\n")) + shown;
    } else if (command.name == QStringLiteral("selection.replace")) {
        preview = QStringLiteral("把选中的文字改为：\n") + shown;
    } else if (command.name == QStringLiteral("document.replaceText")) {
        preview = QStringLiteral("把文档中所有“") + command.arguments.value(QStringLiteral("find")).toString()
            + QStringLiteral("”改为“") + command.arguments.value(QStringLiteral("replace")).toString() + QStringLiteral("”。");
    } else {
        preview = spec->description;
    }
    using Action = AssistantBall::CardAction;
    m_assistantBall->showCard(QStringLiteral("助手想要") + spec->title, preview,
        {Action{QStringLiteral("执行"), [this, command] { runAssistantCommand(command); }},
         Action{QStringLiteral("不要"), [this, command] { m_localAi->commandDeclined(command); }}}, true);
}

void MainWindow::updateAssistantVisibility()
{
    if (!m_assistantBall) return;
    const bool show = isVisible() && !isMinimized() && m_assistantAction->isChecked() && m_documentOpened
        && m_workspace->currentWidget() == m_editorPage;
    if (show != m_assistantBall->isVisible()) m_assistantBall->setBallVisible(show);
}

void MainWindow::runReplaceAfterSave()
{
    auto retry = std::move(m_replaceAfterSave);
    m_replaceAfterSave = nullptr;
    resetCloseAfterSave(); // Re-enables an editor paused while waiting.
    refreshCapabilities();
    if (retry) QTimer::singleShot(0, this, retry);
}

quint64 MainWindow::beginRequest(EngineOperation operation)
{
    m_pendingOperation = operation;
    m_pendingRequestId = ++m_nextRequestId;
    refreshCapabilities();
    showFeedback(operation == EngineOperation::Close
        ? QStringLiteral("正在关闭文档…")
        : operation == EngineOperation::Save
            ? QStringLiteral("正在保存，等待编辑引擎确认完成……")
            : operation == EngineOperation::Create
                ? QStringLiteral("正在新建文档，等待编辑引擎确认就绪……")
                : QStringLiteral("正在加载，等待编辑引擎确认文档就绪……"));
    return m_pendingRequestId;
}

void MainWindow::handleRequestAccepted(const EngineResult& result)
{
    if (!result.ok) {
        if (m_pendingOperation == EngineOperation::Close) dropRelocation();
        m_pendingRequestId = 0;
        resetCloseAfterSave();
        refreshCapabilities();
        showFeedback(result.message, true);
    } else if (m_pendingOperation == EngineOperation::Open || m_pendingOperation == EngineOperation::Create) {
        // The native CEF host must be visible while its document loads. This
        // does not update document readiness or session state before completion.
        m_workspace->setCurrentWidget(m_editorPage);
    }
    // Acceptance is not completion. State and success messages are updated only
    // by a matching completion event from the backend.
}

void MainWindow::handleOperationFinished(quint64 requestId, EngineOperation operation,
                                       const EngineResult& result, const QString& path, DocumentKind kind)
{
    if (!m_pendingRequestId || requestId != m_pendingRequestId || operation != m_pendingOperation)
        return;
    m_pendingRequestId = 0;
    if (m_assistantHostRequest && requestId == m_assistantHostRequest) {
        // The assistant reports the engine's confirmed result, not the request.
        m_assistantHostRequest = 0;
        CommandOutcome outcome{result.ok, result.code, result.message, {}};
        if (result.ok && !path.isEmpty()) outcome.value.insert(QStringLiteral("file"), QFileInfo(path).fileName());
        const AssistantCommand command = m_assistantCommand;
        QTimer::singleShot(0, this, [this, command, outcome] { finishAssistantCommand(command, outcome); });
    }
    if (operation == EngineOperation::Close) {
        resetCloseAfterSave();
        if (!result.ok) {
            dropRelocation();
            refreshCapabilities();
            showFeedback(result.message, true);
            return;
        }
        // Close has no document result and must bypass open/save path guards.
        // Keep the editor host alive until this acknowledgement is received.
        m_closeHandshakeCompleted = true;
        close();
        return;
    }
    if (!result.ok) {
        resetCloseAfterSave();
        refreshCapabilities();
        showFeedback(result.message, true);
        return;
    }
    // An open/save completion must identify the actual file, not merely echo a
    // queued destination. This guard does not replace real round-trip testing.
    if ((operation != EngineOperation::Create && path.isEmpty()) || kind == DocumentKind::Unknown) {
        resetCloseAfterSave();
        refreshCapabilities();
        showFeedback(QStringLiteral("编辑引擎返回的完成信息不完整，未标记操作成功。"), true);
        return;
    }
    const QString previousPath = m_openedPath;
    m_openedPath = path;
    m_openedKind = kind;
    if (operation == EngineOperation::Create) {
        m_untitledTitle = m_pendingTitle;
        ++m_untitledCounts[static_cast<int>(kind)];
    }
    m_documentOpened = true;
    m_documentModified = m_engine->documentModified();
    updateDocumentTitle();
    refreshCapabilities();
    followDocumentWithConversation(operation, previousPath);
    if (operation == EngineOperation::Save) {
        if ((m_closeAfterSave || m_replaceAfterSave) && m_documentModified) {
            awaitSavedState();
            return;
        }
        showFeedback(m_documentModified
            ? QStringLiteral("保存已完成，但仍有后续修改尚未保存。")
            : result.code == QStringLiteral("unchanged") ? result.message
            : QStringLiteral("编辑引擎已确认文档保存完成。"));
        const bool mayClose = m_closeAfterSave && !m_documentModified;
        m_closeAfterSave = false;
        if (mayClose) close();
        else if (m_replaceAfterSave && !m_documentModified) runReplaceAfterSave();
    } else {
        if (!m_selectedPath.isEmpty())
            m_selectionStateLabel->setText(sameFile(m_selectedPath, path)
                ? QStringLiteral("文档已由编辑引擎加载") : QStringLiteral("文件已选择 · 尚未打开"));
        m_workspace->setCurrentWidget(m_editorPage);
        updateAssistantVisibility();
        showFeedback(operation == EngineOperation::Create
            ? QStringLiteral("已新建文档，编辑引擎已确认就绪。")
            : QStringLiteral("编辑引擎已确认文档就绪。"));
    }
}

void MainWindow::resetCloseAfterSave()
{
    m_closeAfterSave = false;
    m_replaceAfterSave = nullptr;
    m_waitingForSavedState = false;
    if (m_closeAfterSaveTimer) m_closeAfterSaveTimer->stop();
    if (m_editorPausedForClose && m_editorView)
        m_editorView->setEnabled(m_editorWasEnabledBeforeClose);
    m_editorPausedForClose = false;
}

void MainWindow::awaitSavedState()
{
    m_waitingForSavedState = true;
    if (m_editorView && !m_editorPausedForClose) {
        m_editorWasEnabledBeforeClose = m_editorView->isEnabled();
        m_editorPausedForClose = true;
        m_editorView->setEnabled(false);
    }
    m_closeAfterSaveTimer->start();
    refreshCapabilities();
    showFeedback(m_closeAfterSave
        ? QStringLiteral("保存已完成，正在等待编辑器确认文档状态后关闭…")
        : QStringLiteral("保存已完成，正在等待编辑器确认文档状态…"));
}

void MainWindow::updateDocumentTitle()
{
    if (!m_documentOpened) return;
    const auto name = !m_openedPath.isEmpty() ? QFileInfo(m_openedPath).fileName()
        : !m_untitledTitle.isEmpty() ? m_untitledTitle
        : QStringLiteral("未命名.") + extensionFor(m_openedKind);
    m_editorTitleLabel->setText(name + (m_documentModified ? QStringLiteral(" · 未保存") : QString()));
    setWindowModified(m_documentModified);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // After the first layout pass, so the ball docks to the final geometry.
    QTimer::singleShot(0, this, [this] { updateAssistantVisibility(); });
}

void MainWindow::hideEvent(QHideEvent* event)
{
    m_assistantBall->setBallVisible(false);
    QMainWindow::hideEvent(event);
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    m_assistantBall->followAnchor();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    m_assistantBall->followAnchor();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_closeHandshakeCompleted) {
        m_closeHandshakeCompleted = false;
        event->accept();
        return;
    }
    if (m_waitingForSavedState) {
        showFeedback(QStringLiteral("保存已完成，正在等待编辑器确认文档状态后关闭…"));
        event->ignore();
        return;
    }
    if (m_pendingRequestId) {
        showFeedback(m_pendingOperation == EngineOperation::Close
            ? QStringLiteral("正在关闭文档…")
            : QStringLiteral("请等待当前文档操作结束后再关闭窗口。"),
            m_pendingOperation != EngineOperation::Close);
        event->ignore();
        return;
    }
    bool discardChanges = false;
    if (m_documentOpened && (m_documentModified || (m_engine && m_engine->documentModified()))) {
        const auto choice = askToSaveChanges(this, QStringLiteral("当前文档有未保存的修改。关闭前是否保存？"));
        if (choice != QMessageBox::Discard) {
            event->ignore();
            if (choice != QMessageBox::Save) dropRelocation();
            if (choice == QMessageBox::Save) {
                m_closeAfterSave = true;
                saveDocument();
            }
            return;
        }
        discardChanges = true;
    }
    if (m_engine && m_engine->needsCloseHandshake()) {
        event->ignore();
        const auto requestId = beginRequest(EngineOperation::Close);
        handleRequestAccepted(m_engine->requestClose(requestId, discardChanges));
        return;
    }
    resetCloseAfterSave();
    event->accept();
}

void MainWindow::refreshCapabilities()
{
    const auto capabilities = m_engine ? m_engine->capabilities() : EngineCapabilities{};
    const bool ready = capabilities.available;
    setWindowTitle(ready ? QStringLiteral("Open Office[*] · 开发版")
                         : QStringLiteral("Open Office[*] · 开发原型"));
    const bool idle = m_pendingRequestId == 0 && !m_waitingForSavedState;
    const bool sessionOpenLocked = ready && !capabilities.open && m_documentOpened;
    const QString openUnavailable = sessionOpenLocked
        ? QStringLiteral("当前窗口已打开一份文档。请在新实例中打开其他文件。")
        : QStringLiteral("当前引擎暂不可打开文档。");
    m_selectFileAction->setEnabled(idle && (!ready || capabilities.open));
    m_openDocumentAction->setEnabled(ready && idle && capabilities.open && !m_selectedPath.isEmpty());
    m_saveDocumentAction->setEnabled(ready && idle && capabilities.save && m_documentOpened);
    m_saveAsAction->setEnabled(ready && idle && capabilities.saveAs && m_documentOpened);
    m_editorPageAction->setEnabled(m_documentOpened || m_pendingRequestId);
    const auto creatable = [this](DocumentKind kind) { return m_engine && m_engine->canCreate(kind); };
    m_createTextAction->setEnabled(ready && idle && creatable(DocumentKind::Text));
    m_createSpreadsheetAction->setEnabled(ready && idle && creatable(DocumentKind::Spreadsheet));
    m_createPresentationAction->setEnabled(ready && idle && creatable(DocumentKind::Presentation));
    const QString unavailable = QStringLiteral("ONLYOFFICE 引擎尚未接入，此功能暂不可用。");
    for (auto* action : {m_openDocumentAction, m_saveDocumentAction, m_saveAsAction, m_createTextAction,
                         m_createSpreadsheetAction, m_createPresentationAction}) {
        action->setToolTip(ready ? action->text() : unavailable);
    }
    m_editorPageAction->setToolTip(QStringLiteral("返回当前文档"));
    if (ready) {
        if (!creatable(DocumentKind::Spreadsheet))
            m_createSpreadsheetAction->setToolTip(QStringLiteral("当前版本暂不支持新建表格。"));
        if (!creatable(DocumentKind::Presentation))
            m_createPresentationAction->setToolTip(QStringLiteral("当前版本暂不支持新建演示。"));
    }
    m_selectFileAction->setToolTip(ready && !capabilities.open ? openUnavailable : m_selectFileAction->text());
    if (ready && !capabilities.open) m_openDocumentAction->setToolTip(openUnavailable);
    m_buildStatusLabel->setText(ready ? QStringLiteral("开发版") : QStringLiteral("开发原型"));
    m_attributionLabel->setText(ready
        ? QStringLiteral("基于 ONLYOFFICE · 原开发者 Ascensio System SIA")
        : QStringLiteral("ONLYOFFICE 接入准备中 · 未包含上游引擎"));
    m_engineStatusLabel->setText(ready ? QStringLiteral("开发版 · ONLYOFFICE 引擎已连接")
                                       : QStringLiteral("开发原型 · ONLYOFFICE 引擎未接入"));
    m_engineDescriptionLabel->setText(!ready
        ? QStringLiteral("当前可选择本地文件；编辑、新建和保存暂不可用。")
        : sessionOpenLocked ? openUnavailable
        : QStringLiteral("文档操作通过当前引擎执行，加载与保存结果以引擎确认为准。"));
}

void MainWindow::showFeedback(const QString& message, bool error)
{
    m_feedbackLabel->setText(message);
    m_editorFeedbackLabel->setText(message);
    m_feedbackLabel->setProperty("error", error);
    m_feedbackLabel->style()->unpolish(m_feedbackLabel);
    m_feedbackLabel->style()->polish(m_feedbackLabel);
}

void MainWindow::showAbout()
{
    QMessageBox dialog(this);
    dialog.setWindowTitle(QStringLiteral("关于 Open Office"));
    dialog.setTextFormat(Qt::PlainText);
    const bool ready = m_engine && m_engine->capabilities().available;
    dialog.setText(ready ? QStringLiteral("Open Office\nWindows 桌面开发版")
                         : QStringLiteral("Open Office\nWindows 桌面开发原型"));
    const QString engineInfo = ready ? QStringLiteral(
        "本程序已接入 ONLYOFFICE 编辑引擎，当前可用操作由引擎能力决定。文档加载与保存结果以引擎确认为准。\n\n"
        "ONLYOFFICE 开源组件原开发者：Ascensio System SIA。保留原开发者署名及适用的许可信息。\n\n") : QStringLiteral(
        "这是本项目自己的原生程序入口。当前未包含 ONLYOFFICE 编辑引擎，不能编辑或保存办公文件。\n\n"
        "计划基于 ONLYOFFICE 开源组件接入文档、表格与演示能力，并保留原开发者署名及适用的许可信息。\n\n");
    dialog.setInformativeText(engineInfo + QStringLiteral(
        "ONLYOFFICE is a trademark of Ascensio System SIA.\n"
        "本项目不是 ONLYOFFICE 或 Apache OpenOffice 的官方发行版本。\n\n"
        "本程序使用 Qt；第三方许可信息见项目 THIRD_PARTY_NOTICES.md。"));
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
}

void MainWindow::applyTheme()
{
    const QString background = m_darkTheme ? "#202630" : "#F6F7FA";
    const QString panel = m_darkTheme ? "#29313D" : "#FFFFFF";
    const QString sidebar = m_darkTheme ? "#242C37" : "#F0F2F6";
    const QString text = m_darkTheme ? "#EDF1F7" : "#243247";
    const QString muted = m_darkTheme ? "#B3BDCB" : "#657186";
    const QString border = m_darkTheme ? "#3A4657" : "#E2E7F0";
    const QString hover = m_darkTheme ? "#354359" : "#EAF0FC";
    const QString banner = m_darkTheme ? "#293A56" : "#EAF0FC";
    const QString accentText = m_darkTheme ? "#C0D2FF" : "#355994";
    const QString disabledBg = m_darkTheme ? "#303946" : "#F1F3F7";
    const QString disabledText = m_darkTheme ? "#8492A6" : "#8792A3";

    QString sheet = QStringLiteral(R"(
        QMainWindow, QWidget#applicationSurface, QWidget#homeContent, QWidget#editorHostPage { background: %1; color: %4; }
        QWidget { font-family: "Microsoft YaHei UI", "Segoe UI"; font-size: 13px; color: %4; }
        QFrame#sidebar { background: %3; border: none; }
        QScrollArea { background: %1; border: none; }
        QLabel { background: transparent; }
        QLabel#brandLabel { font-size: 17px; font-weight: 700; letter-spacing: 1px; }
        QLabel#pageHeading { font-size: 28px; font-weight: 600; }
        QLabel#eyebrowLabel { color: %5; font-size: 12px; }
        QLabel#mutedLabel, QLabel#sidebarFootnote, QLabel#selectedFilePathLabel,
        QLabel#selectedFileInfoLabel, QLabel#activityStatusLabel { color: %5; }
        QLabel#sidebarFootnote { font-size: 12px; line-height: 1.5; }
        QLabel#sectionHeading, QLabel#selectedFileNameLabel { font-size: 15px; font-weight: 600; }
        QLabel#cardHeading { font-size: 19px; font-weight: 600; }
        QFrame#documentCard, QFrame#selectionCard { background: %2; border: none; border-radius: 12px; }
        QFrame#engineBanner { background: %8; border: none; border-radius: 10px; }
        QLabel#engineStatusLabel { color: %9; font-weight: 600; }
        QLabel#engineDescriptionLabel { color: %9; font-size: 12px; }
        QLabel#selectionStateLabel { color: %5; font-size: 12px; }
        QLabel#activityStatusLabel[error="true"] { color: %12; }
        QPushButton { background: %2; border: 1px solid %6; border-radius: 7px; padding: 8px 13px; min-height: 19px; }
        QPushButton:hover { background: %7; border-color: #8FA8DF; }
        QPushButton:pressed { background: %8; }
        QPushButton:focus { border: 2px solid #779BE5; padding: 7px 12px; }
        QPushButton:disabled { color: %11; background: %10; border: 1px solid %10; }
        QPushButton[primary="true"] { background: #446FC9; color: #FFFFFF; border-color: #446FC9; }
        QPushButton[primary="true"]:hover { background: #365EB3; }
        QPushButton#homeNavButton, QPushButton#localFilesNavButton, QPushButton#editorNavButton { text-align: left; padding: 10px 12px; border: none; background: transparent; }
        QPushButton#homeNavButton { background: %8; color: %9; font-weight: 600; }
        QPushButton#localFilesNavButton:hover, QPushButton#editorNavButton:hover { background: %7; }
        QPushButton#editorNavButton:disabled { color: %11; background: transparent; }
        QMenuBar, QToolBar, QStatusBar { background: %2; color: %4; border: none; }
        QMenuBar { padding: 2px 8px; }
        QMenuBar::item { padding: 6px 12px; background: transparent; }
        QMenuBar::item:selected { background: %7; border-radius: 4px; }
        QMenu { background: %2; border: 1px solid %6; padding: 5px; }
        QMenu::item { padding: 7px 30px 7px 22px; }
        QMenu::item:selected { background: %7; }
        QMenu::item:disabled { color: %11; }
        QToolBar { padding: 5px 12px; spacing: 6px; border-bottom: 1px solid %6; }
        QToolBar::separator { background: %6; width: 1px; margin: 5px 7px; }
        QToolButton { border: 1px solid transparent; border-radius: 5px; padding: 6px 9px; background: transparent; }
        QToolButton:hover { background: %7; }
        QToolButton:focus { border-color: #779BE5; }
        QToolButton:disabled { color: %11; }
        QStatusBar { border-top: 1px solid %6; padding: 3px 12px; }
        QStatusBar::item { border: none; }
        QStatusBar QLabel { color: %5; font-size: 11px; }
        QScrollBar:vertical { width: 10px; background: %1; }
        QScrollBar::handle:vertical { min-height: 28px; background: %6; border-radius: 5px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
        QMessageBox { background: %2; }
    )").arg(background, panel, sidebar, text, muted, border, hover, banner, accentText)
       .arg(disabledBg, disabledText, m_darkTheme ? "#FFB9AE" : "#B94C3E");
    setStyleSheet(sheet);
    if (m_assistantBall) m_assistantBall->setDarkTheme(m_darkTheme);
    if (m_engine) m_engine->setEditorAppearance(m_darkTheme);
    if (m_assistantAction) m_assistantAction->setIcon(art::assistantIcon(18, m_darkTheme));
}

} // namespace openoffice
