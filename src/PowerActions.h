#pragma once

#include <QObject>

// Lock and sleep, from the System page's power row.
class PowerActions final : public QObject
{
    Q_OBJECT

public:
    explicit PowerActions(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void lock();
    Q_INVOKABLE void sleep();
};
