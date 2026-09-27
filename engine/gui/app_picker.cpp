// MixCast GUI - "Add app" dialog.
#include "app_picker.h"
#include "channel_strip.h"
#include "devices.h"
#include "theme.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kExeRole  = Qt::UserRole;
constexpr int kPathRole = Qt::UserRole + 1;

QIcon IconForExe(const QString& path)
{
    if (!path.isEmpty() && QFileInfo::exists(path))
    {
        QIcon ic = QFileIconProvider().icon(QFileInfo(path));
        if (!ic.isNull()) return ic;
    }
    return theme::AppFallbackIcon();
}

bool IsSoundboard(const QString& exe)
{
    return exe.contains(QStringLiteral("soundpad"), Qt::CaseInsensitive) ||
           exe.contains(QStringLiteral("voicemod"), Qt::CaseInsensitive);
}
} // namespace

AppPicker::AppPicker(const QStringList& alreadyAdded, QWidget* parent)
    : QDialog(parent), hidden_(alreadyAdded)
{
    setWindowTitle(QStringLiteral("Add an app to the mix"));
    setMinimumSize(420, 480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(12);

    auto* intro = new QLabel(QStringLiteral("Pick an app that's playing sound. Only its audio is sent, nothing else."));
    intro->setObjectName(QStringLiteral("Muted"));
    intro->setWordWrap(true);
    root->addWidget(intro);

    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("Search apps"));
    search_->setClearButtonEnabled(true);
    root->addWidget(search_);

    list_ = new QListWidget;
    list_->setIconSize(QSize(24, 24));
    root->addWidget(list_, 1);

    auto* browse = new QPushButton(QStringLiteral("Browse for an .exe\u2026"));
    browse->setToolTip(QStringLiteral("Add an app that isn't running yet. It joins the mix when it starts."));
    root->addWidget(browse, 0, Qt::AlignLeft);

    duck_ = new QCheckBox(QStringLiteral("Lower it while I talk"));
    duck_->setChecked(true);
    root->addWidget(duck_);

    warning_ = new QLabel(QStringLiteral(
        "Discord plays your friends' voices. Adding it sends them back through your mic, "
        "so they will hear themselves."));
    warning_->setObjectName(QStringLiteral("Warning"));
    warning_->setWordWrap(true);
    warning_->hide();
    root->addWidget(warning_);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    addBtn_ = new QPushButton(QStringLiteral("Add to mix"));
    addBtn_->setObjectName(QStringLiteral("Primary"));
    addBtn_->setEnabled(false);
    addBtn_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(addBtn_);
    root->addLayout(buttons);

    connect(search_, &QLineEdit::textChanged, this, &AppPicker::applyFilter);
    connect(list_, &QListWidget::itemSelectionChanged, this, &AppPicker::onSelectionChanged);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { if (addBtn_->isEnabled()) addBtn_->click(); });
    connect(browse, &QPushButton::clicked, this, &AppPicker::onBrowse);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(addBtn_, &QPushButton::clicked, this, [this] {
        auto* item = list_->currentItem();
        if (!item) return;
        picked_.exe  = item->data(kExeRole).toString();
        picked_.path = item->data(kPathRole).toString();
        picked_.duck = duck_->isChecked();
        accept();
    });

    populate();
}

void AppPicker::populate()
{
    const DWORD self = GetCurrentProcessId();
    for (const auto& p : mixcast::ListAudioProcesses())
    {
        const QString exe = QString::fromStdWString(p.exe);
        if (p.pid == self || hidden_.contains(exe, Qt::CaseInsensitive)) continue;

        const QString path = QString::fromStdWString(p.path);
        auto* item = new QListWidgetItem(IconForExe(path), DisplayNameForExe(exe));
        item->setData(kExeRole, exe);
        item->setData(kPathRole, path);
        item->setToolTip(path.isEmpty() ? exe : path);
        list_->addItem(item);
    }

    if (list_->count() == 0)
    {
        auto* empty = new QListWidgetItem(QStringLiteral("Nothing is playing sound right now. "
                                                         "Start the app, or browse for its .exe."));
        empty->setFlags(Qt::NoItemFlags);
        list_->addItem(empty);
    }
}

void AppPicker::applyFilter(const QString& text)
{
    for (int i = 0; i < list_->count(); i++)
    {
        auto* item = list_->item(i);
        item->setHidden(!text.isEmpty() && !item->text().contains(text, Qt::CaseInsensitive));
    }
}

void AppPicker::onSelectionChanged()
{
    auto* item = list_->currentItem();
    const bool valid = item && (item->flags() & Qt::ItemIsSelectable);
    addBtn_->setEnabled(valid);
    if (!valid) { warning_->hide(); return; }

    const QString exe = item->data(kExeRole).toString();
    duck_->setChecked(!IsSoundboard(exe));
    warning_->setVisible(exe.contains(QStringLiteral("discord"), Qt::CaseInsensitive));
}

void AppPicker::onBrowse()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose an app"),
                                                      QString(), QStringLiteral("Programs (*.exe)"));
    if (path.isEmpty()) return;

    const QString exe = QFileInfo(path).fileName();
    auto* item = new QListWidgetItem(IconForExe(path), DisplayNameForExe(exe));
    item->setData(kExeRole, exe);
    item->setData(kPathRole, path);
    item->setToolTip(path);
    list_->insertItem(0, item);
    list_->setCurrentItem(item);
}
