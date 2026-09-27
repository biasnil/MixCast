// MixCast GUI - "Add app" dialog.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

struct PickedApp
{
    QString exe;    // "Spotify.exe"
    QString path;   // full path, may be empty
    bool    duck = true;
};

class AppPicker : public QDialog
{
    Q_OBJECT
public:
    // alreadyAdded: exe names to hide (case-insensitive).
    explicit AppPicker(const QStringList& alreadyAdded, QWidget* parent = nullptr);
    PickedApp result() const { return picked_; }

private:
    void populate();
    void applyFilter(const QString& text);
    void onSelectionChanged();
    void onBrowse();

    QStringList  hidden_;
    QLineEdit*   search_  = nullptr;
    QListWidget* list_    = nullptr;
    QCheckBox*   duck_    = nullptr;
    QLabel*      warning_ = nullptr;
    QPushButton* addBtn_  = nullptr;
    PickedApp    picked_;
};
