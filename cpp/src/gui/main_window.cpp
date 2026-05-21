#include "zautomate/gui/main_window.hpp"

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <future>
#include <string>
#include <thread>
#include <utility>

#include <QDateTime>
#include <QApplication>
#include <QStyledItemDelegate>
#include <QEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QThreadPool>
#include <QSplitter>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolBar>
#include <QGridLayout>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>
#include <QIcon>

#include "zautomate/logger.hpp"
#include "zautomate/gui/easter_egg_dialog.hpp"

namespace zautomate {

namespace {

QString cart_type_label(int type) {
    switch (type) {
    case 0:
        return "PSA";
    case 1:
        return "Underwriting";
    case 2:
        return "StationID";
    case 3:
        return "Promotion";
    default:
        return QString::number(type);
    }
}

QString format_item_line(const Cart& cart) {
    return QString("%1  %2 - %3").arg(QString::fromStdString(cart.cart_type), QString::fromStdString(cart.issuer), QString::fromStdString(cart.title));
}

class ElideDelegate : public QStyledItemDelegate {
public:
    ElideDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    QString displayText(const QVariant& value, const QLocale& locale) const override {
        const QString text = QStyledItemDelegate::displayText(value, locale);
        QFontMetrics fm(parentWidgetFont());
        return fm.elidedText(text, Qt::ElideRight, 400);
    }
private:
    QFont parentWidgetFont() const {
        if (auto w = qobject_cast<QWidget*>(parent())) return w->font();
        return QApplication::font();
    }
};

template <typename Work, typename Done>
void run_background(QObject* receiver, Work work, Done done) {
    class Runnable final : public QRunnable {
    public:
        Runnable(QObject* receiver, Work work, Done done)
            : guard_(receiver), work_(std::move(work)), done_(std::move(done)) {
            setAutoDelete(true);
        }

        void run() override {
            try {
                auto result = work_();
                if (!guard_) {
                    return;
                }
                QMetaObject::invokeMethod(guard_.data(),
                                          [done = std::move(done_), result = std::move(result)]() mutable {
                                              done(std::move(result));
                                          },
                                          Qt::QueuedConnection);
            } catch (const std::exception& ex) {
                if (!guard_) {
                    return;
                }
                QMetaObject::invokeMethod(guard_.data(),
                                          [message = QString::fromStdString(ex.what())]() {
                                              Logger::log(LogLevel::kWarn, "GUI", message.toStdString());
                                          },
                                          Qt::QueuedConnection);
                return;
            }
        }

    private:
        QPointer<QObject> guard_;
        Work work_;
        Done done_;
    };

    QThreadPool::globalInstance()->start(new Runnable(receiver, std::move(work), std::move(done)));
}

}  // namespace

MainWindow::MainWindow(std::unique_ptr<DatabaseProvider> db_client, QWidget* parent)
        : QMainWindow(parent),
            db_(std::move(db_client)),
            automation_(*db_),
            studio_(*db_) {
    setWindowTitle("ZAutomate");
    setWindowIcon(QIcon(":/assets/app-icon.svg"));
    setMinimumSize(1100, 760);
    build_ui();
    apply_theme();
    // Construct cart machine without initial synchronous refresh to avoid blocking GUI startup
    cart_machine_ = std::make_unique<CartMachineModule>(*db_, false);
    refresh_automation_view();
    refresh_carts_view_async();
    append_activity("Dashboard ready.");
    if (studio_window_ && studio_window_->statusBar()) studio_window_->statusBar()->showMessage("Ready");
}

void MainWindow::build_ui() {
    // Create three top-level windows: Studio (main), Cart Machine (right), Automation (small)
    studio_window_ = new QMainWindow(nullptr);
    studio_window_->setWindowTitle("ZAutomate :: DJ Studio");
    studio_window_->setWindowIcon(QIcon(":/assets/app-icon.svg"));
    studio_window_->setMinimumSize(700, 900);

    cart_window_ = new QMainWindow(nullptr);
    cart_window_->setWindowTitle("ZAutomate :: Cart Machine");
    cart_window_->setWindowIcon(QIcon(":/assets/app-icon.svg"));
    cart_window_->setMinimumSize(700, 900);

    automation_window_ = new QMainWindow(nullptr);
    automation_window_->setWindowTitle("ZAutomate :: Automation");
    automation_window_->setWindowIcon(QIcon(":/assets/app-icon.svg"));
    automation_window_->setMinimumSize(420, 320);

    auto* root = new QWidget(this);
    auto* outer = new QVBoxLayout(root);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    // Put the main menu and toolbar on the Studio window
    auto* fileMenu = studio_window_->menuBar()->addMenu("File");
    refresh_all_action_ = fileMenu->addAction("Refresh all");
    refresh_all_action_->setShortcut(QKeySequence::Refresh);
    connect(refresh_all_action_, &QAction::triggered, this, [this]() {
        refresh_automation_view();
        refresh_carts_view_async();
        append_activity("Refreshed all views.");
    });
    auto* quitAction = fileMenu->addAction("Exit");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* toolsMenu = studio_window_->menuBar()->addMenu("Tools");
    search_action_ = toolsMenu->addAction("Run studio search");
    search_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(search_action_, &QAction::triggered, this, &MainWindow::run_studio_search);

    easter_egg_action_ = toolsMenu->addAction("Hidden card");
    easter_egg_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(easter_egg_action_, &QAction::triggered, this, &MainWindow::show_easter_egg);

    auto* helpMenu = studio_window_->menuBar()->addMenu("Help");
    auto* aboutAction = helpMenu->addAction("About ZAutomate");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        append_activity("Opened about dialog.");
        statusBar()->showMessage("Qt desktop shell with a hidden Easter egg.", 2500);
    });

    auto* toolbar = studio_window_->addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->addAction(refresh_all_action_);
    toolbar->addAction(search_action_);
    toolbar->addAction(easter_egg_action_);
    // Ensure actions are enabled on startup
    if (refresh_all_action_) refresh_all_action_->setEnabled(true);
    if (search_action_) search_action_->setEnabled(true);
    if (easter_egg_action_) easter_egg_action_->setEnabled(true);

    auto* hero = new QFrame(root);
    hero->setObjectName("heroCard");
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(18, 16, 18, 16);
    heroLayout->setSpacing(6);

    hero_title_ = new QLabel("ZAutomate", hero);
    hero_title_->setObjectName("heroTitle");
    hero_title_->installEventFilter(this);
    hero_title_->setCursor(Qt::PointingHandCursor);

    overview_status_ = new QLabel(hero);
    overview_status_->setObjectName("overviewStatus");
    overview_status_->setWordWrap(true);
    overview_status_->setText("Ready — use the toolbar to refresh or run searches.");

    status_hint_ = new QLabel("Windows-style shell: menu bar, toolbar, split panes, and live data.", hero);
    status_hint_->setObjectName("heroHint");
    status_hint_->setWordWrap(true);
    status_hint_->setToolTip("Keyboard shortcut: Ctrl+Shift+B");

    heroLayout->addWidget(hero_title_);
    heroLayout->addWidget(overview_status_);
    heroLayout->addWidget(status_hint_);

    auto* automationGroup = new QGroupBox("Automation");
    auto* automationLayout = new QVBoxLayout(automationGroup);
    automationLayout->setContentsMargins(8, 10, 8, 8);
    automationLayout->setSpacing(8);

    automation_summary_ = new QLabel(automationGroup);
    automation_summary_->setWordWrap(true);

    auto* automationButtons = new QHBoxLayout;
    auto* startButton = new QPushButton("Start", automationGroup);
    auto* stopButton = new QPushButton("Stop", automationGroup);
    auto* refreshButton = new QPushButton("Refresh", automationGroup);
    connect(startButton, &QPushButton::clicked, this, [this]() {
        automation_.start();
        refresh_automation_view();
        append_activity("Automation started.");
        statusBar()->showMessage("Automation started", 2500);
    });
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        automation_.stop();
        refresh_automation_view();
        append_activity("Automation stopped softly.");
        statusBar()->showMessage("Automation stopped softly", 2500);
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refresh_automation_view();
        append_activity("Automation view refreshed.");
    });
    automationButtons->addWidget(startButton);
    automationButtons->addWidget(stopButton);
    automationButtons->addWidget(refreshButton);
    automationButtons->addStretch(1);

    queue_list_ = new QListWidget(automationGroup);
    queue_list_->setMinimumHeight(240);
    queue_list_->setItemDelegate(new ElideDelegate(queue_list_));

    automationLayout->addWidget(automation_summary_);
    automationLayout->addLayout(automationButtons);
    automationLayout->addWidget(queue_list_, 1);

    auto* studioGroup = new QGroupBox("Studio Search");
    auto* studioLayout = new QVBoxLayout(studioGroup);
    studioLayout->setContentsMargins(8, 10, 8, 8);
    studioLayout->setSpacing(8);

    auto* studioSearchRow = new QHBoxLayout;
    studio_query_ = new QLineEdit(studioGroup);
    studio_query_->setPlaceholderText("Search tracks or carts");
    studio_search_button_ = new QPushButton("Search", studioGroup);
    connect(studio_query_, &QLineEdit::returnPressed, this, &MainWindow::run_studio_search);
    connect(studio_search_button_, &QPushButton::clicked, this, &MainWindow::run_studio_search);
    studioSearchRow->addWidget(studio_query_);
    studioSearchRow->addWidget(studio_search_button_);

    studio_results_ = new QListWidget(studioGroup);
    studio_results_->setMinimumHeight(240);
    studio_results_->addItem("Type a query to search the studio library.");
    studio_results_->setItemDelegate(new ElideDelegate(studio_results_));

    studioLayout->addLayout(studioSearchRow);
    studioLayout->addWidget(studio_results_, 1);

    auto* cartGroup = new QGroupBox("Cart Machine");
    auto* cartLayout = new QVBoxLayout(cartGroup);
    cartLayout->setContentsMargins(8, 10, 8, 8);
    cartLayout->setSpacing(8);

    cart_summary_ = new QLabel(cartGroup);
    cart_summary_->setWordWrap(true);
    cart_refresh_button_ = new QPushButton("Refresh cache", cartGroup);
    connect(cart_refresh_button_, &QPushButton::clicked, this, [this]() {
        refresh_carts_view_async();
        append_activity("Cart cache refresh requested.");
    });

    // Cart grid (scrollable) to match Windows-style tiled cart view
    cart_grid_container_ = new QWidget(cartGroup);
    cart_grid_layout_ = new QGridLayout(cart_grid_container_);
    cart_grid_layout_->setContentsMargins(6, 6, 6, 6);
    cart_grid_layout_->setSpacing(6);

    cart_scroll_ = new QScrollArea(cartGroup);
    cart_scroll_->setWidgetResizable(true);
    cart_scroll_->setWidget(cart_grid_container_);
    cart_scroll_->setMinimumHeight(420);

    cart_tree_ = new QTreeWidget(cartGroup); // kept for fallback
    cart_tree_->setVisible(false);

    cartLayout->addWidget(cart_summary_);
    // Right-aligned reload button styled prominently
    auto* reloadRow = new QHBoxLayout;
    reloadRow->addStretch(1);
    auto* reloadButton = new QPushButton("Reload", cartGroup);
    reloadButton->setStyleSheet("background:#d9534f;color:#ffffff;border-radius:4px;padding:6px 10px;font-weight:700;");
    connect(reloadButton, &QPushButton::clicked, this, [this]() {
        refresh_carts_view_async();
        append_activity("Cart grid reload requested.");
    });
    reloadRow->addWidget(reloadButton);
    cartLayout->addLayout(reloadRow);
    cartLayout->addWidget(cart_refresh_button_);
    cartLayout->addWidget(cart_scroll_, 1);

    // Build separate central widgets for each top-level window
    auto* studioRoot = new QWidget();
    auto* studioOuter = new QVBoxLayout(studioRoot);
    studioOuter->setContentsMargins(12, 12, 12, 12);
    studioOuter->setSpacing(10);
    studioOuter->addWidget(hero);
    studioOuter->addWidget(studioGroup, 1);
    // Activity in studio
    auto* activityGroup = new QGroupBox("Activity");
    auto* activityLayout = new QVBoxLayout(activityGroup);
    activityLayout->setContentsMargins(8, 8, 8, 8);
    activityLayout->setSpacing(6);
    activity_log_ = new QListWidget(activityGroup);
    activity_log_->setMinimumHeight(100);
    activity_log_->setMaximumHeight(180);
    activity_log_->addItem("Ready — no activity yet.");
    activityLayout->addWidget(activity_log_);
    studioOuter->addWidget(activityGroup);
    studio_window_->setCentralWidget(studioRoot);

    auto* cartRoot = new QWidget();
    auto* cartOuter = new QVBoxLayout(cartRoot);
    cartOuter->setContentsMargins(12, 12, 12, 12);
    cartOuter->setSpacing(10);
    cartOuter->addWidget(cartGroup);
    cart_window_->setCentralWidget(cartRoot);

    auto* automationRoot = new QWidget();
    auto* automationOuter = new QVBoxLayout(automationRoot);
    automationOuter->setContentsMargins(12, 12, 12, 12);
    automationOuter->setSpacing(10);
    automationOuter->addWidget(automationGroup);
    automation_window_->setCentralWidget(automationRoot);

    // Position the windows roughly like the sample
    studio_window_->resize(720, 1080);
    studio_window_->move(60, 40);
    cart_window_->resize(700, 1080);
    cart_window_->move(820, 40);
    automation_window_->resize(420, 360);
    automation_window_->move(420, 320);

    studio_window_->show();
    cart_window_->show();
    automation_window_->show();

    // Hide controller window
    this->hide();
}

