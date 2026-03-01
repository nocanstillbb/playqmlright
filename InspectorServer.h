#pragma once

#include <QObject>
#include <QTcpServer>
#include <QJsonObject>
#include <QPointer>
#include <QMap>
#include <QByteArray>

class QQmlApplicationEngine;
class QTcpSocket;
class QQuickWindow;

// InspectorServer exposes a local TCP JSON-RPC interface so that an
// external process (e.g. the Python MCP server) can inspect and control the
// running Qt Quick application at runtime.
//
// Protocol: newline-delimited JSON over TCP (127.0.0.1, configurable port)
//   Request:  {"id":1,"method":"dump_tree","params":{}}\n
//   Response: {"id":1,"result":{...}}\n   or   {"id":1,"error":"..."}\n
//
// Available methods:
//   dump_tree        – full QML visual object tree
//   screenshot       – PNG screenshot as base64
//   mouse_click      – inject a mouse click
//   mouse_move       – move the cursor
//   mouse_scroll     – wheel scroll
//   key_press        – inject a key press (+release)
//   type_text        – type a string character by character
//   find_item        – search items by objectName / type / property
//   get_properties   – all serialisable properties of one item (by ptr)
//   get_window_info  – geometry and DPR of the main window

class InspectorServer : public QObject
{
    Q_OBJECT
public:
    explicit InspectorServer(QQmlApplicationEngine *engine, QObject *parent = nullptr);

    // Start listening on 127.0.0.1:port.  Returns true on success.
    bool start(quint16 port = 37521);

    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    // Dispatch a parsed request to the appropriate handler.
    QJsonObject handleRequest(const QJsonObject &request);

    // Individual command handlers – all return a value for the "result" key.
    QJsonObject cmdDumpTree(const QJsonObject &params);
    QJsonObject cmdScreenshot(const QJsonObject &params);
    QJsonObject cmdMouseClick(const QJsonObject &params);
    QJsonObject cmdMouseMove(const QJsonObject &params);
    QJsonObject cmdMouseScroll(const QJsonObject &params);
    QJsonObject cmdKeyPress(const QJsonObject &params);
    QJsonObject cmdTypeText(const QJsonObject &params);
    QJsonObject cmdFindItem(const QJsonObject &params);
    QJsonObject cmdGetProperties(const QJsonObject &params);
    QJsonObject cmdGetWindowInfo(const QJsonObject &params);

    // Serialise a QObject (and its visual subtree) to JSON.
    QJsonObject serializeObject(QObject *obj, int depth, int maxDepth);

    // Collect all reachable objects into a flat list (for searches).
    void collectObjects(QObject *obj, QList<QObject *> &out, int depth, int maxDepth);

    // Find a QObject by its hex pointer string ("0x…").
    QObject *objectByPtr(const QString &ptrStr);

    // Return the first visible QQuickWindow owned by the engine.
    QQuickWindow *mainWindow();

    quint16 m_port;
    QTcpServer *m_server;
    QPointer<QQmlApplicationEngine> m_engine;

    // Per-client read buffers (accumulate data until a newline arrives).
    QMap<QTcpSocket *, QByteArray> m_buffers;
};
