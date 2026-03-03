#include "InspectorServer.h"

#include <QQmlApplicationEngine>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QGuiApplication>
#include <QBuffer>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMetaProperty>
#include <QColor>
#include <QUrl>
#include <QDateTime>
#include <QThread>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString ptrStr(const QObject *obj)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(obj), 0, 16);
}

struct KeyInfo { Qt::Key key; QString text; };

static KeyInfo parseKeyName(const QString &name)
{
    static const QHash<QString, Qt::Key> special = {
        {QStringLiteral("Return"),   Qt::Key_Return},
        {QStringLiteral("Enter"),    Qt::Key_Enter},
        {QStringLiteral("Escape"),   Qt::Key_Escape},
        {QStringLiteral("Tab"),      Qt::Key_Tab},
        {QStringLiteral("Backtab"),  Qt::Key_Backtab},
        {QStringLiteral("Backspace"),Qt::Key_Backspace},
        {QStringLiteral("Delete"),   Qt::Key_Delete},
        {QStringLiteral("Insert"),   Qt::Key_Insert},
        {QStringLiteral("Left"),     Qt::Key_Left},
        {QStringLiteral("Right"),    Qt::Key_Right},
        {QStringLiteral("Up"),       Qt::Key_Up},
        {QStringLiteral("Down"),     Qt::Key_Down},
        {QStringLiteral("Home"),     Qt::Key_Home},
        {QStringLiteral("End"),      Qt::Key_End},
        {QStringLiteral("PageUp"),   Qt::Key_PageUp},
        {QStringLiteral("PageDown"), Qt::Key_PageDown},
        {QStringLiteral("Space"),    Qt::Key_Space},
        {QStringLiteral("F1"),  Qt::Key_F1},  {QStringLiteral("F2"),  Qt::Key_F2},
        {QStringLiteral("F3"),  Qt::Key_F3},  {QStringLiteral("F4"),  Qt::Key_F4},
        {QStringLiteral("F5"),  Qt::Key_F5},  {QStringLiteral("F6"),  Qt::Key_F6},
        {QStringLiteral("F7"),  Qt::Key_F7},  {QStringLiteral("F8"),  Qt::Key_F8},
        {QStringLiteral("F9"),  Qt::Key_F9},  {QStringLiteral("F10"), Qt::Key_F10},
        {QStringLiteral("F11"), Qt::Key_F11}, {QStringLiteral("F12"), Qt::Key_F12},
    };

    auto it = special.find(name);
    if (it != special.end())
        return {it.value(), name == QLatin1String("Space") ? QStringLiteral(" ") : QString()};

    if (name.length() == 1) {
        QChar ch = name.at(0);
        return {Qt::Key(ch.toUpper().unicode()), ch};
    }
    return {Qt::Key_unknown, {}};
}

static Qt::KeyboardModifiers parseModifiers(const QJsonArray &mods)
{
    Qt::KeyboardModifiers result = Qt::NoModifier;
    for (const QJsonValue &v : mods) {
        const QString m = v.toString();
        if (m == QLatin1String("Ctrl")  || m == QLatin1String("Control")) result |= Qt::ControlModifier;
        if (m == QLatin1String("Shift"))  result |= Qt::ShiftModifier;
        if (m == QLatin1String("Alt"))    result |= Qt::AltModifier;
        if (m == QLatin1String("Meta"))   result |= Qt::MetaModifier;
    }
    return result;
}

// Collect all QMetaProperty values of simple/printable types.
static QJsonObject collectSimpleProperties(QObject *obj)
{
    QJsonObject props;
    const QMetaObject *meta = obj->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        QMetaProperty mp = meta->property(i);
        if (!mp.isReadable()) continue;

        const QVariant val = mp.read(obj);
        switch (val.typeId()) {
        case QMetaType::Bool:
            props[QLatin1String(mp.name())] = val.toBool();
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
            props[QLatin1String(mp.name())] = val.toInt();
            break;
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            props[QLatin1String(mp.name())] = val.toLongLong();
            break;
        case QMetaType::Double:
            props[QLatin1String(mp.name())] = val.toDouble();
            break;
        case QMetaType::Float:
            props[QLatin1String(mp.name())] = static_cast<double>(val.toFloat());
            break;
        case QMetaType::QString:
            props[QLatin1String(mp.name())] = val.toString();
            break;
        case QMetaType::QUrl:
            props[QLatin1String(mp.name())] = val.toUrl().toString();
            break;
        case QMetaType::QColor: {
            const QColor c = val.value<QColor>();
            props[QLatin1String(mp.name())] = c.name(QColor::HexArgb);
            break;
        }
        default:
            break;
        }
    }
    return props;
}

