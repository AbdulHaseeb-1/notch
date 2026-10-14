#pragma once

#include <QObject>
#include <QPointer>
#include <QRegion>
#include <QVariantList>

class QWindow;

class OverlayController final : public QObject
{
    Q_OBJECT

public:
    explicit OverlayController(QObject *parent = nullptr);
    void setWindow(QWindow *window);

    Q_INVOKABLE void setInteractiveRegions(const QVariantList &regions);

private:
    QPointer<QWindow> m_window;
    QRegion m_interactiveRegion;
};
