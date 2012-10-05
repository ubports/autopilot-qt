/*
Copyright 2012 Canonical

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License version 3, as published
by the Free Software Foundation.
*/


#include <xpathselect/node.h>
#include <xpathselect/xpathselect.h>

#include <QDebug>

#if QT_VERSION >= 0x050000
  #include <QtGui/QGuiApplication>
  #include <QtWidgets/QGraphicsItem>
  #include <QtWidgets/QGraphicsScene>
  #include <QtWidgets/QGraphicsView>
  #include <QtWidgets/QWidget>
#else
  #include <QGraphicsItem>
  #include <QGraphicsScene>
  #include <QGraphicsView>
  #include <QApplication>
  #include <QWidget>
#endif

#include <QMap>
#include <QMetaProperty>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QRect>

#include "introspection.h"
#include "qtnode.h"
#include "rootnode.h"


QVariant IntrospectNode(QObject* obj);
QString GetNodeName(QObject* obj);
QStringList GetNodeChildNames(QObject* obj);
void AddCustomProperties(QObject* obj, QVariantMap& properties);

QList<QVariant> Introspect(QString const& query_string)
{
    QList<QVariant> state;
    QList<QtNode::Ptr> node_list = GetNodesThatMatchQuery(query_string);
    foreach (QtNode::Ptr obj, node_list)
    {
        state.append(obj->IntrospectNode());
    }

    return state;

}


QList<QtNode::Ptr> GetNodesThatMatchQuery(QString const& query_string)
{
#if QT_VERSION >= 0x050000
    std::shared_ptr<RootNode> root = std::make_shared<RootNode>(QGuiApplication::instance());
    foreach (QWindow *widget, QGuiApplication::topLevelWindows())
    {
        root->AddChild((QObject*) widget);
    }
#else
    std::shared_ptr<RootNode> root = std::make_shared<RootNode>(QApplication::instance());
    foreach (QWidget *widget, QApplication::topLevelWidgets())
    {
        root->AddChild((QObject*) widget);
    }
#endif
    QList<QtNode::Ptr> node_list;

    xpathselect::NodeList list = xpathselect::SelectNodes(root, query_string.toStdString());
    qDebug() << "XPathSelect library returned" << list.size() << "items.";
    for (auto node : list)
    {
        // node may be our root node wrapper *or* an ordinary qobject wrapper
        auto object_ptr = std::static_pointer_cast<QtNode>(node);
        if (object_ptr)
        {
            node_list.append(object_ptr);
        }

    }
    return node_list;
}


QVariant IntrospectNode(QObject* obj)
{
    // return must be (name, state_map)
    QString object_name = GetNodeName(obj);
    QVariantMap object_properties = GetNodeProperties(obj);
    QList<QVariant> object_tuple = { QVariant(object_name), QVariant(object_properties) };
    return QVariant(object_tuple);
}


QString GetNodeName(QObject* obj)
{
    return obj->metaObject()->className();
}


QVariantMap GetNodeProperties(QObject* obj)
{
    QVariantMap object_properties;
    const QMetaObject* meta = obj->metaObject();
    do
    {
        for(int i = meta->propertyOffset(); i < meta->propertyCount(); ++i)
        {
            QMetaProperty prop = meta->property(i);
            if (!prop.isValid())
            {
                qDebug() << "Property at index" << i << "Is not valid!";
                continue;
            }
            QVariant object_property = PackProperty(prop.read(obj));
            if (! object_property.isValid())
                continue;
            object_properties[prop.name()] = object_property;
        }
        meta = meta->superClass();
    } while(meta);

    AddCustomProperties(obj, object_properties);

    // add the 'Children' pseudo-property:
    QStringList children = GetNodeChildNames(obj);
    if (!children.empty())
        object_properties["Children"] = children;

    return object_properties;
}


void AddCustomProperties(QObject* obj, QVariantMap &properties)
{
    // Add any custom properties we need to the given QObject.
    // Add GlobalRect support for QWidget-derived classes
    QWidget *w = qobject_cast<QWidget*>(obj);
    if (w)
    {
        QRect r = w->rect();
        r = QRect(w->mapToGlobal(r.topLeft()), r.size());
        properties["globalRect"] = PackProperty(r);
    }
    // ...and support for QGraphicsItem-derived classes.
    else if (QGraphicsItem *i = qobject_cast<QGraphicsItem*>(obj))
    {
        // need to get the view that this item is in. Should only be one. If there's
        // more than one, we're in trouble.
        QGraphicsView *view = i->scene()->views().last();
        QRectF bounding_rect = i->boundingRect();
        bounding_rect = i->mapRectToScene(bounding_rect);
        QRect scene_rect = view->mapFromScene(bounding_rect).boundingRect();
        QRect global_rect = QRect(
                    view->mapToGlobal(scene_rect.topLeft()),
                    scene_rect.size());
        properties["globalRect"] = PackProperty(global_rect);
    }

}

QVariant PackProperty(QVariant const& prop)
{
    switch (prop.type())
    {
    case QVariant::Int:
    case QVariant::Bool:
    case QVariant::String:
    case QVariant::UInt:
    case QVariant::ULongLong:
    case QVariant::StringList:
    {
        return prop;
    }

    case QVariant::ByteArray:
    {
        return QVariant(QString(qvariant_cast<QByteArray>(prop)));
    }

    case QVariant::Point:
    {
        QPoint p = qvariant_cast<QPoint>(prop);
        QList<QVariant> l = {QVariant(p.x()), QVariant(p.y())};
        return QVariant(l);
    }

    case QVariant::Rect:
    {
        QRect r = qvariant_cast<QRect>(prop);
        QList<QVariant> l = {
            QVariant(r.x()),
            QVariant(r.y()),
            QVariant(r.width()),
            QVariant(r.height()) };
        return QVariant(l);
    }

    case QVariant::Size:
    {
        QSize s = qvariant_cast<QSize>(prop);
        QList<QVariant> l = { QVariant(s.width()), QVariant(s.height()) };
        return QVariant(l);
    }

    case QVariant::Color:
    {
        QColor color = qvariant_cast<QColor>(prop).toRgb();
        QList<QVariant> l = { QVariant(color.red()),
                              QVariant(color.green()),
                              QVariant(color.blue()),
                              QVariant(color.alpha())
                            };
        return QVariant(l);
    }

    default:
    {
        return QVariant();
    }
    }
}


QStringList GetNodeChildNames(QObject* obj)
{
    QStringList child_names;
    foreach (QObject *child, obj->children())
    {
        if (child->parent() == obj)
            child_names.append(GetNodeName(child));
    }
    return child_names;
}
