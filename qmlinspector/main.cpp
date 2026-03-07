#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "InspectorServer.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const bool testMode = app.arguments().contains(QStringLiteral("--test"));

    QQmlApplicationEngine engine;
    const QUrl url = testMode
        ? QUrl(QStringLiteral("qrc:/qml_inspector/example/TestBench.qml"))
        : QUrl(QStringLiteral("qrc:/qml_inspector/main.qml"));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    // Start the inspector server so external tools (e.g. the MCP server) can
    // inspect and drive the UI via TCP on 127.0.0.1.
    // Port can be overridden via the QML_INSPECTOR_PORT environment variable.
    const quint16 inspectorPort = []() -> quint16 {
        const QByteArray env = qgetenv("QML_INSPECTOR_PORT");
        if (!env.isEmpty()) {
            bool ok = false;
            const int p = env.toInt(&ok);
            if (ok && p > 0 && p < 65536) return static_cast<quint16>(p);
        }
        return 37521;
    }();
    InspectorServer inspector(&engine);
    if (!inspector.start(inspectorPort)) {
        qWarning("InspectorServer failed to start – continuing without it.");
    }

    return app.exec();
}