// ---------------------------------------------------------------------------
// InspectorServer
// ---------------------------------------------------------------------------

InspectorServer::InspectorServer(QQmlApplicationEngine *engine, QObject *parent)
    : QObject(parent)
    , m_port(0)
    , m_server(new QTcpServer(this))
    , m_engine(engine)
{
}

bool InspectorServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "InspectorServer: failed to listen on 127.0.0.1:" << port
                   << m_server->errorString();
        return false;
    }
    m_port = m_server->serverPort();
    connect(m_server, &QTcpServer::newConnection,
            this,     &InspectorServer::onNewConnection);
    qDebug() << "InspectorServer: listening on 127.0.0.1:" << m_port;
    return true;
}

void InspectorServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = qobject_cast<QTcpSocket *>(m_server->nextPendingConnection());
        if (!sock) continue;
        m_buffers[sock] = {};
        connect(sock, &QTcpSocket::readyRead,
                this, &InspectorServer::onClientReadyRead);
        connect(sock, &QTcpSocket::disconnected,
                this, &InspectorServer::onClientDisconnected);
    }
}

void InspectorServer::onClientReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;

    m_buffers[sock] += sock->readAll();

    // Process all complete newline-delimited messages.
    while (true) {
        const int nl = m_buffers[sock].indexOf('\n');
        if (nl == -1) break;

        const QByteArray line = m_buffers[sock].left(nl).trimmed();
        m_buffers[sock] = m_buffers[sock].mid(nl + 1);
        if (line.isEmpty()) continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject e;
            e[QStringLiteral("error")] =
                QStringLiteral("JSON parse error: ") + err.errorString();
            sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + "\n");
            sock->flush();
            continue;
        }

        const QJsonObject req = doc.object();
        QJsonObject resp = handleRequest(req);
        if (req.contains(QLatin1String("id")))
            resp[QStringLiteral("id")] = req[QStringLiteral("id")];

        sock->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
        sock->flush();
    }
}

void InspectorServer::onClientDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (sock) {
        m_buffers.remove(sock);
        sock->deleteLater();
    }
}

QJsonObject InspectorServer::handleRequest(const QJsonObject &req)
{
    const QString method = req[QStringLiteral("method")].toString();
    const QJsonObject params = req[QStringLiteral("params")].toObject();

    const QHash<QString, std::function<QJsonObject(const QJsonObject &)>> dispatch = {
        {QStringLiteral("dump_tree"),     [this](const QJsonObject &p){ return cmdDumpTree(p); }},
        {QStringLiteral("screenshot"),    [this](const QJsonObject &p){ return cmdScreenshot(p); }},
        {QStringLiteral("mouse_click"),   [this](const QJsonObject &p){ return cmdMouseClick(p); }},
        {QStringLiteral("mouse_move"),    [this](const QJsonObject &p){ return cmdMouseMove(p); }},
        {QStringLiteral("mouse_scroll"),  [this](const QJsonObject &p){ return cmdMouseScroll(p); }},
        {QStringLiteral("mouse_drag"),    [this](const QJsonObject &p){ return cmdMouseDrag(p); }},
        {QStringLiteral("key_press"),     [this](const QJsonObject &p){ return cmdKeyPress(p); }},
        {QStringLiteral("type_text"),     [this](const QJsonObject &p){ return cmdTypeText(p); }},
        {QStringLiteral("find_item"),     [this](const QJsonObject &p){ return cmdFindItem(p); }},
        {QStringLiteral("get_properties"),[this](const QJsonObject &p){ return cmdGetProperties(p); }},
        {QStringLiteral("get_window_info"),[this](const QJsonObject &p){ return cmdGetWindowInfo(p); }},
        {QStringLiteral("focus_item"),    [this](const QJsonObject &p){ return cmdFocusItem(p); }},
        {QStringLiteral("set_property"),  [this](const QJsonObject &p){ return cmdSetProperty(p); }},
    };

    auto it = dispatch.find(method);
    if (it == dispatch.end()) {
        QJsonObject e;
        e[QStringLiteral("error")] = QStringLiteral("Unknown method: ") + method;
        return e;
    }

    try {
        QJsonObject result;
        result[QStringLiteral("result")] = it.value()(params);
        return result;
    } catch (const std::exception &ex) {
        QJsonObject e;
        e[QStringLiteral("error")] = QString::fromStdString(ex.what());
        return e;
    }
}

