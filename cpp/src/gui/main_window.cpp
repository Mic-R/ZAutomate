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
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QFormLayout>
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
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QBrush>
#include <QColor>
#include <QShortcut>
#include <QStatusBar>
#include <QPlainTextEdit>
#include <QThreadPool>
#include <QTimer>
#include <QSplitter>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolBar>
#include <QGridLayout>
#include <QScrollArea>
#include <QDesktopServices>
#include <QComboBox>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>
#include <QIcon>
#include <QSpinBox>
#include <QTabWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>

#if defined(ZAUTOMATE_HAS_QT_MULTIMEDIA)
#include <QAudioDevice>
#include <QMediaDevices>
#endif

#include "zautomate/database_client.hpp"
#include "zautomate/logger.hpp"
#include "zautomate/gui/easter_egg_dialog.hpp"
#include "zautomate/update_manager.hpp"

namespace zautomate {

namespace {

enum SearchItemRole {
    KindRole = Qt::UserRole + 1,
    IdRole,
    IssuerRole,
    TitleRole,
    FileRole,
    TypeRole,
    DurationRole,
    LengthRole,
};

constexpr char kCartMimeType[] = "application/x-zautomate-cart";
constexpr char kSystemDefaultAudioLabel[] = "System default output";

void populate_audio_device_picker(QComboBox* combo, const QString& selectedText = QString()) {
    if (!combo) {
        return;
    }

    combo->clear();

#if defined(ZAUTOMATE_HAS_QT_MULTIMEDIA)
    combo->addItem(kSystemDefaultAudioLabel);
    const auto outputs = QMediaDevices::audioOutputs();
    for (const auto& device : outputs) {
        combo->addItem(device.description());
    }
    if (!selectedText.isEmpty()) {
        const int index = combo->findText(selectedText);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    } else if (combo->count() > 0) {
        combo->setCurrentIndex(0);
    }
#else
    combo->addItem(kSystemDefaultAudioLabel);
    combo->addItem("Qt Multimedia not available");
    combo->setCurrentIndex(0);
    if (!selectedText.isEmpty()) {
        const int index = combo->findText(selectedText);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    }
#endif
}

QJsonObject cart_to_json(const Cart& cart, const QString& kind) {
    QJsonObject obj;
    obj["kind"] = kind;
    obj["id"] = QString::fromStdString(cart.cart_id);
    obj["issuer"] = QString::fromStdString(cart.issuer);
    obj["title"] = QString::fromStdString(cart.title);
    obj["file"] = QString::fromStdString(cart.filename);
    obj["type"] = QString::fromStdString(cart.cart_type);
    obj["durationMs"] = cart.length_ms;
    return obj;
}

Cart cart_from_json(const QJsonObject& obj) {
    Cart cart;
    cart.cart_id = obj["id"].toString().toStdString();
    cart.issuer = obj["issuer"].toString().toStdString();
    cart.title = obj["title"].toString().toStdString();
    cart.filename = obj["file"].toString().toStdString();
    cart.cart_type = obj["type"].toString().toStdString();
    cart.length_ms = obj["durationMs"].toInt();
    return cart;
}

int parse_duration_ms_from_item(QListWidgetItem* item) {
    const auto raw = item->data(LengthRole);
    if (raw.isValid()) {
        return raw.toInt();
    }
    const auto duration = item->data(DurationRole).toString();
    const auto parts = duration.split(":");
    if (parts.size() != 2) {
        return 0;
    }
    return parts[0].toInt() * 60 * 1000 + parts[1].toInt() * 1000;
}

Cart cart_from_item(QListWidgetItem* item) {
    Cart cart;
    cart.cart_id = item->data(IdRole).toString().toStdString();
    cart.issuer = item->data(IssuerRole).toString().toStdString();
    cart.title = item->data(TitleRole).toString().toStdString();
    cart.filename = item->data(FileRole).toString().toStdString();
    cart.cart_type = item->data(TypeRole).toString().toStdString();
    cart.length_ms = parse_duration_ms_from_item(item);
    return cart;
}

class CartDragListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        auto* item = currentItem();
        if (!item) {
            return;
        }