void MainWindow::apply_theme() {
    setStyleSheet(R"(
        QMainWindow {
            background: #ececec;
            color: #202020;
        }
        QMenuBar {
            background: #f4f4f4;
            border-bottom: 1px solid #d0d0d0;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 10px;
        }
        QMenuBar::item:selected {
            background: #d9d9d9;
        }
        QToolBar {
            background: #f8f8f8;
            border-bottom: 1px solid #d0d0d0;
            spacing: 6px;
            padding: 4px;
        }
        QToolBar QToolButton {
            border: 1px solid #bcbcbc;
            border-radius: 3px;
            padding: 5px 10px;
            background: #f7f7f7;
        }
        QToolBar QToolButton:hover {
            background: #ececec;
        }
        QToolBar QToolButton:pressed {
            background: #dedede;
        }
        QFrame#heroCard {
            background: #ffffff;
            border: 1px solid #d0d0d0;
            border-radius: 4px;
            padding: 6px;
        }
        QLabel#heroTitle {
            font-size: 26px;
            font-weight: 700;
            color: #101010;
        }
        QLabel#overviewStatus {
            color: #3f3f3f;
            font-size: 13px;
        }
        QLabel#heroHint {
            color: #606060;
        }
        QGroupBox {
            background: #ffffff;
            border: 1px solid #d0d0d0;
            border-radius: 4px;
            margin-top: 10px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 4px;
        }
        /* Ensure readable default text color for key widgets */
        QLabel, QToolButton, QToolBar QToolButton, QPushButton, QMenuBar::item, QGroupBox, QHeaderView::section {
            color: #202020;
        }
        QListWidget::item, QTreeWidget::item, QListWidget, QTreeWidget, QLineEdit {
            color: #202020;
        }
        QListWidget, QTreeWidget, QLineEdit {
            background: #ffffff;
            border: 1px solid #c6c6c6;
            border-radius: 3px;
            padding: 6px;
            font-size: 13px;
        }
        QListWidget::item:selected, QTreeWidget::item:selected {
            background: #0078d7;
            color: #ffffff;
        }
        QPushButton {
            background: #f7f7f7;
            color: #202020;
            border: 1px solid #bcbcbc;
            border-radius: 3px;
            padding: 6px 12px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #ececec;
        }
        QPushButton:pressed {
            background: #dedede;
        }
        QListWidget, QTreeWidget {
            background: #ffffff;
        }
    )");
}

