#include "PowerActions.h"

#include <QDebug>
#include <QTimer>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <powrprof.h>
#endif

void PowerActions::lock()
{
#ifdef Q_OS_WIN
    LockWorkStation();
#endif
}

void PowerActions::sleep()
{
#ifdef Q_OS_WIN
    // Let the click land and the notch tuck itself away before the machine
    // suspends, so the animation is not frozen mid-frame in the screenshot the
    // lock screen keeps.
    QTimer::singleShot(300, [] {
        if (!SetSuspendState(FALSE, FALSE, FALSE))
            qWarning("noch: sleep request failed (error %lu)", GetLastError());
    });
#endif
}