        const QString kind = item->data(KindRole).toString();
        Cart cart = cart_from_item(item);
        if (cart.cart_id.empty()) {
            return;
        }

        auto* mime = new QMimeData();
        mime->setData(kCartMimeType, QJsonDocument(cart_to_json(cart, kind)).toJson(QJsonDocument::Compact));

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(viewport()->grab(visualItemRect(item)));
        drag->exec(supportedActions, Qt::CopyAction);
    }
};

class QueueDropListWidget : public QListWidget {
public:
    explicit QueueDropListWidget(std::function<void(const Cart&, bool)> onDrop, QWidget* parent = nullptr)
        : QListWidget(parent), onDrop_(std::move(onDrop)) {
        setAcceptDrops(true);
        setDropIndicatorShown(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(kCartMimeType)) {
            event->acceptProposedAction();
            return;
        }
        QListWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->mimeData()->hasFormat(kCartMimeType)) {
            event->acceptProposedAction();
            return;
        }
        QListWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        if (event->mimeData()->hasFormat(kCartMimeType)) {
            const auto json = QJsonDocument::fromJson(event->mimeData()->data(kCartMimeType)).object();
            if (onDrop_) {
                onDrop_(cart_from_json(json), event->modifiers() & Qt::ShiftModifier);
            }
            event->acceptProposedAction();
            return;
        }
        QListWidget::dropEvent(event);
    }

private:
    std::function<void(const Cart&, bool)> onDrop_;
};

QString search_item_text(const QString& kind, const QString& issuer, const QString& title) {
    if (kind == "track") {
        return QString("[Track] %1 - %2").arg(issuer, title);
    }
    return QString("[Cart] %1 - %2").arg(issuer, title);
}

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

QString format_duration(int ms) {
    const int totalSeconds = std::max(0, ms / 1000);
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
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

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle("Settings");
        setMinimumSize(680, 460);

        auto* root = new QVBoxLayout(this);
        tabs_ = new QTabWidget(this);

        root->addWidget(tabs_);
        root->addWidget(create_button_box());

        build_general_tab();
        build_playback_tab();
        load_from_settings();
    }

    QString base_search_url() const {
        return base_search_url_edit_->text().trimmed();
    }

    QString library_prefix() const {
        return library_prefix_edit_->text().trimmed();
    }

    QString theme() const {
        return theme_combo_->currentText();
    }

    QString audio_output() const {
        return audio_output_combo_->currentText();
    }

    int search_result_limit() const {
        return search_limit_spin_->value();
    }

    bool verbose_activity_log() const {
        return verbose_activity_check_->isChecked();
    }

    bool auto_refresh_carts() const {
        return auto_refresh_check_->isChecked();
    }