void MainWindow::update_overview() {
    const auto queue = automation_.queue_snapshot();
    const auto carts = cart_machine_ ? cart_machine_->carts_by_type() : std::unordered_map<int, std::vector<Cart>>{};

    std::size_t cart_count = 0;
    for (const auto& [_, items] : carts) {
        cart_count += items.size();
    }

    const QString queueText = queue.empty() ? "Queue idle" : QString("Queue: %1 item(s)").arg(static_cast<int>(queue.size()));
    const QString cartsText = cart_count == 0 ? "Cart cache empty" : QString("Cart cache: %1 item(s)").arg(static_cast<int>(cart_count));
    const QString playedText = QString("Played: %1").arg(static_cast<int>(automation_.played_count()));

    overview_status_->setText(QString("%1 • %2 • %3").arg(queueText, cartsText, playedText));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == hero_title_ && event->type() == QEvent::MouseButtonDblClick) {
        show_easter_egg();
        return true;
    }
    // Tile double-click handling
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            if (w->property("isTile").toBool()) {
                const QString label = w->property("cartLabel").toString();
                const QString id = w->property("cartId").toString();
                if (!id.isEmpty()) {
                    db_->log_cart(id.toStdString());
                }
                append_activity(QString("Double-click play requested: %1").arg(label));
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refresh_automation_view() {
    const auto queue = automation_.queue_snapshot();
    update_queue_list(queue);
    update_overview();

    QString summary = queue.empty()
                         ? QString("Automation idle. Start it to load the queue.")
                         : QString("Queued: %1 | Played: %2").arg(static_cast<int>(queue.size())).arg(static_cast<int>(automation_.played_count()));
    if (!queue.empty()) {
        summary += QString(" | Next: %1 - %2").arg(QString::fromStdString(queue.front().issuer), QString::fromStdString(queue.front().title));
    }
    automation_summary_->setText(summary);
    append_activity(QString("Automation refreshed (%1 queued, %2 played).")
                        .arg(static_cast<int>(queue.size()))
                        .arg(static_cast<int>(automation_.played_count())));
    statusBar()->showMessage(QString("Automation ready: %1 queued, %2 played")
                                 .arg(static_cast<int>(queue.size()))
                                 .arg(static_cast<int>(automation_.played_count())),
                             1800);
}

void MainWindow::refresh_carts_view() {
    const auto carts = cart_machine_ ? cart_machine_->carts_by_type() : std::unordered_map<int, std::vector<Cart>>{};
    update_cart_tree(carts);
    update_overview();

    int total = 0;
    QStringList summaryParts;
    for (const auto& [type, items] : carts) {
        total += static_cast<int>(items.size());
        summaryParts << QString("%1: %2").arg(cart_type_label(type)).arg(static_cast<int>(items.size()));
    }
    cart_summary_->setText(total == 0 ? QString("Cart cache is empty right now.")
                                      : QString("%1 items cached | %2").arg(total).arg(summaryParts.join(" | ")));
    append_activity(QString("Cart cache refreshed (%1 item(s)).").arg(total));
    set_busy(false);
}

void MainWindow::refresh_carts_view_async() {
    set_busy(true, "Refreshing cart cache...");
    append_activity("Refreshing cart cache...");
    run_background(this,
                   [this]() {
                       if (cart_machine_) cart_machine_->refresh();
                       return cart_machine_ ? cart_machine_->carts_by_type() : std::unordered_map<int, std::vector<Cart>>{};
                   },
                   [this](const std::unordered_map<int, std::vector<Cart>>& carts) {
                       update_cart_tree(carts);
                       update_overview();
                       int total = 0;
                       QStringList summaryParts;
                       for (const auto& [type, items] : carts) {
                           total += static_cast<int>(items.size());
                           summaryParts << QString("%1: %2").arg(cart_type_label(type)).arg(static_cast<int>(items.size()));
                       }
                       cart_summary_->setText(total == 0 ? QString("Cart cache is empty right now.")
                                                         : QString("%1 items cached | %2").arg(total).arg(summaryParts.join(" | ")));
                       append_activity(QString("Cart cache refreshed (%1 item(s)).").arg(total));
                       statusBar()->showMessage("Cart cache refreshed", 2500);
                       set_busy(false);
                   });
}

void MainWindow::run_studio_search() {
    const auto query = studio_query_->text().trimmed();
    if (query.isEmpty()) {
        studio_results_->clear();
        studio_results_->addItem("Type a query to search the studio library.");
        return;
    }

    set_busy(true, "Searching studio library...");
    studio_results_->clear();
    studio_results_->addItem("Searching...");
    append_activity(QString("Studio search: %1").arg(query));

    run_background(this,
                   [this, query = query.toStdString()]() {
                       return studio_.search_async(query).get();
                   },
                   [this](const LibrarySearchResult& result) {
                       studio_results_->clear();
                       for (const auto& cart : result.carts) {
                           studio_results_->addItem("[Cart] " + format_item_line(cart));
                       }
                       for (const auto& track : result.tracks) {
                           studio_results_->addItem(QString("[Track] %1 - %2").arg(QString::fromStdString(track.artist), QString::fromStdString(track.title)));
                       }
                       if (studio_results_->count() == 0) {
                           studio_results_->addItem("No results found.");
                       }
                       append_activity(QString("Studio search returned %1 cart(s) and %2 track(s).")
                                           .arg(static_cast<int>(result.carts.size()))
                                           .arg(static_cast<int>(result.tracks.size())));
                       statusBar()->showMessage(QString("Search complete: %1 carts, %2 tracks").arg(static_cast<int>(result.carts.size())).arg(static_cast<int>(result.tracks.size())), 3000);
                       set_busy(false);
                   });
}

void MainWindow::show_easter_egg() {
    append_activity("Hidden card opened.");
    EasterEggDialog dialog(this);
    dialog.exec();
}

void MainWindow::set_busy(bool busy, const QString& message) {
    if (!message.isEmpty()) {
        statusBar()->showMessage(message);
    }
    if (studio_search_button_) {
        studio_search_button_->setEnabled(!busy);
    }
    if (cart_refresh_button_) {
        cart_refresh_button_->setEnabled(!busy);
    }
    if (refresh_all_action_) {
        refresh_all_action_->setEnabled(!busy);
    }
    if (search_action_) {
        search_action_->setEnabled(!busy);
    }
}

void MainWindow::append_activity(const QString& message) {
    if (!activity_log_) {
        return;
    }

    const auto entry = QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message;
    activity_log_->addItem(entry);
    activity_log_->scrollToBottom();
}

void MainWindow::update_queue_list(const std::vector<Cart>& queue) {
    queue_list_->clear();
    for (const auto& cart : queue) {
        queue_list_->addItem(QString::fromStdString(cart.issuer + " - " + cart.title));
    }
    if (queue.empty()) {
        queue_list_->addItem("Queue is currently empty.");
    }
}

void MainWindow::update_cart_tree(const std::unordered_map<int, std::vector<Cart>>& carts_by_type) {
    // Clear tree fallback
    cart_tree_->clear();

    // Clear existing grid items
    if (cart_grid_layout_) {
        while (QLayoutItem* it = cart_grid_layout_->takeAt(0)) {
            if (auto* w = it->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete it;
        }
    }

    // Build tiled grid view
    const int cols = 6;
    int row = 0;
    int col = 0;
    bool has_items = false;

    for (const auto& [type, carts] : carts_by_type) {
        for (const auto& cart : carts) {
            has_items = true;
            QString label = QString::fromStdString(cart.issuer) + "\n" + QString::fromStdString(cart.title);
            auto* tile = new QPushButton(label, cart_grid_container_);
            tile->setProperty("cartLabel", label);
            tile->setMinimumSize(160, 84);
            tile->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            tile->setProperty("cartType", type);
            tile->setProperty("isTile", true);
            tile->setProperty("cartId", QString::fromStdString(cart.cart_id));
            tile->installEventFilter(this);
            tile->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(tile, &QWidget::customContextMenuRequested, this, [this, tile, cart](const QPoint& pos) {
                QMenu menu(tile);
                menu.addAction("Log cart", [this, cart]() {
                    run_background(this,
                                   [this, id = cart.cart_id]() {
                                       db_->log_cart(id);
                                       return true;
                                   },
                                   [this, cart](bool) {
                                       append_activity(QString("Logged cart: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
                                   });
                });
                menu.addAction("Request play", [this, cart]() {
                    // enqueue and log in background
                    run_background(this,
                                   [this, cart]() {
                                       db_->log_cart(cart.cart_id);
                                       return true;
                                   },
                                   [this, cart](bool) {
                                       // enqueue into automation and ensure it is started
                                       automation_.enqueue_cart(cart);
                                       automation_.start();
                                       append_activity(QString("Play requested: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
                                   });
                });
                menu.addAction("Reveal file", [this, cart]() {
                    const QString path = QString::fromStdString(cart.filename);
                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                });
                menu.exec(tile->mapToGlobal(pos));
            });
            // Color by type
            QString color = (type == 3) ? "#ff8c42" : "#17a2b8";
            tile->setStyleSheet(QString("QPushButton { background:%1; color: #ffffff; border-radius:6px; font-weight:600; padding:8px; } QPushButton:pressed { background: #0f7f8a; }").arg(color));
            connect(tile, &QPushButton::clicked, this, [this, cart]() {
                append_activity(QString("Selected cart: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
            });

            cart_grid_layout_->addWidget(tile, row, col);

            if (++col >= cols) {
                col = 0;
                ++row;
            }
        }
    }

    if (!has_items) {
        auto* placeholder = new QLabel("No carts cached yet. Refresh to load data.", cart_grid_container_);
        placeholder->setStyleSheet("color:#606060;padding:8px;");
        cart_grid_layout_->addWidget(placeholder, 0, 0);
    }
}

}  // namespace zautomate