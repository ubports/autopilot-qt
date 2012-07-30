/*
Copyright 2012 Canonical

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License version 3, as published
by the Free Software Foundation.
*/

#include "qttestability.h"
#include "dbus_adaptor.h"
#include "dbus_object.h"

#include <QCoreApplication>
#include <QDebug>
#include <QtDBus>
#include <qindicateserver.h>

void qt_testability_init(void)
{
    qDebug() << "In Testability init function.";


    DBusObject* obj = new DBusObject;
    new AutopilotAdaptor(obj);

    QDBusConnection connection = QDBusConnection::sessionBus();

    if (!connection.registerObject("/", obj))
    {
        qDebug("Unable to register object!");
    }

    QIndicate::Server* server = QIndicate::Server::defaultInstance();
    server->setType("autopilot");
    server->setProperty("autopilot_service", connection.baseService());
    server->show();

}