private:
    QDialogButtonBox* create_button_box() {
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
        return box;
    }

    void build_general_tab() {
        auto* general = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(general);
        auto* form = new QFormLayout();

        base_search_url_edit_ = new QLineEdit(general);
        base_search_url_edit_->setPlaceholderText("https://wsbf.net");
        form->addRow("Base search URL", base_search_url_edit_);

        library_prefix_edit_ = new QLineEdit(general);
        library_prefix_edit_->setPlaceholderText("/users/jermaine");
        form->addRow("Library path", library_prefix_edit_);

        search_limit_spin_ = new QSpinBox(general);
        search_limit_spin_->setRange(1, 500);
        search_limit_spin_->setSuffix(" results");
        form->addRow("Search result limit", search_limit_spin_);

        layout->addWidget(new QLabel("General application and backend settings.", general));
        layout->addLayout(form);
        layout->addStretch(1);
        tabs_->addTab(general, "General");
    }

    void build_playback_tab() {
        auto* playback = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(playback);
        auto* form = new QFormLayout();

        audio_output_combo_ = new QComboBox(playback);
        populate_audio_device_picker(audio_output_combo_);
        form->addRow("Audio device", audio_output_combo_);

        theme_combo_ = new QComboBox(playback);
        theme_combo_->addItems({"Fusion", "System"});
        form->addRow("Theme", theme_combo_);

        auto_refresh_check_ = new QCheckBox("Refresh cart cache on startup", playback);
        verbose_activity_check_ = new QCheckBox("Write verbose activity log entries", playback);

        layout->addWidget(new QLabel("Playback and visual preferences.", playback));
        layout->addLayout(form);
        layout->addWidget(auto_refresh_check_);
        layout->addWidget(verbose_activity_check_);
        layout->addStretch(1);
        tabs_->addTab(playback, "Playback");
    }

    /* Advanced tab removed: no content to display */

    void load_from_settings() {
        QSettings settings;
        base_search_url_edit_->setText(settings.value("network/baseSearchUrl", "https://wsbf.net").toString());
        library_prefix_edit_->setText(settings.value("storage/libraryPrefix", "/users/jermaine").toString());
        search_limit_spin_->setValue(settings.value("studio/searchResultLimit", 24).toInt());
        populate_audio_device_picker(audio_output_combo_, settings.value("audio/defaultOutput", kSystemDefaultAudioLabel).toString());
        theme_combo_->setCurrentText(settings.value("ui/theme", "Fusion").toString());
        auto_refresh_check_->setChecked(settings.value("cart/autoRefreshOnStart", true).toBool());
        verbose_activity_check_->setChecked(settings.value("ui/verboseActivityLog", false).toBool());
    }

    QTabWidget* tabs_{nullptr};
    QLineEdit* base_search_url_edit_{nullptr};
    QLineEdit* library_prefix_edit_{nullptr};
    QSpinBox* search_limit_spin_{nullptr};
    QComboBox* audio_output_combo_{nullptr};
    QComboBox* theme_combo_{nullptr};
    QCheckBox* auto_refresh_check_{nullptr};
    QCheckBox* verbose_activity_check_{nullptr};
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
    {
        QSettings settings;
        if (audio_output_combo_) {
            populate_audio_device_picker(audio_output_combo_, settings.value("audio/defaultOutput", kSystemDefaultAudioLabel).toString());
        }
        const auto theme_name = settings.value("ui/theme", "Fusion").toString();
        if (!theme_name.isEmpty()) {
            qApp->setStyle(theme_name);
        }
    }
    apply_theme();
    // Construct cart machine without initial synchronous refresh to avoid blocking GUI startup
    cart_machine_ = std::make_unique<CartMachineModule>(*db_, false);
    automation_.set_on_cart_start([this](const Cart& cart) {
        QMetaObject::invokeMethod(this, [this, cart]() {
            refresh_automation_view();
            append_activity(QString("Cart started: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
        }, Qt::QueuedConnection);
    });
    automation_.set_on_warning([this](const std::string& warning) {
        QMetaObject::invokeMethod(this, [this, warning]() {
            const QString message = QString::fromStdString(warning);
            append_playback_warning(message);
            if (studio_window_ && studio_window_->statusBar()) {
                studio_window_->statusBar()->showMessage(message, 5000);
            }
        }, Qt::QueuedConnection);
    });
    playback_timer_ = new QTimer(this);
    playback_timer_->setInterval(1000);
    connect(playback_timer_, &QTimer::timeout, this, &MainWindow::update_playback_status);
    playback_timer_->start();
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
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    auto* toolsMenu = studio_window_->menuBar()->addMenu("Tools");
    search_action_ = toolsMenu->addAction("Run studio search");
    search_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(search_action_, &QAction::triggered, this, &MainWindow::run_studio_search);

    install_action_ = toolsMenu->addAction("Install permanently");
    connect(install_action_, &QAction::triggered, this, [this]() {
        append_activity("Permanent install requested.");
        set_busy(true, "Installing ZAutomate system-wide...");
        auto updater = std::make_shared<UpdateManager>("https://cloud.mic-r.eu/zautomate", ZAUTOMATE_VERSION);
        std::thread([this, updater]() {
            updater->install_latest_release();
            QMetaObject::invokeMethod(this, [this]() {
                set_busy(false);
                statusBar()->showMessage("Install action finished", 2500);
            }, Qt::QueuedConnection);
        }).detach();
    });

    settings_action_ = toolsMenu->addAction("Settings");
    settings_action_->setShortcut(QKeySequence::Preferences);
    connect(settings_action_, &QAction::triggered, this, &MainWindow::open_settings_dialog);

    easter_egg_action_ = toolsMenu->addAction("Shh big secret!");
    easter_egg_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(easter_egg_action_, &QAction::triggered, this, &MainWindow::show_easter_egg);

    auto* helpMenu = studio_window_->menuBar()->addMenu("Help");
    auto* aboutAction = helpMenu->addAction("About ZAutomate");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        append_activity("Opened about dialog.");
        const QString ver = QString::fromUtf8(ZAUTOMATE_VERSION);
        const QString about_text = QString("ZAutomate %1\n\nQt desktop shell for zAutomate (the Reimchen version).\n\nCopyright Michael Reimchen (michael@reimchen.org)\n").arg(ver);
        QMessageBox::about(studio_window_, "About ZAutomate", about_text);
    });

    auto* toolbar = studio_window_->addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->addAction(refresh_all_action_);
    toolbar->addAction(search_action_);
    toolbar->addAction(install_action_);
    toolbar->addAction(settings_action_);
    toolbar->addAction(easter_egg_action_);
    // Ensure actions are enabled on startup
    if (refresh_all_action_) refresh_all_action_->setEnabled(true);
    if (search_action_) search_action_->setEnabled(true);
    if (install_action_) install_action_->setEnabled(true);
    if (settings_action_) settings_action_->setEnabled(true);
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

    heroLayout->addWidget(hero_title_);
    heroLayout->addWidget(overview_status_);

    auto* automationGroup = new QGroupBox("Automation");
    auto* automationLayout = new QVBoxLayout(automationGroup);
    automationLayout->setContentsMargins(8, 10, 8, 8);
    automationLayout->setSpacing(8);

    automation_summary_ = new QLabel(automationGroup);
    automation_summary_->setWordWrap(true);

    now_playing_label_ = new QLabel("Now playing: nothing", automationGroup);
    now_playing_label_->setWordWrap(true);
    now_playing_time_ = new QLabel("00:00", automationGroup);
    now_playing_time_->setStyleSheet("color:#148a28;font-weight:700;");

    auto* audioRow = new QHBoxLayout;
    auto* audioLabel = new QLabel("Output:", automationGroup);
    audio_output_combo_ = new QComboBox(automationGroup);
    populate_audio_device_picker(audio_output_combo_, QSettings().value("audio/defaultOutput", kSystemDefaultAudioLabel).toString());
    auto* audioApplyButton = new QPushButton("Apply", automationGroup);
    audio_output_status_ = new QLabel("Choose a device for playback output.", automationGroup);
    audio_output_status_->setWordWrap(true);
    connect(audioApplyButton, &QPushButton::clicked, this, [this]() {
        if (audio_output_status_ && audio_output_combo_) {
            audio_output_status_->setText(QString("Selected output: %1").arg(audio_output_combo_->currentText()));
            QSettings().setValue("audio/defaultOutput", audio_output_combo_->currentText());
        }
        append_activity(QString("Audio output selected: %1").arg(audio_output_combo_ ? audio_output_combo_->currentText() : QString("unknown")));
    });
    audioRow->addWidget(audioLabel);
    audioRow->addWidget(audio_output_combo_, 1);
    audioRow->addWidget(audioApplyButton);

    auto* automationButtons = new QHBoxLayout;
    auto* startButton = new QPushButton("Start", automationGroup);
    auto* stopButton = new QPushButton("Pause", automationGroup);
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
        append_activity("Automation paused.");
        statusBar()->showMessage("Automation paused", 2500);
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refresh_automation_view();
        append_activity("Automation view refreshed.");
    });
    automationButtons->addWidget(startButton);
    automationButtons->addWidget(stopButton);
    automationButtons->addWidget(refreshButton);
    automationButtons->addStretch(1);

    queue_list_ = new QueueDropListWidget([this](const Cart& cart, bool playNext) {
        prompt_queue_choice_and_enqueue(cart, playNext);
    }, automationGroup);
    queue_list_->setMinimumHeight(240);
    queue_list_->setItemDelegate(new ElideDelegate(queue_list_));
    queue_list_->setDragDropMode(QAbstractItemView::DropOnly);
    queue_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(queue_list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = queue_list_->itemAt(pos);
        if (!item) {
            return;
        }

        const QString id = item->data(IdRole).toString();
        const QString label = item->text();

        QMenu menu(queue_list_);
        menu.addAction("Delete from queue", [this, id, label]() {
            const bool removed = automation_.remove_cart_by_id(id.toStdString());
            append_activity(removed ? QString("Removed from queue: %1").arg(label)
                                    : QString("Queue item not found: %1").arg(label));
            refresh_automation_view();
        });
        menu.exec(queue_list_->viewport()->mapToGlobal(pos));
    });

    automationLayout->addWidget(automation_summary_);
    automationLayout->addWidget(now_playing_label_);
    automationLayout->addWidget(now_playing_time_);
    automationLayout->addLayout(audioRow);
    automationLayout->addWidget(audio_output_status_);
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

    studio_results_ = new CartDragListWidget(studioGroup);
    studio_results_->setMinimumHeight(240);
    studio_results_->addItem("Type a query to search the studio library.");
    studio_results_->setItemDelegate(new ElideDelegate(studio_results_));
    studio_results_->setDragEnabled(true);
    studio_results_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(studio_results_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = studio_results_->itemAt(pos);
        if (!item) {
            return;
        }

        const QString kind = item->data(KindRole).toString();
        const QString id = item->data(IdRole).toString();
        const QString issuer = item->data(IssuerRole).toString();
        const QString title = item->data(TitleRole).toString();
        const QString file = item->data(FileRole).toString();
        const QString type = item->data(TypeRole).toString();

        QMenu menu(studio_results_);
        menu.addAction("Add to queue", [this, item, kind, id, issuer, title, file, type]() {
            Cart cart;
            cart.cart_id = id.toStdString();
            cart.title = title.toStdString();
            cart.issuer = issuer.toStdString();
            cart.cart_type = type.toStdString();
            cart.filename = file.toStdString();
            int length_ms = (kind == "track") ? parse_duration_ms_from_item(item) : 20;
            if (length_ms <= 0 && kind == "track") {
                length_ms = 180000;
            }
            cart.length_ms = length_ms;
            automation_.append_cart(cart);
            append_activity(QString("Added to queue: %1 - %2").arg(issuer, title));
            refresh_automation_view();
        });
        menu.addAction("Play next", [this, item, kind, id, issuer, title, file, type]() {
            Cart cart;
            cart.cart_id = id.toStdString();
            cart.title = title.toStdString();
            cart.issuer = issuer.toStdString();
            cart.cart_type = type.toStdString();
            cart.filename = file.toStdString();
            int length_ms = (kind == "track") ? parse_duration_ms_from_item(item) : 20;
            if (length_ms <= 0 && kind == "track") {
                length_ms = 180000;
            }
            cart.length_ms = length_ms;
            prompt_queue_choice_and_enqueue(cart, true);
        });
        menu.addAction("Remove from list", [item]() {
            delete item;
        });
        menu.exec(studio_results_->viewport()->mapToGlobal(pos));
    });

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

    auto* cartSplit = new QSplitter(Qt::Horizontal, cartGroup);
    auto* cartSide = new QWidget(cartSplit);
    auto* cartSideLayout = new QVBoxLayout(cartSide);
    cartSideLayout->setContentsMargins(0, 0, 0, 0);
    cartSideLayout->setSpacing(8);

    cart_current_label_ = new QLabel("Current: none", cartSide);
    cart_current_label_->setWordWrap(true);
    cart_current_label_->setStyleSheet("font-weight:700;");
    cart_next_label_ = new QLabel("Next: none", cartSide);
    cart_next_label_->setWordWrap(true);

    cart_preview_queue_ = new QListWidget(cartSide);
    cart_preview_queue_->setMinimumHeight(220);
    cart_preview_queue_->setItemDelegate(new ElideDelegate(cart_preview_queue_));

    auto* reloadButton = new QPushButton("Reload", cartSide);
    reloadButton->setStyleSheet("background:#d9534f;color:#ffffff;border-radius:4px;padding:6px 10px;font-weight:700;");
    connect(reloadButton, &QPushButton::clicked, this, [this]() {
        refresh_carts_view_async();
        append_activity("Cart grid reload requested.");
    });

    cartSideLayout->addWidget(cart_summary_);
    cartSideLayout->addWidget(cart_current_label_);
    cartSideLayout->addWidget(cart_next_label_);
    cartSideLayout->addWidget(cart_preview_queue_, 1);
    cartSideLayout->addWidget(cart_refresh_button_);
    cartSideLayout->addWidget(reloadButton);

    auto* cartRight = new QWidget(cartSplit);
    auto* cartRightLayout = new QVBoxLayout(cartRight);
    cartRightLayout->setContentsMargins(0, 0, 0, 0);
    cartRightLayout->setSpacing(8);
    cartRightLayout->addWidget(cart_scroll_, 1);

    cartSplit->addWidget(cartSide);
    cartSplit->addWidget(cartRight);
    cartSplit->setStretchFactor(0, 0);
    cartSplit->setStretchFactor(1, 1);

    cartLayout->addWidget(cartSplit, 1);

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
                Cart cart;
                const auto label = w->property("cartLabel").toString();
                cart.cart_id = w->property("cartId").toString().toStdString();
                cart.issuer = label.section('\n', 0, 0).toStdString();
                cart.title = label.section('\n', 1, 1).toStdString();
                cart.cart_type = std::to_string(w->property("cartType").toInt());
                cart.length_ms = w->property("durationMs").toInt();
                if (!cart.cart_id.empty()) {
                    db_->log_cart(cart.cart_id);
                    prompt_queue_choice_and_enqueue(cart, true);
                }
                append_activity(QString("Double-click play requested: %1").arg(label));
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::prompt_queue_choice_and_enqueue(const Cart& cart, bool play_next) {
    const bool currentlyPlaying = automation_.is_playing();
    const auto current = automation_.current_cart_snapshot();

    if (currentlyPlaying && current.has_value() && current->cart_id != cart.cart_id) {
        QMessageBox box(this);
        box.setWindowTitle("Queue action");
        box.setText(QString("A cart is already playing: %1 - %2").arg(QString::fromStdString(current->issuer), QString::fromStdString(current->title)));
        box.setInformativeText("Do you want to replace the current queue or append after the current track?");
        auto* replaceButton = box.addButton("Replace queue", QMessageBox::AcceptRole);
        auto* appendButton = box.addButton("Append after current", QMessageBox::ActionRole);
        auto* cancelButton = box.addButton(QMessageBox::Cancel);
        box.exec();

        if (box.clickedButton() == cancelButton) {
            return;
        }
        if (box.clickedButton() == replaceButton) {
            automation_.clear_queue();
            automation_.enqueue_cart(cart);
            automation_.start();
            append_activity(QString("Replaced queue with: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
            refresh_automation_view();
            return;
        }
        automation_.append_cart(cart);
        automation_.start();
        append_activity(QString("Appended after current: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
        refresh_automation_view();
        return;
    }

    if (play_next) {
        automation_.enqueue_cart(cart);
        automation_.start();
    } else {
        automation_.append_cart(cart);
    }
    append_activity(QString("Queued: %1 - %2").arg(QString::fromStdString(cart.issuer), QString::fromStdString(cart.title)));
    refresh_automation_view();
}

void MainWindow::update_playback_status() {
    const auto current = automation_.current_cart_snapshot();
    if (!current.has_value()) {
        if (now_playing_label_) {
            now_playing_label_->setText("Now playing: nothing");
        }
        if (now_playing_time_) {
            now_playing_time_->setText("00:00");
        }
        return;
    }

    const auto started = automation_.current_started_at();
    const auto elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - started).count());
    const int remainingMs = std::max(0, current->length_ms - elapsedMs);
    const QString elapsedText = format_duration(std::max(0, elapsedMs));
    const QString remainingText = format_duration(remainingMs);

    if (now_playing_label_) {
        now_playing_label_->setText(QString("Now playing: %1 - %2").arg(QString::fromStdString(current->issuer), QString::fromStdString(current->title)));
    }
    if (now_playing_time_) {
        now_playing_time_->setText(QString("%1 remaining • %2 elapsed").arg(remainingText, elapsedText));
    }
    sync_cart_preview();
}

void MainWindow::sync_cart_preview() {
    const auto current = automation_.current_cart_snapshot();
    const auto queue = automation_.queue_snapshot();

    if (cart_current_label_) {
        if (current.has_value()) {
            const auto started = automation_.current_started_at();
            const auto elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - started).count());
            const int remainingMs = std::max(0, current->length_ms - elapsedMs);
            cart_current_label_->setText(QString("Current: %1 - %2 (%3 left)").arg(QString::fromStdString(current->issuer), QString::fromStdString(current->title), format_duration(remainingMs)));
        } else {
            cart_current_label_->setText("Current: none");
        }
    }

    if (cart_next_label_) {
        if (!queue.empty()) {
            cart_next_label_->setText(QString("Next: %1 - %2").arg(QString::fromStdString(queue.front().issuer), QString::fromStdString(queue.front().title)));
        } else {
            cart_next_label_->setText("Next: none");
        }
    }

    if (cart_preview_queue_) {
        cart_preview_queue_->clear();
        std::size_t count = 0;
        for (const auto& item : queue) {
            auto* preview = new QListWidgetItem(QString("%1 - %2 [%3]").arg(QString::fromStdString(item.issuer), QString::fromStdString(item.title), format_duration(item.length_ms)), cart_preview_queue_);
            preview->setForeground(QBrush(QColor("#1a8a2f")));
            if (++count >= 8) {
                break;
            }
        }
        if (queue.empty()) {
            cart_preview_queue_->addItem("Queue is empty.");
        }
    }
}