// ---------------------------------------------------------------------------
// Command: dump_tree
// ---------------------------------------------------------------------------

QJsonObject InspectorServer::serializeObject(QObject *obj, int depth, int maxDepth)
{
    QJsonObject node;
    if (!obj) return node;

    node[QStringLiteral("type")]       = QString::fromLatin1(obj->metaObject()->className());
    node[QStringLiteral("objectName")] = obj->objectName();
    node[QStringLiteral("ptr")]        = ptrStr(obj);
    node[QStringLiteral("props")]      = collectSimpleProperties(obj);

    auto *item = qobject_cast<QQuickItem *>(obj);
    auto *win  = qobject_cast<QQuickWindow *>(obj);

    if (item) {
        // parent-relative geometry
        QJsonObject geom;
        geom[QStringLiteral("x")]      = item->x();
        geom[QStringLiteral("y")]      = item->y();
        geom[QStringLiteral("width")]  = item->width();
        geom[QStringLiteral("height")] = item->height();
        node[QStringLiteral("geometry")] = geom;

        // scene (window-relative) position
        const QPointF sp = item->mapToScene(QPointF(0, 0));
        QJsonObject sgeom;
        sgeom[QStringLiteral("x")]      = sp.x();
        sgeom[QStringLiteral("y")]      = sp.y();
        sgeom[QStringLiteral("width")]  = item->width();
        sgeom[QStringLiteral("height")] = item->height();
        node[QStringLiteral("sceneGeometry")] = sgeom;

        node[QStringLiteral("visible")] = item->isVisible();
        node[QStringLiteral("enabled")] = item->isEnabled();
        node[QStringLiteral("z")]       = item->z();
        node[QStringLiteral("opacity")] = item->opacity();
    } else if (win) {
        QJsonObject geom;
        geom[QStringLiteral("x")]      = win->x();
        geom[QStringLiteral("y")]      = win->y();
        geom[QStringLiteral("width")]  = win->width();
        geom[QStringLiteral("height")] = win->height();
        node[QStringLiteral("geometry")] = geom;
        node[QStringLiteral("title")]    = win->title();
        node[QStringLiteral("dpr")]      = win->devicePixelRatio();
    }

    if (depth >= maxDepth) {
        node[QStringLiteral("truncated")] = true;
        return node;
    }

    // Build children array using the visual hierarchy for items.
    QJsonArray children;
    if (item) {
        for (QQuickItem *child : item->childItems())
            children.append(serializeObject(child, depth + 1, maxDepth));
    } else if (win) {
        // The content item is the root of the visual tree.
        if (auto *ci = win->contentItem())
            children.append(serializeObject(ci, depth + 1, maxDepth));
    } else {
        for (QObject *child : obj->children()) {
            if (qobject_cast<QQuickWindow *>(child))
                children.append(serializeObject(child, depth + 1, maxDepth));
        }
    }

    if (!children.isEmpty())
        node[QStringLiteral("children")] = children;

    return node;
}

QJsonObject InspectorServer::cmdDumpTree(const QJsonObject &params)
{
    const int maxDepth = params[QStringLiteral("maxDepth")].toInt(50);

    if (!m_engine) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Engine gone"); return e;
    }

    QJsonArray roots;
    for (QObject *obj : m_engine->rootObjects())
        roots.append(serializeObject(obj, 0, maxDepth));

    QJsonObject result;
    result[QStringLiteral("roots")] = roots;
    return result;
}

