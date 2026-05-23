#include "zautomate/gui/easter_egg_dialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

namespace zautomate {

EasterEggDialog::EasterEggDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Hidden Session Card");
    setModal(true);
    setMinimumSize(520, 620);
    setObjectName("easterEggDialog");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    auto* title = new QLabel("Wow such secret card!");
    title->setObjectName("easterEggTitle");
    title->setWordWrap(true);

    auto* caption = new QLabel("Kiss boys. Stay gay. Commit crimes.");
    caption->setObjectName("easterEggCaption");
    caption->setWordWrap(true);

    auto* image = new QLabel;
    image->setAlignment(Qt::AlignCenter);
    image->setObjectName("easterEggImage");
    QPixmap pixmap(":/assets/boykisser.png");
    image->setPixmap(pixmap.scaled(440, 440, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    root->addWidget(title);
    root->addWidget(caption);
    root->addWidget(image, 1);
    root->addWidget(buttons);

    setStyleSheet(R"(
        QDialog#easterEggDialog {
            background: #111214;
            color: #f2efe8;
        }
        QLabel#easterEggTitle {
            font-size: 22px;
            font-weight: 700;
            color: #f7f3ec;
        }
        QLabel#easterEggCaption {
            color: #a9adb7;
        }
        QLabel#easterEggImage {
            background: #1a1b1f;
            border: 1px solid #2d3138;
            border-radius: 20px;
            padding: 12px;
        }
    )");
}

}  // namespace zautomate