void MainWindow::refresh_automation_view() {
    const auto queue = automation_.queue_snapshot();
    update_queue_list(queue);
    update_overview();
    update_playback_status();
    sync_cart_preview();

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
                       QSettings settings;
                       const int search_limit = std::max(1, settings.value("studio/searchResultLimit", 24).toInt());
                       int rendered = 0;

                       studio_results_->clear();
                       for (const auto& cart : result.carts) {
                           if (rendered >= search_limit) {
                               break;
                           }
                           auto* item = new QListWidgetItem("[Cart] " + format_item_line(cart) + QString(" [%1]").arg(format_duration(cart.length_ms)), studio_results_);
                            item->setData(KindRole, "cart");
                            item->setData(IdRole, QString::fromStdString(cart.cart_id));
                            item->setData(IssuerRole, QString::fromStdString(cart.issuer));
                            item->setData(TitleRole, QString::fromStdString(cart.title));
                            item->setData(FileRole, QString::fromStdString(cart.filename));
                            item->setData(TypeRole, QString::fromStdString(cart.cart_type));
                           item->setData(DurationRole, format_duration(cart.length_ms));
                           item->setForeground(QBrush(QColor("#1a8a2f")));
                           ++rendered;
                       }
                       for (const auto& track : result.tracks) {
                           if (rendered >= search_limit) {
                               break;
                           }
                           auto* item = new QListWidgetItem(QString("[Track] %1 - %2 [%3]").arg(QString::fromStdString(track.artist), QString::fromStdString(track.title), format_duration(track.length_ms)), studio_results_);
                            item->setData(KindRole, "track");
                            item->setData(IdRole, QString::fromStdString(track.track_id));
                            item->setData(IssuerRole, QString::fromStdString(track.artist));
                            item->setData(TitleRole, QString::fromStdString(track.title));
                            item->setData(FileRole, QString::fromStdString(track.filename));
                            item->setData(TypeRole, QString::fromStdString(track.rotation));
                           item->setData(DurationRole, format_duration(track.length_ms));
                           item->setForeground(QBrush(QColor("#1a8a2f")));
                           ++rendered;
                       }
                       if (studio_results_->count() == 0) {
                           studio_results_->addItem("No results found.");
                       }
                       append_activity(QString("Studio search returned %1 cart(s) and %2 track(s); showing %3 item(s).").arg(static_cast<int>(result.carts.size())).arg(static_cast<int>(result.tracks.size())).arg(rendered));
                       statusBar()->showMessage(QString("Search complete: %1 carts, %2 tracks").arg(static_cast<int>(result.carts.size())).arg(static_cast<int>(result.tracks.size())), 3000);
                       set_busy(false);
                   });
}