// ---------------------------------------------------------------------------
// Command: screenshot
// ---------------------------------------------------------------------------

QJsonObject InspectorServer::cmdScreenshot(const QJsonObject &)
{
    QQuickWindow *win = mainWindow();
    if (!win) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e;
    }

    const QImage img = win->grabWindow();
    if (img.isNull()) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("grabWindow returned null"); return e;
    }

    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");

    QJsonObject result;
    result[QStringLiteral("image")]  = QString::fromLatin1(bytes.toBase64());
    result[QStringLiteral("width")]  = img.width();
    result[QStringLiteral("height")] = img.height();
    result[QStringLiteral("dpr")]    = win->devicePixelRatio();
    return result;
}

// ---------------------------------------------------------------------------
// Mouse helpers
// ---------------------------------------------------------------------------

QQuickWindow *InspectorServer::mainWindow()
{
    if (!m_engine) return nullptr;
    for (QObject *obj : m_engine->rootObjects()) {
        if (auto *w = qobject_cast<QQuickWindow *>(obj))
            return w;
    }
    return nullptr;
}

static quint64 nextTimestamp()
{
    // Monotonically increasing timestamp (ms) so QQuickDeliveryAgent can
    // properly track event sequences (double-click detection, etc.).
    static quint64 ts = 1;
    return ts++;
}

static void sendMouseEvent(QQuickWindow *win, QEvent::Type type,
                           qreal x, qreal y, Qt::MouseButton btn,
                           Qt::MouseButtons btns, Qt::KeyboardModifiers mods)
{
    const QPointF local(x, y);
    const QPointF global = win->mapToGlobal(local.toPoint());
    QMouseEvent ev(type, local, local, global, btn, btns, mods,
                   QPointingDevice::primaryPointingDevice());
    ev.setTimestamp(nextTimestamp());
    QCoreApplication::sendEvent(win, &ev);
}

