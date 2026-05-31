#pragma once

#include <memory>
#include <unordered_map>

#include <QMainWindow>
#include <QString>

#include "zautomate/cart.hpp"
#include "zautomate/database_provider.hpp"
#include "zautomate/modules.hpp"
#include "zautomate/track.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QDialog;
class QCheckBox;
class QDialogButtonBox;
class QFormLayout;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QToolBar;
class QObject;
class QEvent;
class QAction;
class QWidget;
class QGridLayout;
class QScrollArea;
class QTimer;
class QComboBox;
class QTabWidget;

namespace zautomate {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(std::unique_ptr<DatabaseProvider> db_client, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build_ui();
    void apply_theme();
    void update_overview();
    void refresh_automation_view();
    void refresh_carts_view();
    void refresh_carts_view_async();
    void run_studio_search();
    void open_settings_dialog();
    void show_easter_egg();
    void set_busy(bool busy, const QString& message = QString());
    void append_activity(const QString& message);
    void append_playback_warning(const QString& message);
    void update_playback_status();
    void prompt_queue_choice_and_enqueue(const Cart& cart, bool play_next);
    void sync_cart_preview();
    void update_queue_list(const std::vector<Cart>& queue);
    void update_cart_tree(const std::unordered_map<int, std::vector<Cart>>& carts_by_type);

    std::unique_ptr<DatabaseProvider> db_;
    AutomationModule automation_;
    StudioModule studio_;
    std::unique_ptr<CartMachineModule> cart_machine_;

    QLabel* hero_title_{nullptr};
    QLabel* overview_status_{nullptr};
    QLabel* automation_summary_{nullptr};
    QLabel* cart_summary_{nullptr};
    QLabel* status_hint_{nullptr};
    QLineEdit* studio_query_{nullptr};
    QListWidget* queue_list_{nullptr};
    QListWidget* studio_results_{nullptr};
    QTreeWidget* cart_tree_{nullptr};
    QWidget* cart_grid_container_{nullptr};
    QGridLayout* cart_grid_layout_{nullptr};
    QScrollArea* cart_scroll_{nullptr};
    QListWidget* activity_log_{nullptr};
    QDialog* playback_error_window_{nullptr};
    QPlainTextEdit* playback_error_log_{nullptr};
    QLabel* now_playing_label_{nullptr};
    QLabel* now_playing_time_{nullptr};
    QTimer* playback_timer_{nullptr};
    QLabel* cart_current_label_{nullptr};
    QLabel* cart_next_label_{nullptr};
    QListWidget* cart_preview_queue_{nullptr};
    QComboBox* audio_output_combo_{nullptr};
    QLabel* audio_output_status_{nullptr};
    QAction* refresh_all_action_{nullptr};
    QAction* search_action_{nullptr};
    QAction* settings_action_{nullptr};
    QAction* easter_egg_action_{nullptr};
    QPushButton* studio_search_button_{nullptr};
    QPushButton* cart_refresh_button_{nullptr};
    QMainWindow* studio_window_{nullptr};
    QMainWindow* cart_window_{nullptr};
    QMainWindow* automation_window_{nullptr};
};

}  // namespace zautomate