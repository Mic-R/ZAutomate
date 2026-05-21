#include "zautomate/gui/main_window.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <utility>

#include <QEvent>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QThreadPool>
#include <QStringList>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QToolButton>
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
      studio_(*db_),
      cart_machine_(*db_) {
    setWindowTitle("ZAutomate");
    setWindowIcon(QIcon(":/assets/app-icon.svg"));
    setMinimumSize(1100, 760);
    build_ui();
    apply_theme();
    refresh_automation_view();
    refresh_carts_view();
    statusBar()->showMessage("Ready");
}

void MainWindow::build_ui() {
    auto* root = new QWidget(this);
    auto* outer = new QHBoxLayout(root);
    outer->setContentsMargins(20, 20, 20, 20);
    outer->setSpacing(16);

    auto* sidebar = new QFrame(root);
    sidebar->setObjectName("sidebarCard");
    sidebar->setMinimumWidth(210);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(10);

    auto* sidebarTitle = new QLabel("ZAutomate", sidebar);
    sidebarTitle->setObjectName("sidebarTitle");
    auto* sidebarSubtitle = new QLabel("Desktop control plane", sidebar);
    sidebarSubtitle->setObjectName("sidebarSubtitle");
    sidebarSubtitle->setWordWrap(true);

    auto* navLabel = new QLabel("Navigation", sidebar);
    navLabel->setObjectName("sidebarSectionLabel");

    auto* dashboardButton = new QToolButton(sidebar);
    dashboardButton->setText("Dashboard");
    dashboardButton->setCheckable(true);
    dashboardButton->setChecked(true);
    dashboardButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* aboutButton = new QToolButton(sidebar);
    aboutButton->setText("About");
    aboutButton->setCheckable(true);
    aboutButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* eggButton = new QToolButton(sidebar);
    eggButton->setText("Hidden card");
    eggButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* navStack = new QStackedWidget(root);

    auto* hero = new QFrame(root);
    hero->setObjectName("heroCard");
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(24, 24, 24, 24);
    heroLayout->setSpacing(10);

    hero_title_ = new QLabel("ZAutomate", hero);
    hero_title_->setObjectName("heroTitle");
    hero_title_->installEventFilter(this);
    hero_title_->setCursor(Qt::PointingHandCursor);

    auto* subtitle = new QLabel("A focused desktop control surface for automation, studio search, and cart management.", hero);
    subtitle->setWordWrap(true);
    subtitle->setObjectName("heroSubtitle");

    status_hint_ = new QLabel("A small surprise is built into the shell.", hero);
    status_hint_->setObjectName("heroHint");
    status_hint_->setToolTip("Keyboard shortcut: Ctrl+Shift+B");

    heroLayout->addWidget(hero_title_);
    heroLayout->addWidget(subtitle);
    heroLayout->addWidget(status_hint_);

    auto* dashboard = new QWidget(tabs_);
    auto* dashboardLayout = new QGridLayout(dashboard);
    dashboardLayout->setContentsMargins(0, 0, 0, 0);
    dashboardLayout->setHorizontalSpacing(16);
    dashboardLayout->setVerticalSpacing(16);

    auto* automationCard = new QFrame(dashboard);
    automationCard->setObjectName("card");
    auto* automationLayout = new QVBoxLayout(automationCard);
    automationLayout->setContentsMargins(20, 20, 20, 20);
    automationLayout->setSpacing(12);

    auto* automationTitle = new QLabel("Automation", automationCard);
    automationTitle->setObjectName("cardTitle");
    automation_summary_ = new QLabel(automationCard);
    automation_summary_->setWordWrap(true);
    auto* buttonRow = new QHBoxLayout;
    auto* startButton = new QPushButton("Start", automationCard);
    auto* stopButton = new QPushButton("Stop", automationCard);
    auto* refreshButton = new QPushButton("Refresh", automationCard);
    connect(startButton, &QPushButton::clicked, this, [this]() {
        automation_.start();
        refresh_automation_view();
        statusBar()->showMessage("Automation started", 2500);
    });
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        automation_.stop();
        refresh_automation_view();
        statusBar()->showMessage("Automation stopped softly", 2500);
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refresh_automation_view();
    });
    buttonRow->addWidget(startButton);
    buttonRow->addWidget(stopButton);
    buttonRow->addWidget(refreshButton);
    buttonRow->addStretch(1);

    queue_list_ = new QListWidget(automationCard);
    queue_list_->setMinimumHeight(220);

    automationLayout->addWidget(automationTitle);
    automationLayout->addWidget(automation_summary_);
    automationLayout->addLayout(buttonRow);
    automationLayout->addWidget(queue_list_);

    auto* studioCard = new QFrame(dashboard);
    studioCard->setObjectName("card");
    auto* studioLayout = new QVBoxLayout(studioCard);
    studioLayout->setContentsMargins(20, 20, 20, 20);
    studioLayout->setSpacing(12);

    auto* studioTitle = new QLabel("Studio Search", studioCard);
    studioTitle->setObjectName("cardTitle");
    auto* searchRow = new QHBoxLayout;
    studio_query_ = new QLineEdit(studioCard);
    studio_query_->setPlaceholderText("Search tracks or carts");
    studio_search_button_ = new QPushButton("Search", studioCard);
    connect(studio_query_, &QLineEdit::returnPressed, this, &MainWindow::run_studio_search);
    connect(studio_search_button_, &QPushButton::clicked, this, &MainWindow::run_studio_search);
    searchRow->addWidget(studio_query_);
    searchRow->addWidget(studio_search_button_);

    studio_results_ = new QListWidget(studioCard);
    studio_results_->setMinimumHeight(220);

    studioLayout->addWidget(studioTitle);
    studioLayout->addLayout(searchRow);
    studioLayout->addWidget(studio_results_);

    auto* cartCard = new QFrame(dashboard);
    cartCard->setObjectName("card");
    auto* cartLayout = new QVBoxLayout(cartCard);
    cartLayout->setContentsMargins(20, 20, 20, 20);
    cartLayout->setSpacing(12);

    auto* cartTitle = new QLabel("Cart Machine", cartCard);
    cartTitle->setObjectName("cardTitle");
    cart_summary_ = new QLabel(cartCard);
    cart_summary_->setWordWrap(true);
    cart_refresh_button_ = new QPushButton("Refresh cache", cartCard);
    connect(cart_refresh_button_, &QPushButton::clicked, this, [this]() {
        refresh_carts_view_async();
    });

    cart_tree_ = new QTreeWidget(cartCard);
    cart_tree_->setHeaderLabels({"Type", "Items"});
    cart_tree_->header()->setStretchLastSection(true);
    cart_tree_->setMinimumHeight(220);

    cartLayout->addWidget(cartTitle);
    cartLayout->addWidget(cart_summary_);
    cartLayout->addWidget(cart_refresh_button_);
    cartLayout->addWidget(cart_tree_);

    dashboardLayout->addWidget(automationCard, 0, 0);
    dashboardLayout->addWidget(studioCard, 0, 1);
    dashboardLayout->addWidget(cartCard, 1, 0, 1, 2);

    navStack->addWidget(dashboard);

    auto* about = new QWidget(tabs_);
    auto* aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(0, 0, 0, 0);
    aboutLayout->setSpacing(12);

    auto* aboutCard = new QFrame(about);
    aboutCard->setObjectName("card");
    auto* aboutCardLayout = new QVBoxLayout(aboutCard);
    aboutCardLayout->setContentsMargins(20, 20, 20, 20);
    aboutCardLayout->setSpacing(10);

    auto* aboutTitle = new QLabel("About", aboutCard);
    aboutTitle->setObjectName("cardTitle");
    auto* aboutBody = new QLabel("Minimalist desktop shell for the WSBF automation tools. The hidden card is intentionally tucked away.", aboutCard);
    aboutBody->setWordWrap(true);
    auto* aboutLines = new QLabel("Built with Qt Widgets and the existing C++ backend.", aboutCard);
    aboutLines->setWordWrap(true);

    aboutCardLayout->addWidget(aboutTitle);
    aboutCardLayout->addWidget(aboutBody);
    aboutCardLayout->addWidget(aboutLines);
    aboutLayout->addWidget(aboutCard);
    aboutLayout->addStretch(1);

    navStack->addWidget(about);

    connect(dashboardButton, &QToolButton::clicked, this, [dashboardButton, aboutButton, navStack]() {
        dashboardButton->setChecked(true);
        aboutButton->setChecked(false);
        navStack->setCurrentIndex(0);
    });
    connect(aboutButton, &QToolButton::clicked, this, [dashboardButton, aboutButton, navStack]() {
        dashboardButton->setChecked(false);
        aboutButton->setChecked(true);
        navStack->setCurrentIndex(1);
    });
    connect(eggButton, &QToolButton::clicked, this, [this]() {
        show_easter_egg();
    });

    sidebarLayout->addWidget(sidebarTitle);
    sidebarLayout->addWidget(sidebarSubtitle);
    sidebarLayout->addSpacing(8);
    sidebarLayout->addWidget(navLabel);
    sidebarLayout->addWidget(dashboardButton);
    sidebarLayout->addWidget(aboutButton);
    sidebarLayout->addWidget(eggButton);
    sidebarLayout->addStretch(1);

    auto* contentColumn = new QVBoxLayout;
    contentColumn->setSpacing(16);

    auto* appMenu = menuBar()->addMenu("App");
    refresh_all_action_ = appMenu->addAction("Refresh All");
    refresh_all_action_->setShortcut(QKeySequence::Refresh);
    connect(refresh_all_action_, &QAction::triggered, this, [this]() {
        refresh_automation_view();
        refresh_carts_view_async();
    });

    search_action_ = appMenu->addAction("Run Search");
    search_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(search_action_, &QAction::triggered, this, &MainWindow::run_studio_search);

    easter_egg_action_ = appMenu->addAction("Hidden Card");
    easter_egg_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(easter_egg_action_, &QAction::triggered, this, &MainWindow::show_easter_egg);

    auto* helpMenu = menuBar()->addMenu("Help");
    auto* aboutAction = helpMenu->addAction("About ZAutomate");
    connect(aboutAction, &QAction::triggered, this, [this, navStack]() {
        navStack->setCurrentIndex(1);
        statusBar()->showMessage("About view opened", 2000);
    });

    contentColumn->addWidget(hero);
    contentColumn->addWidget(navStack, 1);

    outer->addWidget(sidebar);
    outer->addLayout(contentColumn, 1);
    setCentralWidget(root);
}

