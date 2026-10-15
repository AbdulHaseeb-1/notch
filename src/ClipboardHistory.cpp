#include "ClipboardHistory.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSettings>
#include <QVariantMap>

#include <algorithm>

namespace {
const char kEnabledKey[] = "clipboard/enabled";

// Formats that apps use to say "do not keep this" (see Windows' clipboard
// history documentation). Qt lists Windows formats inside a mime type string.
bool mentionsPrivateFormat(const QString &format)
{
    return format.contains(QLatin1String("ExcludeClipboardContentFromMonitorProcessing"), Qt::CaseInsensitive)
        || format.contains(QLatin1String("x-kde-passwordManagerHint"), Qt::CaseInsensitive);
}

QString previewOf(const QString &text)
{
    // First non-empty line, whitespace collapsed, cut to a length that fits two lines.
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString first;
    for (const QString &line : lines) {
        first = line.simplified();
        if (!first.isEmpty())
            break;
    }
    constexpr int kMax = 160;
    if (first.size() > kMax)
        first = first.left(kMax).trimmed() + QChar(0x2026);
    return first;
}
} // namespace

ClipboardHistory::ClipboardHistory(QObject *parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
{
    const QSettings settings;
    m_enabled = settings.value(QLatin1String(kEnabledKey), false).toBool();
    if (m_clipboard)
        connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardHistory::onClipboardChanged);
    if (m_enabled)
        QMetaObject::invokeMethod(this, &ClipboardHistory::onClipboardChanged, Qt::QueuedConnection);
}

void ClipboardHistory::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings settings;
    settings.setValue(QLatin1String(kEnabledKey), m_enabled);
    emit enabledChanged();
    if (m_enabled) {
        onClipboardChanged(); // pick up what is on the clipboard right now
    } else if (!m_entries.isEmpty()) {
        m_entries.clear();    // turning it off forgets everything
        emit itemsChanged();
    }
}

QVariantList ClipboardHistory::items() const
{
    QVariantList list;
    list.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        const int lines = int(entry.text.count(QLatin1Char('\n'))) + 1;
        QVariantMap map;
        map.insert(QStringLiteral("text"), entry.text);
        map.insert(QStringLiteral("preview"), previewOf(entry.text));
        map.insert(QStringLiteral("chars"), int(entry.text.size()));
        map.insert(QStringLiteral("lines"), lines);
        map.insert(QStringLiteral("pinned"), entry.pinned);
        map.insert(QStringLiteral("when"), entry.when);
        list.append(map);
    }
    return list;
}

bool ClipboardHistory::isPrivate(const QMimeData *data) const
{
    const QStringList formats = data->formats();
    for (const QString &format : formats) {
        if (mentionsPrivateFormat(format))
            return true;
        // "CanIncludeInClipboardHistory" carries a DWORD: 0 means keep it out.
        if (format.contains(QLatin1String("CanIncludeInClipboardHistory"), Qt::CaseInsensitive)) {
            const QByteArray value = data->data(format);
            if (!value.isEmpty() && value.at(0) == 0)
                return true;
        }
    }
    return false;
}

void ClipboardHistory::onClipboardChanged()
{
    if (!m_enabled || !m_clipboard)
        return;
    const QMimeData *data = m_clipboard->mimeData();
    if (!data || !data->hasText() || isPrivate(data))
        return;
    const QString text = data->text();
    if (text.trimmed().isEmpty() || text.size() > kMaxChars)
        return;
    record(text);
}

void ClipboardHistory::record(const QString &text)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const Entry &e) { return e.text == text; });
    if (it != m_entries.end()) {
        it->when = QDateTime::currentDateTime();
        if (!it->pinned) {
            const Entry moved = *it;
            m_entries.erase(it);
            m_entries.prepend(moved);
        }
    } else {
        Entry entry;
        entry.text = text;
        entry.when = QDateTime::currentDateTime();
        m_entries.prepend(entry);
        // Keep the newest kMaxEntries unpinned entries.
        int unpinned = 0;
        for (int i = 0; i < m_entries.size();) {
            if (m_entries.at(i).pinned) {
                ++i;
            } else if (++unpinned > kMaxEntries) {
                m_entries.removeAt(i);
            } else {
                ++i;
            }
        }
    }
    sortPinnedFirst();
    emit itemsChanged();
}

void ClipboardHistory::sortPinnedFirst()
{
    std::stable_partition(m_entries.begin(), m_entries.end(), [](const Entry &e) { return e.pinned; });
}

bool ClipboardHistory::copy(int index)
{
    if (!m_clipboard || index < 0 || index >= m_entries.size())
        return false;
    // setText fires dataChanged, which moves this entry to the top.
    m_clipboard->setText(m_entries.at(index).text);
    return true;
}

void ClipboardHistory::remove(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    emit itemsChanged();
}

void ClipboardHistory::togglePin(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    Entry &entry = m_entries[index];
    entry.pinned = !entry.pinned;
    if (!entry.pinned) {
        // An unpinned entry rejoins the list at the top of the unpinned ones.
        const Entry moved = entry;
        m_entries.removeAt(index);
        int firstUnpinned = 0;
        while (firstUnpinned < m_entries.size() && m_entries.at(firstUnpinned).pinned)
            ++firstUnpinned;
        m_entries.insert(firstUnpinned, moved);
    } else {
        sortPinnedFirst();
    }
    emit itemsChanged();
}

void ClipboardHistory::clearUnpinned()
{
    const int before = int(m_entries.size());
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [](const Entry &e) { return !e.pinned; }),
                    m_entries.end());
    if (m_entries.size() != before)
        emit itemsChanged();
}
