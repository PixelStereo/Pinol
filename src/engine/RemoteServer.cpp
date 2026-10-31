#include "RemoteServer.h"
#include "Engine.h"
#include "RemotePage.h"
#include "WebSocketFrame.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <cmath>

namespace {
constexpr int kMaxClients = 16;
constexpr int kMaxHeader = 16384;
constexpr quint64 kMaxFrame = 4096;
constexpr int kMaxFailures = 8;
constexpr qint64 kLockMs = 60'000;

QByteArray response(int status, const QByteArray &body, const QByteArray &type, const QByteArray &extra = {})
{
    static const QHash<int, QByteArray> text = {{200, "OK"}, {400, "Bad Request"}, {403, "Forbidden"}, {404, "Not Found"}, {429, "Too Many Requests"}};
    QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + ' ' + text.value(status, "Error") + "\r\n";
    r += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n";
    r += extra + "Connection: close\r\n\r\n";
    return r + body;
}

QByteArray hash(const char *text)
{
    return "'sha256-" + QCryptographicHash::hash(QByteArray(text), QCryptographicHash::Sha256).toBase64() + "'";
}

// Same time whatever the digits: a wrong PIN says nothing of how close it was
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    int diff = a.size() ^ b.size();
    for (int i = 0; i < std::max(a.size(), b.size()); ++i) diff |= (i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0);
    return diff == 0;
}

QList<double> valuesOf(const ShowControl &c)
{
    if (c.kind == ShowControl::Kind::Color) return {c.rgb[0], c.rgb[1], c.rgb[2]};
    return {c.value};
}
QJsonValue jsonOf(const QList<double> &v) { return v.size() == 3 ? QJsonValue(QJsonArray{v[0], v[1], v[2]}) : QJsonValue(v.value(0)); }
} // namespace

QByteArray RemoteServer::page()
{
    return QByteArray("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
                      "<title>NyxMap remote</title><style>") +
           remote_page::kStyle + "</style></head><body>" + remote_page::kBody + "<script>" + remote_page::kScript + "</script></body></html>";
}

RemoteServer::RemoteServer(Engine *engine, QObject *parent) : QObject(parent), m_e(engine)
{
    m_csp = "default-src 'none'; script-src " + hash(remote_page::kScript) + "; style-src " + hash(remote_page::kStyle) +
            "; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &RemoteServer::push);
}

RemoteServer::~RemoteServer() { stop(); }

QString RemoteServer::makePin(int digits)
{
    QString pin;
    for (int i = 0; i < std::clamp(digits, 5, 12); ++i) pin += QChar('0' + QRandomGenerator::system()->bounded(10));
    return pin;
}

bool RemoteServer::pinValid(const QString &pin)
{
    if (pin.size() < 4 || pin.size() > 32) return false;
    for (const QChar c : pin)
        if (c.unicode() > 127 || !c.isLetterOrNumber()) return false; // letters and digits: it travels in an address
    return true;
}

bool RemoteServer::start(quint16 port, const QString &pin, const QString &listen)
{
    stop();
    if (!pin.isEmpty() && !pinValid(pin)) {
        m_status = QStringLiteral("Phone remote: the code has to be 4 to 32 letters or digits");
        return false;
    }
    m_pin = pin;
    m_server = new QTcpServer(this);
    const QHostAddress address = listen.isEmpty() ? QHostAddress(QHostAddress::Any) : QHostAddress(listen);
    if (!m_server->listen(address, port)) {
        m_status = QStringLiteral("Phone remote: port %1 is not available (%2)").arg(port).arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &RemoteServer::onConnection);
    m_timer.start();
    const QStringList list = urls();
    m_status = QStringLiteral("Phone remote on port %1").arg(m_server->serverPort()) + (m_pin.isEmpty() ? QStringLiteral(" (no code)") : QString()) + (list.isEmpty() ? QString() : QStringLiteral(": ") + list.join(QStringLiteral("  ")));
    return true;
}

void RemoteServer::stop()
{
    m_timer.stop();
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->socket) {
            it->socket->disconnect(this);
            it->socket->abort();
            it->socket->deleteLater();
        }
    m_clients.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_attempts.clear();
    emit clientsChanged(0);
}

bool RemoteServer::isRunning() const { return m_server && m_server->isListening(); }
quint16 RemoteServer::port() const { return m_server ? m_server->serverPort() : 0; }