QJsonObject InspectorServer::cmdMouseMove(const QJsonObject &params)
{
    QQuickWindow *win = mainWindow();
    if (!win) { QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e; }

    const qreal x = params[QStringLiteral("x")].toDouble();
    const qreal y = params[QStringLiteral("y")].toDouble();

    const QPointF local(x, y);
    const QPointF global = win->mapToGlobal(local.toPoint());

    // Send a MouseMove – QQuickDeliveryAgent internally generates hover
    // events for items with acceptHoverEvents() == true.
    sendMouseEvent(win, QEvent::MouseMove, x, y,
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    // Explicit HoverMove as fallback so QQuickHoverHandler / HoverHandler
    // sees the event even when a synthetic MouseMove alone is not enough.
    {
        const QPointF oldLocal(x - 1.0, y - 1.0);
        QHoverEvent hev(QEvent::HoverMove, local, global, oldLocal,
                        Qt::NoModifier, QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(win, &hev);
    }

    return QJsonObject{{QStringLiteral("ok"), true}};
}

QJsonObject InspectorServer::cmdMouseClick(const QJsonObject &params)
{
    QQuickWindow *win = mainWindow();
    if (!win) { QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e; }

    const qreal x   = params[QStringLiteral("x")].toDouble();
    const qreal y   = params[QStringLiteral("y")].toDouble();
    const int clicks = params[QStringLiteral("clicks")].toInt(1);  // 1=single, 2=double

    const QString btnStr = params[QStringLiteral("button")].toString(QStringLiteral("left"));
    Qt::MouseButton btn = Qt::LeftButton;
    if (btnStr == QLatin1String("right"))  btn = Qt::RightButton;
    if (btnStr == QLatin1String("middle")) btn = Qt::MiddleButton;

    const Qt::KeyboardModifiers mods =
        parseModifiers(params[QStringLiteral("modifiers")].toArray());

    // Move to position first (triggers hover delivery).
    sendMouseEvent(win, QEvent::MouseMove, x, y, Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    // pressDuration: ms to hold the button down so the pressed state is visible.
    const int holdMs = params[QStringLiteral("pressDuration")].toInt(80);

    if (clicks == 2) {
        // Double-click: two rapid press+release cycles so Qt Quick's internal
        // double-click detection fires (QQuickDeliveryAgent tracks timing
        // between consecutive presses rather than relying on DblClick type).
        sendMouseEvent(win, QEvent::MouseButtonPress,   x, y, btn, btn, mods);
        QCoreApplication::processEvents();
        sendMouseEvent(win, QEvent::MouseButtonRelease, x, y, btn, Qt::NoButton, mods);
        QCoreApplication::processEvents();
        // Second click — must arrive within QStyleHints::mouseDoubleClickInterval().
        sendMouseEvent(win, QEvent::MouseButtonPress,   x, y, btn, btn, mods);
        QCoreApplication::processEvents();
        if (holdMs > 0) QThread::msleep(holdMs);
        sendMouseEvent(win, QEvent::MouseButtonRelease, x, y, btn, Qt::NoButton, mods);
    } else {
        sendMouseEvent(win, QEvent::MouseButtonPress,   x, y, btn, btn, mods);
        QCoreApplication::processEvents();
        if (holdMs > 0) QThread::msleep(holdMs);
        sendMouseEvent(win, QEvent::MouseButtonRelease, x, y, btn, Qt::NoButton, mods);
    }

    return QJsonObject{{QStringLiteral("ok"), true}};
}

QJsonObject InspectorServer::cmdMouseScroll(const QJsonObject &params)
{
    QQuickWindow *win = mainWindow();
    if (!win) { QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e; }

    const qreal x     = params[QStringLiteral("x")].toDouble();
    const qreal y     = params[QStringLiteral("y")].toDouble();
    const int   dx    = params[QStringLiteral("dx")].toInt(0);
    const int   dy    = params[QStringLiteral("dy")].toInt(120); // one notch
    const Qt::KeyboardModifiers mods =
        parseModifiers(params[QStringLiteral("modifiers")].toArray());

    const QPointF local(x, y);
    const QPointF global = win->mapToGlobal(local.toPoint());
    QWheelEvent ev(local, global,
                   QPoint(dx, 0),               // pixelDelta
                   QPoint(0, dy),               // angleDelta
                   Qt::NoButton, mods,
                   Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(win, &ev);

    return QJsonObject{{QStringLiteral("ok"), true}};
}

QJsonObject InspectorServer::cmdMouseDrag(const QJsonObject &params)
{
    QQuickWindow *win = mainWindow();
    if (!win) { QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e; }

    const qreal x1 = params[QStringLiteral("startX")].toDouble();
    const qreal y1 = params[QStringLiteral("startY")].toDouble();
    const qreal x2 = params[QStringLiteral("endX")].toDouble();
    const qreal y2 = params[QStringLiteral("endY")].toDouble();
    const int steps = qMax(2, params[QStringLiteral("steps")].toInt(10));
    const int stepMs = qMax(1, params[QStringLiteral("stepMs")].toInt(16));

    const Qt::MouseButton btn = Qt::LeftButton;
    const Qt::KeyboardModifiers mods =
        parseModifiers(params[QStringLiteral("modifiers")].toArray());

    // Move to start, press
    sendMouseEvent(win, QEvent::MouseMove, x1, y1, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    sendMouseEvent(win, QEvent::MouseButtonPress, x1, y1, btn, btn, mods);
    QCoreApplication::processEvents();

    // Interpolate movement
    for (int i = 1; i <= steps; ++i) {
        const qreal t = static_cast<qreal>(i) / steps;
        const qreal cx = x1 + (x2 - x1) * t;
        const qreal cy = y1 + (y2 - y1) * t;
        sendMouseEvent(win, QEvent::MouseMove, cx, cy, Qt::NoButton, btn, mods);
        QCoreApplication::processEvents();
        QThread::msleep(stepMs);
    }

    // Release at end
    sendMouseEvent(win, QEvent::MouseButtonRelease, x2, y2, btn, Qt::NoButton, mods);

    return QJsonObject{{QStringLiteral("ok"), true}};
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

static void sendKey(QQuickWindow *win, Qt::Key key,
                    Qt::KeyboardModifiers mods, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QCoreApplication::sendEvent(win, &press);
    QCoreApplication::sendEvent(win, &release);
}

QJsonObject InspectorServer::cmdKeyPress(const QJsonObject &params)
{
    QQuickWindow *win = mainWindow();
    if (!win) { QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e; }

    const QString keyName = params[QStringLiteral("key")].toString();
    if (keyName.isEmpty()) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("key param required"); return e;
    }

    const KeyInfo ki   = parseKeyName(keyName);
    const Qt::KeyboardModifiers mods =
        parseModifiers(params[QStringLiteral("modifiers")].toArray());

    sendKey(win, ki.key, mods, ki.text);
    return QJsonObject{{QStringLiteral("ok"), true}};
}

QJsonObject InspectorServer::cmdTypeText(const QJsonObject &params)
{
    QQuickWindow *win = mainWindow();
    if (!win) { QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e; }

    const QString text = params[QStringLiteral("text")].toString();
    for (const QChar &ch : text) {
        const Qt::Key key = Qt::Key(ch.toUpper().unicode());
        Qt::KeyboardModifiers mods = Qt::NoModifier;
        if (ch.isUpper()) mods |= Qt::ShiftModifier;
        sendKey(win, key, mods, ch);
    }
    return QJsonObject{{QStringLiteral("ok"), true}};
}

// ---------------------------------------------------------------------------
// Command: find_item
// ---------------------------------------------------------------------------

void InspectorServer::collectObjects(QObject *obj, QList<QObject *> &out,
                                     int depth, int maxDepth)
{
    if (!obj || depth > maxDepth) return;
    out.append(obj);

    auto *item = qobject_cast<QQuickItem *>(obj);
    auto *win  = qobject_cast<QQuickWindow *>(obj);

    if (item) {
        for (QQuickItem *c : item->childItems())
            collectObjects(c, out, depth + 1, maxDepth);
    } else if (win) {
        if (auto *ci = win->contentItem())
            collectObjects(ci, out, depth + 1, maxDepth);
    }
}

QJsonObject InspectorServer::cmdFindItem(const QJsonObject &params)
{
    if (!m_engine) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Engine gone"); return e;
    }

    const QString byName = params[QStringLiteral("objectName")].toString();
    const QString byType = params[QStringLiteral("type")].toString();
    const QString byProp = params[QStringLiteral("property")].toString();
    const QVariant byVal = params[QStringLiteral("value")].toVariant();

    QList<QObject *> all;
    for (QObject *root : m_engine->rootObjects())
        collectObjects(root, all, 0, 200);

    QJsonArray matches;
    for (QObject *obj : all) {
        bool ok = true;
        if (!byName.isEmpty() && obj->objectName() != byName)        ok = false;
        if (ok && !byType.isEmpty()) {
            const QString cls = QString::fromLatin1(obj->metaObject()->className());
            if (!cls.contains(byType, Qt::CaseInsensitive)) ok = false;
        }
        if (ok && !byProp.isEmpty()) {
            const QVariant v = obj->property(byProp.toLatin1());
            if (!v.isValid() || v != byVal) ok = false;
        }
        if (ok)
            matches.append(serializeObject(obj, 0, 0)); // depth 0: no children
    }

    QJsonObject result;
    result[QStringLiteral("matches")] = matches;
    result[QStringLiteral("count")]   = matches.size();
    return result;
}

// ---------------------------------------------------------------------------
// Command: get_properties
// ---------------------------------------------------------------------------

QObject *InspectorServer::objectByPtr(const QString &ptrStr)
{
    if (!m_engine) return nullptr;
    bool ok = false;
    const quintptr addr = ptrStr.toULongLong(&ok, 16);
    if (!ok) return nullptr;

    QList<QObject *> all;
    for (QObject *root : m_engine->rootObjects())
        collectObjects(root, all, 0, 200);

    for (QObject *obj : all) {
        if (reinterpret_cast<quintptr>(obj) == addr)
            return obj;
    }
    return nullptr;
}

QJsonObject InspectorServer::cmdGetProperties(const QJsonObject &params)
{
    const QString ptr = params[QStringLiteral("ptr")].toString();
    if (ptr.isEmpty()) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("ptr required"); return e;
    }
    QObject *obj = objectByPtr(ptr);
    if (!obj) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Object not found"); return e;
    }
    return serializeObject(obj, 0, 0); // flat – no children
}

// ---------------------------------------------------------------------------
// Command: get_window_info
// ---------------------------------------------------------------------------

QJsonObject InspectorServer::cmdGetWindowInfo(const QJsonObject &)
{
    QQuickWindow *win = mainWindow();
    if (!win) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("No window"); return e;
    }

    QJsonObject result;
    result[QStringLiteral("title")]  = win->title();
    result[QStringLiteral("x")]      = win->x();
    result[QStringLiteral("y")]      = win->y();
    result[QStringLiteral("width")]  = win->width();
    result[QStringLiteral("height")] = win->height();
    result[QStringLiteral("dpr")]    = win->devicePixelRatio();
    result[QStringLiteral("ptr")]    = ptrStr(win);

    if (QScreen *scr = win->screen()) {
        QJsonObject screen;
        screen[QStringLiteral("name")]             = scr->name();
        screen[QStringLiteral("physicalDpi")]       = scr->physicalDotsPerInch();
        screen[QStringLiteral("logicalDpi")]        = scr->logicalDotsPerInch();
        screen[QStringLiteral("devicePixelRatio")]  = scr->devicePixelRatio();
        screen[QStringLiteral("availableGeometry")] =
            QJsonObject{{QStringLiteral("x"),      scr->availableGeometry().x()},
                        {QStringLiteral("y"),      scr->availableGeometry().y()},
                        {QStringLiteral("width"),  scr->availableGeometry().width()},
                        {QStringLiteral("height"), scr->availableGeometry().height()}};
        result[QStringLiteral("screen")] = screen;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Command: focus_item
// ---------------------------------------------------------------------------

QJsonObject InspectorServer::cmdFocusItem(const QJsonObject &params)
{
    const QString ptr = params[QStringLiteral("ptr")].toString();
    if (ptr.isEmpty()) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("ptr required"); return e;
    }

    QObject *obj = objectByPtr(ptr);
    if (!obj) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Object not found"); return e;
    }

    auto *item = qobject_cast<QQuickItem *>(obj);
    if (!item) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Object is not a QQuickItem"); return e;
    }

    const QString reason = params[QStringLiteral("reason")].toString(QStringLiteral("OtherFocusReason"));
    Qt::FocusReason fr = Qt::OtherFocusReason;
    if (reason == QLatin1String("MouseFocusReason"))    fr = Qt::MouseFocusReason;
    else if (reason == QLatin1String("TabFocusReason")) fr = Qt::TabFocusReason;

    item->forceActiveFocus(fr);

    QJsonObject result;
    result[QStringLiteral("ok")]          = true;
    result[QStringLiteral("activeFocus")] = item->hasActiveFocus();
    return result;
}

// ---------------------------------------------------------------------------
// Command: set_property
// ---------------------------------------------------------------------------

QJsonObject InspectorServer::cmdSetProperty(const QJsonObject &params)
{
    const QString ptr = params[QStringLiteral("ptr")].toString();
    if (ptr.isEmpty()) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("ptr required"); return e;
    }

    const QString propName = params[QStringLiteral("property")].toString();
    if (propName.isEmpty()) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("property name required"); return e;
    }

    QObject *obj = objectByPtr(ptr);
    if (!obj) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Object not found"); return e;
    }

    const QJsonValue jsonVal = params[QStringLiteral("value")];
    QVariant val;
    if (jsonVal.isBool())        val = jsonVal.toBool();
    else if (jsonVal.isDouble()) val = jsonVal.toDouble();
    else if (jsonVal.isString()) val = jsonVal.toString();
    else {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("value must be bool, number, or string"); return e;
    }

    if (!obj->setProperty(propName.toLatin1(), val)) {
        QJsonObject e; e[QStringLiteral("error")] = QStringLiteral("Failed to set property (does it exist and is it writable?)"); return e;
    }

    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    return result;
}
