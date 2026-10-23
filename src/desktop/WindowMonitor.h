#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>

// The one place that lists the windows on the desktop. Running programs, the
// full-screen guard and the music pill's space, and the pinned dock all read
// the same listing, so Noch walks the window list once a second instead of
// each feature walking it on its own timer.
class WindowMonitor final : public QObject
{
    Q_OBJECT

public:
    // What Windows reports about one visible top-level window. Consumers apply
    // their own rules to these facts; `program` is the palette and dock rule.
    struct Window {
        double id = 0;                  // the window handle, as a number
        quint32 pid = 0;                // the process that owns the window
        bool self = false;              // one of Noch's own windows
        bool minimized = false;
        bool owned = false;             // has an owner window (dialogs, popups)
        bool toolWindow = false;
        bool appWindow = false;
        bool noActivate = false;
        bool transparent = false;       // click-through
        bool nearlyInvisible = false;   // layered and almost fully transparent
        bool cloaked = false;           // on another virtual desktop
        bool program = false;           // a user-facing program window
        QString title;
        QString className;
        QString path;                   // the program's full path ("" when unknown)
        QString exe;                    // lower-case file name of path
        QRect rect;                     // the window as drawn (empty when unknown)
    };

    explicit WindowMonitor(QObject *parent = nullptr);

    // The windows found by the last listing, topmost first.
    const QList<Window> &windows() const { return m_windows; }

    // Lists the windows again now and tells every consumer through refreshed().
    void refresh();

signals:
    void refreshed();

private:
    QList<Window> m_windows;
    QTimer m_timer;
};
