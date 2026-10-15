#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class QClipboard;
class QMimeData;

// Remembers what was copied, so it can be copied again from the notch.
//   - Text only. Images and files are ignored.
//   - Off until the user turns it on (Clipboard tab), and kept in memory only:
//     nothing is written to disk, and everything is gone when Noch quits.
//   - Copies that apps mark as private (password managers set the Windows
//     "ExcludeClipboardContentFromMonitorProcessing" / "CanIncludeInClipboardHistory"
//     formats) are skipped, the same as Windows' own clipboard history does.
//   - Pinned entries stay on top and survive "Clear".
class ClipboardHistory final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)

public:
    explicit ClipboardHistory(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QVariantList items() const;
    int count() const { return int(m_entries.size()); }

    // Puts entry `index` back on the clipboard and moves it to the top.
    Q_INVOKABLE bool copy(int index);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void togglePin(int index);
    // Removes everything that is not pinned.
    Q_INVOKABLE void clearUnpinned();

    static constexpr int kMaxEntries = 30;
    static constexpr int kMaxChars = 20000;

signals:
    void enabledChanged();
    void itemsChanged();

private:
    struct Entry {
        QString text;
        bool pinned = false;
        QDateTime when;
    };

    void onClipboardChanged();
    bool isPrivate(const QMimeData *data) const;
    void record(const QString &text);
    void sortPinnedFirst();

    QClipboard *m_clipboard = nullptr;
    QList<Entry> m_entries;
    bool m_enabled = false;
};