void MainWindow::apply_theme() {
    setStyleSheet(R"(
        QMainWindow {
            background: #f5f2ec;
            color: #1f2328;
        }
        QTabWidget::pane {
            border: none;
        }
        QTabBar::tab {
            background: #e7e2da;
            color: #43484f;
            padding: 10px 16px;
            margin-right: 6px;
            border-top-left-radius: 12px;
            border-top-right-radius: 12px;
        }
        QTabBar::tab:selected {
            background: #ffffff;
            color: #121417;
        }
        QFrame#heroCard, QFrame#card, QFrame#sidebarCard {
            background: rgba(255, 255, 255, 0.86);
            border: 1px solid #ded7ce;
            border-radius: 22px;
        }
        QFrame#sidebarCard {
            background: #fffdf9;
        }
        QLabel#sidebarTitle {
            font-size: 22px;
            font-weight: 800;
            color: #111214;
        }
        QLabel#sidebarSubtitle {
            color: #5a6069;
        }
        QLabel#sidebarSectionLabel {
            color: #8d8478;
            text-transform: uppercase;
            letter-spacing: 1px;
            font-size: 11px;
        }
        QToolButton {
            background: transparent;
            color: #1f2328;
            border: 1px solid transparent;
            border-radius: 12px;
            padding: 10px 12px;
            text-align: left;
        }
        QToolButton:hover {
            background: #f0ebe3;
        }
        QToolButton:checked {
            background: #1f2328;
            color: #ffffff;
        }
        QLabel#heroTitle {
            font-size: 34px;
            font-weight: 800;
            letter-spacing: 0.5px;
            color: #101214;
        }
        QLabel#heroSubtitle {
            font-size: 15px;
            color: #4c5159;
        }
        QLabel#heroHint {
            color: #8a6a35;
            font-weight: 600;
        }
        QLabel#cardTitle {
            font-size: 18px;
            font-weight: 700;
            color: #101214;
        }
        QListWidget, QTreeWidget, QLineEdit {
            background: #ffffff;
            border: 1px solid #d9d2c8;
            border-radius: 14px;
            padding: 10px;
            selection-background-color: #d8caa7;
        }
        QPushButton {
            background: #1f2328;
            color: #ffffff;
            border: none;
            border-radius: 12px;
            padding: 10px 16px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #2f3640;
        }
        QPushButton:pressed {
            background: #111317;
        }
    )");
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == hero_title_ && event->type() == QEvent::MouseButtonDblClick) {
        show_easter_egg();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refresh_automation_view() {
    const auto queue = automation_.queue_snapshot();
    update_queue_list(queue);

    QString summary = QString("Queued: %1 | Played: %2").arg(static_cast<int>(queue.size())).arg(static_cast<int>(automation_.played_count()));
    if (!queue.empty()) {
        summary += QString(" | Next: %1 - %2").arg(QString::fromStdString(queue.front().issuer), QString::fromStdString(queue.front().title));
    }
    automation_summary_->setText(summary);
    statusBar()->showMessage(QString("Automation ready: %1 queued, %2 played")
                                 .arg(static_cast<int>(queue.size()))
                                 .arg(static_cast<int>(automation_.played_count())),
                             1800);
}

void MainWindow::refresh_carts_view() {
    const auto carts = cart_machine_.carts_by_type();
    update_cart_tree(carts);

    int total = 0;
    QStringList summaryParts;
    for (const auto& [type, items] : carts) {
        total += static_cast<int>(items.size());
        summaryParts << QString("%1: %2").arg(cart_type_label(type)).arg(static_cast<int>(items.size()));
    }
    cart_summary_->setText(QString("%1 items cached | %2").arg(total).arg(summaryParts.join(" | ")));
    set_busy(false);
}

void MainWindow::refresh_carts_view_async() {
    set_busy(true, "Refreshing cart cache...");
    run_background(this,
                   [this]() {
                       cart_machine_.refresh();
                       return cart_machine_.carts_by_type();
                   },
                   [this](const std::unordered_map<int, std::vector<Cart>>& carts) {
                       update_cart_tree(carts);
                       int total = 0;
                       QStringList summaryParts;
                       for (const auto& [type, items] : carts) {
                           total += static_cast<int>(items.size());
                           summaryParts << QString("%1: %2").arg(cart_type_label(type)).arg(static_cast<int>(items.size()));
                       }
                       cart_summary_->setText(QString("%1 items cached | %2").arg(total).arg(summaryParts.join(" | ")));
                       statusBar()->showMessage("Cart cache refreshed", 2500);
                       set_busy(false);
                   });
}

void MainWindow::run_studio_search() {
    const auto query = studio_query_->text().trimmed();
    if (query.isEmpty()) {
        studio_results_->clear();
        studio_results_->addItem("Enter a search term first.");
        return;
    }

    set_busy(true, "Searching studio library...");
    studio_results_->clear();
    studio_results_->addItem("Searching...");

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
                           studio_results_->addItem("No results.");
                       }
                       statusBar()->showMessage(QString("Search complete: %1 carts, %2 tracks").arg(static_cast<int>(result.carts.size())).arg(static_cast<int>(result.tracks.size())), 3000);
                       set_busy(false);
                   });
}

void MainWindow::show_easter_egg() {
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
    cart_tree_->clear();

    for (const auto& [type, carts] : carts_by_type) {
        auto* typeItem = new QTreeWidgetItem(cart_tree_);
        typeItem->setText(0, cart_type_label(type));
        typeItem->setText(1, QString::number(static_cast<int>(carts.size())));

        for (const auto& cart : carts) {
            auto* item = new QTreeWidgetItem(typeItem);
            item->setText(0, QString::fromStdString(cart.issuer + " - " + cart.title));
            item->setText(1, QString::fromStdString(cart.filename));
        }
    }

    cart_tree_->expandAll();
}

}  // namespace zautomate