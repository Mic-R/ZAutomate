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
class QMenuBar;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QObject;
class QEvent;
class QAction;

namespace zautomate {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(std::unique_ptr<DatabaseProvider> db_client, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build_ui();
    void apply_theme();
    void refresh_automation_view();
    void refresh_carts_view();
    void refresh_carts_view_async();
    void run_studio_search();
    void show_easter_egg();
    void set_busy(bool busy, const QString& message = QString());
    void update_queue_list(const std::vector<Cart>& queue);
    void update_cart_tree(const std::unordered_map<int, std::vector<Cart>>& carts_by_type);

    std::unique_ptr<DatabaseProvider> db_;
    AutomationModule automation_;
    StudioModule studio_;
    CartMachineModule cart_machine_;

    QLabel* hero_title_{nullptr};
    QLabel* automation_summary_{nullptr};
    QLabel* cart_summary_{nullptr};
    QLabel* status_hint_{nullptr};
    QLineEdit* studio_query_{nullptr};
    QListWidget* queue_list_{nullptr};
    QListWidget* studio_results_{nullptr};
    QTreeWidget* cart_tree_{nullptr};
    QAction* refresh_all_action_{nullptr};
    QAction* search_action_{nullptr};
    QAction* easter_egg_action_{nullptr};
    QPushButton* studio_search_button_{nullptr};
    QPushButton* cart_refresh_button_{nullptr};
    QTabWidget* tabs_{nullptr};
};

}  // namespace zautomate