void MainWindow::show_easter_egg() {
    append_activity("Hidden card opened.");
    EasterEggDialog dialog(this);
    dialog.exec();
}

void MainWindow::open_settings_dialog() {
    SettingsDialog dialog(studio_window_ ? studio_window_ : this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QSettings settings;
    settings.setValue("network/baseSearchUrl", dialog.base_search_url());
    settings.setValue("storage/libraryPrefix", dialog.library_prefix());
    settings.setValue("studio/searchResultLimit", dialog.search_result_limit());
    settings.setValue("audio/defaultOutput", dialog.audio_output());
    settings.setValue("ui/theme", dialog.theme());
    settings.setValue("cart/autoRefreshOnStart", dialog.auto_refresh_carts());
    settings.setValue("ui/verboseActivityLog", dialog.verbose_activity_log());

    if (auto* client = dynamic_cast<DatabaseClient*>(db_.get())) {
        client->set_api_base_url(dialog.base_search_url().toStdString());
        client->set_library_prefix(dialog.library_prefix().toStdString());
    }

    if (audio_output_combo_) {
        audio_output_combo_->setCurrentText(dialog.audio_output());
    }
    if (audio_output_status_) {
        audio_output_status_->setText(QString("Selected output: %1").arg(dialog.audio_output()));
    }

    const auto theme_name = dialog.theme();
    if (!theme_name.isEmpty()) {
        qApp->setStyle(theme_name);
        apply_theme();
    }

    refresh_carts_view_async();
    refresh_automation_view();

    if (dialog.verbose_activity_log()) {
        append_activity("Verbose activity logging enabled.");
    }

    append_activity(QString("Settings saved: base URL %1, library path %2.").arg(dialog.base_search_url(), dialog.library_prefix()));
    statusBar()->showMessage("Settings saved", 2500);
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
    if (install_action_) {
        install_action_->setEnabled(!busy);
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

void MainWindow::append_playback_warning(const QString& message) {
    if (!playback_error_window_) {
        playback_error_window_ = new QDialog(studio_window_);
        playback_error_window_->setWindowTitle("Playback errors");
        playback_error_window_->setWindowModality(Qt::NonModal);
        playback_error_window_->setAttribute(Qt::WA_DeleteOnClose, false);

        auto* layout = new QVBoxLayout(playback_error_window_);
        auto* label = new QLabel("Missing files are appended here while playback continues.", playback_error_window_);
        playback_error_log_ = new QPlainTextEdit(playback_error_window_);
        playback_error_log_->setReadOnly(true);
        playback_error_log_->setMinimumSize(520, 220);
        layout->addWidget(label);
        layout->addWidget(playback_error_log_);
    }

    if (playback_error_log_) {
        playback_error_log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message);
    }

    if (!playback_error_window_->isVisible()) {
        playback_error_window_->show();
    }
    playback_error_window_->raise();
    playback_error_window_->activateWindow();
    QApplication::alert(playback_error_window_, 0);
}

void MainWindow::update_queue_list(const std::vector<Cart>& queue) {
    queue_list_->clear();
    for (const auto& cart : queue) {
        auto* item = new QListWidgetItem(QString::fromStdString(cart.issuer + " - " + cart.title), queue_list_);
        item->setData(IdRole, QString::fromStdString(cart.cart_id));
        item->setData(IssuerRole, QString::fromStdString(cart.issuer));
        item->setData(TitleRole, QString::fromStdString(cart.title));
        item->setData(TypeRole, QString::fromStdString(cart.cart_type));
        item->setData(DurationRole, format_duration(cart.length_ms));
        item->setData(LengthRole, cart.length_ms);
        item->setForeground(QBrush(QColor("#1a8a2f")));
    }
    if (queue.empty()) {
        queue_list_->addItem("Queue is currently empty.");
    }
    sync_cart_preview();
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
            tile->setProperty("durationMs", cart.length_ms);
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
                                       prompt_queue_choice_and_enqueue(cart, true);
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