int RemoteServer::clientCount() const
{
    int n = 0;
    for (const Client &c : m_clients) n += c.websocket;
    return n;
}

QStringList RemoteServer::urls() const
{
    QStringList out;
    if (!m_server) return out;
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        if (!(nif.flags() & QNetworkInterface::IsUp) || (nif.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                out << QStringLiteral("http://%1:%2/").arg(e.ip().toString()).arg(m_server->serverPort());
    }
    return out;
}

bool RemoteServer::authorized(const QByteArray &presented) const { return m_pin.isEmpty() || sameBytes(presented, m_pin.toUtf8()); }

void RemoteServer::onConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        if (m_clients.size() >= kMaxClients * 2) { // a flood: no more room
            s->abort();
            s->deleteLater();
            continue;
        }
        Client c;
        c.socket = s;
        m_clients.insert(s, c);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            const bool was = m_clients.value(s).websocket;
            m_clients.remove(s);
            s->deleteLater();
            if (was) emit clientsChanged(clientCount());
        });
    }
}

void RemoteServer::send(QTcpSocket *s, const QByteArray &frame) const
{
    if (s->bytesToWrite() > (1 << 20)) { // a client that does not read: dropped, not buffered without end
        s->abort();
        return;
    }
    s->write(frame);
}

QByteArray RemoteServer::controlsMessage() const
{
    QJsonArray list;
    for (const ShowControl &c : m_e->controls()) {
        QJsonObject o{{"id", QString::number(c.id)}, {"name", c.name}, {"key", c.key}, {"kind", ShowControl::kindKey(c.kind)}};
        switch (c.kind) {
        case ShowControl::Kind::Slider:
            o["min"] = c.min;
            o["max"] = c.max;
            o["step"] = c.step;
            o["unit"] = c.unit;
            o["value"] = c.value;
            break;
        case ShowControl::Kind::Choice: o["choices"] = QJsonArray::fromStringList(c.choices); [[fallthrough]];
        case ShowControl::Kind::Toggle:
        case ShowControl::Kind::Button: o["value"] = c.value; break;
        case ShowControl::Kind::Color: o["rgb"] = QJsonArray{c.rgb[0], c.rgb[1], c.rgb[2]}; break;
        }
        list.append(o);
    }
    const QString title = QFileInfo(m_e->projectPath()).completeBaseName();
    return QJsonDocument(QJsonObject{{"t", "controls"}, {"title", title.isEmpty() ? QStringLiteral("NyxMap") : title}, {"list", list}}).toJson(QJsonDocument::Compact);
}

