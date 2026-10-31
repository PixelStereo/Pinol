#pragma once
// The phone remote: a small web server that shows the show's controls (ShowControl.h) as a page for a phone or a tablet —
// sliders, switches, selectors, hold buttons, color pickers — and moves them live, both ways. One page, no installation: open
// http://<this machine>:<port>/ and enter the PIN.
//
// What it is, and is not: it reaches the *controls* and nothing else (not the layers, not the project, not OSC). It asks a
// code (the "PIN": 4 to 32 letters or digits, chosen by the operator, or none at all), compared in constant time; a machine that
// gets it wrong eight times is refused for a minute. Without a code anyone on the network may move the controls.
// A WebSocket is accepted only from the page this server served (same origin). The page's script and style are the only
// ones its Content-Security-Policy allows. There is no encryption: the PIN and the values cross the network in clear, as OSC
// does: use it on the show's network, not on a shared one.
//
// Protocol (text frames, JSON), for those who want their own client:
//   GET /info          {"pin":true|false}: whether a code is asked
//   GET /ws?pin=<PIN>  upgrade to a WebSocket (no pin= when none is asked)
//   server -> {"t":"controls","title":..,"list":[{id,name,key,kind,min,max,step,unit,choices,value,rgb}]}   at connection, and
//             when a control is added, removed or shaped
//   server -> {"t":"values","v":{"<id>":<number>|[r,g,b]}}                                                  what changed
//   client -> {"t":"set","id":"<id>","v":<number>}      client -> {"t":"color","id":"<id>","rgb":[r,g,b]}

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <vector>

class Engine;
class QTcpServer;
class QTcpSocket;

class RemoteServer : public QObject
{
    Q_OBJECT
public:
    explicit RemoteServer(Engine *engine, QObject *parent = nullptr);
    ~RemoteServer() override;

    // Port 0: chosen by the system (tests). `pin` empty: no code asked. `listen`: the interface's address ("": all). False with
    // status() when it cannot
    bool start(quint16 port, const QString &pin, const QString &listen = QString());
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString status() const { return m_status; }
    int clientCount() const;
    // Where to point the phone: one address per network interface
    QStringList urls() const;
    // A random PIN of `digits` digits
    static QString makePin(int digits = 6);
    static bool pinValid(const QString &pin); // 4 to 32 letters or digits (an empty one is "no code": start() takes it)

    // The page, as served (tests)
    static QByteArray page();

signals:
    void clientsChanged(int count);
    void controlMoved(); // a control was set from a client: the interface follows

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        bool websocket = false;
        quint64 controlsSeen = ~0ull;
        QHash<quint64, QList<double>> sent; // the values the client has
        double budget = 300;                  // messages it may still send this second
    };
    void onConnection();
    void onData(QTcpSocket *s);
    void onFrame(Client &c, int opcode, const QByteArray &payload);
    void push();
    void send(QTcpSocket *s, const QByteArray &frame) const;
    QByteArray controlsMessage() const;
    bool authorized(const QByteArray &presented) const;

    Engine *m_e;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Client> m_clients;
    QTimer m_timer;
    QString m_pin, m_status;
    QByteArray m_csp;
    struct Attempts {
        int failures = 0;
        qint64 until = 0; // ms since epoch
    };
    QHash<QString, Attempts> m_attempts; // by address
};
