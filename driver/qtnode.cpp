#include "qtnode.h"

#include "introspection.h"

#include <QDebug>

#ifdef QT5_SUPPORT
  #include <QtWidgets/QGraphicsScene>
  #include <QtWidgets/QGraphicsObject>
  #include <QtQml/QQmlEngine>
  #include <QtQml/QQmlContext>
  #include <QtQuick/QQuickView>
  #include <QtQuick/QQuickItem>
#else
  #include <QGraphicsScene>
  #include <QGraphicsObject>
#endif
#include <QDBusArgument>

// Marshall the NodeIntrospectionData data into a D-Bus argument
QDBusArgument &operator<<(QDBusArgument &argument, const NodeIntrospectionData &node_data)
{
    argument.beginStructure();
    argument << node_data.object_path << node_data.state;
    argument.endStructure();
    return argument;
}

// Retrieve the NodeIntrospectionData data from the D-Bus argument
const QDBusArgument &operator>>(const QDBusArgument &argument, NodeIntrospectionData &node_data)
{
    argument.beginStructure();
    argument >> node_data.object_path >> node_data.state;
    argument.endStructure();
    return argument;
}

const QByteArray AP_ID_NAME("_autopilot_id");

QObjectNode::QObjectNode(QObject *obj, DBusNode::Ptr parent)
: object_(obj)
, parent_(parent)
{
    std::string parent_path = parent ? parent->GetPath() : "";
    full_path_ = parent_path + "/" + GetName();
}

QObjectNode::QObjectNode(QObject* obj)
: object_(obj)
{
    full_path_ = "/" + GetName();
}

QObject* QObjectNode::getWrappedObject() const
{
    return object_;
}

NodeIntrospectionData QObjectNode::GetIntrospectionData() const
{
    NodeIntrospectionData data;
    data.object_path = QString::fromStdString(GetPath());
    data.state = GetNodeProperties(object_);
    data.state["id"] = PackProperty(GetId());
    return data;
}

std::string QObjectNode::GetName() const
{
    QString name = object_->metaObject()->className();

    // QML type names get mangled by Qt - they get _QML_N or _QMLTYPE_N appended.
    //
    if (name.contains('_'))
        name = name.split('_').front();
    return name.toStdString();
}

std::string QObjectNode::GetPath() const
{
    return full_path_;
}

int32_t QObjectNode::GetId() const
{
    // Note: This method is used to assign ids to both the root node (with a QApplication object) and
    // child nodes. This used to be separate code, but now that we export QApplication properties,
    // we can use this one method everywhere.
    static int32_t next_id=0;

    QList<QByteArray> property_names = object_->dynamicPropertyNames();
    if (!property_names.contains(AP_ID_NAME))
    {
        int32_t new_id = ++next_id;
        object_->setProperty(AP_ID_NAME, QVariant(new_id));
    }
    return qvariant_cast<int32_t>(object_->property(AP_ID_NAME));
}

bool QObjectNode::MatchStringProperty(const std::string& name, const std::string& value) const
{
    QVariantMap properties = GetNodeProperties(object_);

    QString qname = QString::fromStdString(name);
    if (! properties.contains(qname))
        return false;

    QVariant object_value = qvariant_cast<QVariantList>(properties[qname]).at(1);
    QVariant check_value(QString::fromStdString(value));
    if (check_value.canConvert(object_value.type()))
    {
        check_value.convert(object_value.type());
        return check_value == object_value;
    }

    return false;
}

bool QObjectNode::MatchIntegerProperty(const std::string& name, int32_t value) const
{
    if (name == "id")
        return value == GetId();

    QVariantMap properties = GetNodeProperties(object_);

    QString qname = QString::fromStdString(name);
    if (! properties.contains(qname))
        return false;

    QVariant object_value = qvariant_cast<QVariantList>(properties[qname]).at(1);
    QVariant check_value(value);
    if (check_value.canConvert(object_value.type()))
    {
        check_value.convert(object_value.type());
        return check_value == object_value;
    }

    return false;
}

bool QObjectNode::MatchBooleanProperty(const std::string& name, bool value) const
{
    QVariantMap properties = GetNodeProperties(object_);

    QString qname = QString::fromStdString(name);
    if (! properties.contains(qname))
        return false;

    QVariant object_value = qvariant_cast<QVariantList>(properties[qname]).at(1);
    QVariant check_value(value);

    if (check_value.canConvert(object_value.type()))
    {
        check_value.convert(object_value.type());
        return check_value == object_value;
    }

    return false;
}

xpathselect::NodeVector QObjectNode::Children() const
{
    xpathselect::NodeVector children;

#ifdef QT5_SUPPORT
    // Qt5's hierarchy for QML has changed a bit:
    // - On top there's a QQuickView which holds all the QQuick items
    // - QQuickItems don't always follow the QObject type hierarchy (e.g. QQuickListView does not), therefore we use the QQuickItem's childItems()
    // - In case it is not a QQuickItem, fall back to the standard QObject hierarchy

    QQuickView *view = qobject_cast<QQuickView*>(object_);
    if (view && view->rootObject() != 0) {
        children.push_back(std::make_shared<QObjectNode>(view->rootObject(), shared_from_this()));
    }

    QQuickItem* item = qobject_cast<QQuickItem*>(object_);
    if (item) {
        foreach (QQuickItem *childItem, item->childItems()) {
            if (childItem->parentItem() == item) {
                children.push_back(std::make_shared<QObjectNode>(childItem, shared_from_this()));
            }
        }
    } else {
        foreach (QObject *child, object_->children())
        {
            if (child->parent() == object_)
                children.push_back(std::make_shared<QObjectNode>(child, shared_from_this()));
        }
    }

#else
    foreach (QObject *child, object_->children())
    {
        if (child->parent() == object_)
            children.push_back(std::make_shared<QObjectNode>(child, shared_from_this()));
    }

    // If our wrapped object is a QGraphicsScene, we need to explicitly grab any child graphics
    // items that are derived from QObjects. Declarative UIs use this idiom, so this need to be
    // done to support QML applications.
    QGraphicsScene *scene = qobject_cast<QGraphicsScene*>(object_);
    if (scene)
    {
        QList<QGraphicsItem*> child_items = scene->items();
        foreach(QGraphicsItem* item, child_items)
        {
            QGraphicsObject *obj = item->toGraphicsObject();
            if (obj && ! obj->parent())
                children.push_back(std::make_shared<QObjectNode>(obj, shared_from_this()));
        }
    }
#endif

    return children;
}


xpathselect::Node::Ptr QObjectNode::GetParent() const
{
    return parent_;
}