void RemoteServer::onData(QTcpSocket *s)
{
    auto it = m_clients.find(s);
    if (it == m_clients.end()) return;
    Client &c = *it;
    c.buffer += s->readAll();
    if (!c.websocket) {
        const int end = c.buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (c.buffer.size() > kMaxHeader) s->abort();
            return;
        }
        const QList<QByteArray> lines = c.buffer.left(end).split('\n');
        c.buffer.remove(0, end + 4);
        const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
        QHash<QByteArray, QByteArray> headers;
        for (int k = 1; k < lines.size(); ++k) {
            const int colon = lines[k].indexOf(':');
            if (colon > 0) headers.insert(lines[k].left(colon).trimmed().toLower(), lines[k].mid(colon + 1).trimmed());
        }
        if (request.value(0) != "GET" || request.size() < 2) {
            send(s, response(400, {}, "text/plain"));
            s->disconnectFromHost();
            return;
        }
        const QUrl url = QUrl::fromEncoded(request.value(1));
        const QString path = url.path();
        if (headers.value("upgrade").toLower() == "websocket") {
            const QString who = s->peerAddress().toString();
            Attempts &a = m_attempts[who];
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (a.until > now) {
                send(s, response(429, {}, "text/plain", "Retry-After: 60\r\n"));
                s->disconnectFromHost();
                return;
            }
            // Only the page this server served opens it: a page of another site must not reach the show from a phone's browser
            const QByteArray origin = headers.value("origin");
            const bool sameOrigin = origin.isEmpty() || QUrl(QString::fromUtf8(origin)).authority() == QString::fromUtf8(headers.value("host"));
            const QByteArray pin = QUrl::fromPercentEncoding(QUrlQuery(url).queryItemValue(QStringLiteral("pin"), QUrl::FullyEncoded).toUtf8()).toUtf8();
            int clients = 0;
            for (const Client &other : std::as_const(m_clients)) clients += other.websocket;
            if (path != QLatin1String("/ws") || !sameOrigin || !authorized(pin)) {
                if (path == QLatin1String("/ws") && sameOrigin && ++a.failures >= kMaxFailures) {
                    a.failures = 0;
                    a.until = now + kLockMs;
                }
                send(s, response(403, {}, "text/plain"));
                s->disconnectFromHost();
                return;
            }
            if (clients >= kMaxClients) {
                send(s, response(429, {}, "text/plain"));
                s->disconnectFromHost();
                return;
            }
            a.failures = 0;
            const QByteArray accept = QCryptographicHash::hash(headers.value("sec-websocket-key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                               QCryptographicHash::Sha1).toBase64();
            s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
            c.websocket = true;
            emit clientsChanged(clientCount());
            push(); // its first picture of the controls
        } else if (path == QLatin1String("/info")) { // whether it asks a code: nothing else
            send(s, response(200, m_pin.isEmpty() ? "{\"pin\":false}" : "{\"pin\":true}", "application/json"));
            s->disconnectFromHost();
            return;
        } else if (path == QLatin1String("/") || path == QLatin1String("/index.html")) {
            send(s, response(200, page(), "text/html; charset=utf-8", "Content-Security-Policy: " + m_csp + "\r\n"));
            s->disconnectFromHost();
            return;
        } else {
            send(s, response(404, {}, "text/plain"));
            s->disconnectFromHost();
            return;
        }
    }
    while (c.websocket) {
        int opcode = 0;
        QByteArray payload;
        const int got = websocket::decode(c.buffer, &opcode, &payload, kMaxFrame);
        if (got == 0) return;
        if (got < 0) {
            s->abort();
            return;
        }
        onFrame(c, opcode, payload);
        if (!m_clients.contains(s)) return;
    }
}

void RemoteServer::onFrame(Client &c, int opcode, const QByteArray &payload)
{
    switch (opcode) {
    case websocket::Text: {
        if (--c.budget < 0) return; // faster than any hand: dropped
        const QJsonObject o = QJsonDocument::fromJson(payload).object();
        const quint64 id = o.value("id").toString().toULongLong();
        if (!id) return;
        const QString type = o.value("t").toString();
        if (type == QLatin1String("set") && o.value("v").isDouble()) {
            if (m_e->setControlValue(id, o.value("v").toDouble())) emit controlMoved();
        } else if (type == QLatin1String("color")) {
            const QJsonArray rgb = o.value("rgb").toArray();
            if (rgb.size() == 3 && m_e->setControlColor(id, rgb[0].toDouble(), rgb[1].toDouble(), rgb[2].toDouble())) emit controlMoved();
        }
        break;
    }
    case websocket::Close:
        send(c.socket, websocket::encode(websocket::Close, {}));
        c.socket->disconnectFromHost();
        break;
    case websocket::Ping: send(c.socket, websocket::encode(websocket::Pong, payload.left(125))); break;
    default: break;
    }
}

// Every 50 ms: the list to a client that has not got its latest form, then the values that changed
void RemoteServer::push()
{
    bool any = false;
    for (const Client &c : std::as_const(m_clients)) any |= c.websocket;
    if (!any) return;
    const std::vector<ShowControl> controls = m_e->controls();
    const quint64 revision = m_e->controlsRevision();
    QByteArray list;
    for (Client &c : m_clients) {
        if (!c.websocket) continue;
        c.budget = std::min(300.0, c.budget + 15); // 300 a second
        if (c.controlsSeen != revision) {
            if (list.isEmpty()) list = websocket::encode(websocket::Text, controlsMessage());
            send(c.socket, list);
            c.controlsSeen = revision;
            c.sent.clear();
            for (const ShowControl &control : controls) c.sent.insert(control.id, valuesOf(control));
            continue;
        }
        QJsonObject changed;
        for (const ShowControl &control : controls) {
            const QList<double> now = valuesOf(control);
            auto found = c.sent.find(control.id);
            if (found != c.sent.end() && found.value() == now) continue;
            c.sent.insert(control.id, now);
            changed[QString::number(control.id)] = jsonOf(now);
        }
        if (!changed.isEmpty())
            send(c.socket, websocket::encode(websocket::Text, QJsonDocument(QJsonObject{{"t", "values"}, {"v", changed}}).toJson(QJsonDocument::Compact)));
    }
}
