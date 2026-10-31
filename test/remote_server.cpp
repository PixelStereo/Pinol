// The phone remote: the page and its policy, the PIN, the lockout, the origin, and the controls moving both ways.
#include "Engine.h"
#include "RemotePage.h"
#include "RemoteServer.h"
#include "WebSocketFrame.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTcpSocket>
#include <QThread>
#include <cstdio>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

// A plain client: one connection, the replies as they come
struct Client {
    QTcpSocket socket;
    QByteArray in;
    bool connectTo(quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        return socket.waitForConnected(2000);
    }
    void pump(int ms = 400)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            socket.waitForReadyRead(5);
            in += socket.readAll();
        }
    }
    QByteArray request(const QByteArray &text)
    {
        socket.write(text);
        socket.flush();
        in.clear();
        pump(800);
        return in;
    }
    QByteArray upgrade(const QByteArray &path, const QByteArray &origin = {}, const QByteArray &host = "127.0.0.1")
    {
        QByteArray r = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
        if (!origin.isEmpty()) r += "Origin: " + origin + "\r\n";
        return request(r + "\r\n");
    }
    // A masked text frame, as a browser sends it
    void sendText(const QByteArray &payload)
    {
        const char mask[4] = {0x12, 0x34, 0x56, 0x78};
        QByteArray f;
        f.append(char(0x81));
        f.append(char(0x80 | payload.size()));
        f.append(mask, 4);
        for (int i = 0; i < payload.size(); ++i) f.append(char(payload[i] ^ mask[i & 3]));
        socket.write(f);
        socket.flush();
    }
    // The server's text frames received so far, as JSON
    QList<QJsonObject> messages()
    {
        QList<QJsonObject> out;
        QByteArray rest = in;
        const int header = rest.indexOf("\r\n\r\n");
        if (rest.startsWith("HTTP/1.1 101") && header >= 0) rest.remove(0, header + 4);
        while (rest.size() >= 2) {
            quint64 len = quint8(rest[1]) & 0x7F;
            int pos = 2;
            if (len == 126) { len = qFromBigEndian<quint16>(rest.constData() + 2); pos = 4; }
            else if (len == 127) { len = qFromBigEndian<quint64>(rest.constData() + 2); pos = 10; }
            if (quint64(rest.size()) < quint64(pos) + len) break;
            out << QJsonDocument::fromJson(rest.mid(pos, int(len))).object();
            rest.remove(0, pos + int(len));
        }
        return out;
    }
};

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    try {
        Engine engine;
        QString error;
        require(engine.initialize(&error), qPrintable(error));
        engine.newProject();
        ShowControl slider;
        slider.name = "Level";
        slider.max = 10;
        slider.step = 0.5;
        ShowControl button;
        button.name = "Flash";
        button.kind = ShowControl::Kind::Button;
        ShowControl color;
        color.name = "Wash";
        color.kind = ShowControl::Kind::Color;
        const quint64 a = engine.addControl(slider), b = engine.addControl(button), c = engine.addControl(color);
        if (qEnvironmentVariableIsSet("NYXMAP_REMOTE_DEMO")) { // serves the page for a look in a browser, then quits
            ShowControl toggle, choice;
            toggle.name = "Strobe";
            toggle.kind = ShowControl::Kind::Toggle;
            choice.name = "Scene";
            choice.kind = ShowControl::Kind::Choice;
            choice.choices = {"Intro", "Verse", "Drop"};
            engine.addControl(toggle);
            engine.addControl(choice);
            RemoteServer demo(&engine);
            demo.start(8799, qEnvironmentVariableIsSet("NYXMAP_REMOTE_NOCODE") ? QString() : QStringLiteral("482913"), "127.0.0.1");
            std::cerr << "DEMO http://127.0.0.1:8799/?pin=482913\n";
            QElapsedTimer limit;
            limit.start();
            while (limit.elapsed() < 150000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return 0;
        }

        require(RemoteServer::pinValid("1234") && RemoteServer::pinValid("Show2026") && !RemoteServer::pinValid("123") &&
                    !RemoteServer::pinValid("12 45") && !RemoteServer::pinValid("ab#de") && !RemoteServer::pinValid(QString(33, 'a')) &&
                    !RemoteServer::pinValid("é1234"),
                "a code is 4 to 32 letters or digits, chosen by the operator");
        const QString generated = RemoteServer::makePin(6);
        require(RemoteServer::pinValid(generated) && generated.size() == 6, "a generated PIN is valid");
        RemoteServer server(&engine);
        require(!server.start(0, "12"), "a short PIN is refused");
        require(!server.start(0, "no spaces"), "a code with a space is refused");
        require(server.start(0, "482913", "127.0.0.1") && server.isRunning() && server.port() != 0, "started");
        const quint16 port = server.port();

        // The page and its policy
        {
            Client h;
            require(h.connectTo(port), "connect");
            const QByteArray reply = h.request("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
            require(reply.startsWith("HTTP/1.1 200") && reply.contains("<title>NyxMap remote</title>"), "the page is served");
            require(reply.contains("Content-Security-Policy: default-src 'none'; script-src 'sha256-") && reply.contains("style-src 'sha256-") &&
                        reply.contains("connect-src 'self'"), "its script and style are hashed into the policy, nothing else allowed");
            require(reply.contains("X-Frame-Options: DENY") && reply.contains("nosniff") && reply.contains("no-store"), "and the usual headers");
            require(!reply.contains("482913"), "the PIN is nowhere in the page");
            require(reply.contains("id=\"go\"") && reply.contains("Connect"), "the login has a button: a phone's keypad may have no Enter key");
            // The hash is that of the code served
            const QByteArray scriptHash = "'sha256-" + QCryptographicHash::hash(QByteArray(remote_page::kScript), QCryptographicHash::Sha256).toBase64() + "'";
            require(reply.contains(scriptHash), "the policy's hash is the script's");
        }
        {
            Client h;
            require(h.connectTo(port), "connect");
            require(h.request("GET /layers HTTP/1.1\r\nHost: x\r\n\r\n").startsWith("HTTP/1.1 404"), "nothing else is served");
        }
        {
            Client h;
            require(h.connectTo(port), "connect");
            require(h.request("POST / HTTP/1.1\r\nHost: x\r\n\r\n").startsWith("HTTP/1.1 400"), "only GET");
        }

        // Whether a code is asked, said before any code
        {
            Client h;
            require(h.connectTo(port), "connect");
            const QByteArray reply = h.request("GET /info HTTP/1.1\r\nHost: x\r\n\r\n");
            require(reply.startsWith("HTTP/1.1 200") && reply.contains("{\"pin\":true}") && !reply.contains("482913"), "/info says a code is asked");
        }
        // The PIN, the origin
        {
            Client h;
            require(h.connectTo(port), "connect");
            require(h.upgrade("/ws").startsWith("HTTP/1.1 403"), "no PIN: refused");
        }
        {
            Client h;
            require(h.connectTo(port), "connect");
            require(h.upgrade("/ws?pin=482914").startsWith("HTTP/1.1 403"), "a wrong PIN: refused");
        }
        {
            Client h;
            require(h.connectTo(port), "connect");
            require(h.upgrade("/ws?pin=482913", "http://evil.example", "127.0.0.1").startsWith("HTTP/1.1 403"), "a page of another site: refused, whatever the PIN");
        }
        {
            Client h;
            require(h.connectTo(port), "connect");
            require(h.upgrade("/nope?pin=482913").startsWith("HTTP/1.1 403"), "only /ws upgrades");
        }

        // A phone: the right PIN from its own page
        Client phone;
        require(phone.connectTo(port), "connect");
        const QByteArray origin = "http://127.0.0.1:" + QByteArray::number(port);
        const QByteArray host = "127.0.0.1:" + QByteArray::number(port);
        require(phone.upgrade("/ws?pin=482913", origin, host).startsWith("HTTP/1.1 101"), "the right PIN, from the page's own origin: accepted");
        phone.pump(700);
        auto first = phone.messages();
        require(!first.isEmpty() && first[0].value("t").toString() == "controls", "it gets the list at once");
        const QJsonArray list = first[0].value("list").toArray();
        require(list.size() == 3 && list[0].toObject().value("name").toString() == "Level" &&
                    list[0].toObject().value("kind").toString() == "slider" && near(list[0].toObject().value("max").toDouble(), 10) &&
                    list[2].toObject().contains("rgb"), "with the kinds, the ranges and the values");
        require(server.clientCount() == 1, "one client");

        // From the phone to the show
        phone.sendText(QJsonDocument(QJsonObject{{"t", "set"}, {"id", QString::number(a)}, {"v", 7.5}}).toJson(QJsonDocument::Compact));
        phone.sendText(QJsonDocument(QJsonObject{{"t", "set"}, {"id", QString::number(b)}, {"v", 1}}).toJson(QJsonDocument::Compact));
        phone.sendText(QJsonDocument(QJsonObject{{"t", "color"}, {"id", QString::number(c)}, {"rgb", QJsonArray{0.1, 0.2, 0.3}}}).toJson(QJsonDocument::Compact));
        phone.pump(700);
        require(near(engine.control(a)->value, 7.5) && engine.control(b)->value == 1.0 && near(engine.control(c)->rgb[2], 0.3), "a slider, a held button and a color set from the phone");
        phone.sendText(QJsonDocument(QJsonObject{{"t", "set"}, {"id", QString::number(a)}, {"v", 99}}).toJson(QJsonDocument::Compact));
        phone.sendText("not json");
        phone.sendText(QJsonDocument(QJsonObject{{"t", "set"}, {"id", "999999"}, {"v", 1}}).toJson(QJsonDocument::Compact));
        phone.sendText(QJsonDocument(QJsonObject{{"t", "layer"}, {"id", QString::number(a)}, {"v", 1}}).toJson(QJsonDocument::Compact));
        phone.pump(700);
        require(near(engine.control(a)->value, 10), "a value beyond the range is kept in it");
        require(server.isRunning(), "junk is ignored, nothing falls");

        // From the show to the phone
        phone.in.clear();
        engine.setControlValue(a, 2.5);
        engine.setControlColor(c, 1, 0, 0);
        phone.pump(800);
        QJsonObject values;
        for (const QJsonObject &m : phone.messages()) {
            if (m.value("t").toString() != "values") continue;
            const QJsonObject v = m.value("v").toObject();
            for (auto it = v.begin(); it != v.end(); ++it) values[it.key()] = it.value();
        }
        require(near(values.value(QString::number(a)).toDouble(-1), 2.5), "a value changed in NyxMap reaches the phone");
        require(values.value(QString::number(c)).toArray().size() == 3 && near(values.value(QString::number(c)).toArray()[0].toDouble(), 1.0), "and a color");
        require(!values.contains(QString::number(b)) || values.value(QString::number(b)).toDouble() == 1.0, "only what changed");
        // Nothing changes: nothing is sent
        phone.in.clear();
        phone.pump(700);
        require(phone.messages().isEmpty(), "a still show sends nothing");
        // The list changes: the phone gets the new list
        phone.in.clear();
        ShowControl extra;
        extra.name = "Extra";
        engine.addControl(extra);
        phone.pump(800);
        bool refreshed = false;
        for (const QJsonObject &m : phone.messages()) refreshed |= m.value("t").toString() == "controls" && m.value("list").toArray().size() == 4;
        require(refreshed, "a control added in NyxMap appears on the phone");

        // A frame that is too large, or not masked, drops the client
        {
            Client big;
            require(big.connectTo(port), "connect");
            require(big.upgrade("/ws?pin=482913", origin, host).startsWith("HTTP/1.1 101"), "second phone");
            big.sendText(QByteArray(200, 'x')); // fine
            QByteArray huge;
            huge.append(char(0x81));
            huge.append(char(0xFE));
            huge.append(char(0x27));
            huge.append(char(0x10)); // 10000 bytes
            huge.append(QByteArray(4, 'm'));
            huge.append(QByteArray(10000, 'x'));
            big.socket.write(huge);
            big.socket.flush();
            big.pump(800);
            require(big.socket.state() != QAbstractSocket::ConnectedState, "an oversize frame drops the connection");
        }
        // The lockout: eight wrong PINs, then even the right one is refused for a while
        {
            for (int i = 0; i < 8; ++i) {
                Client h;
                require(h.connectTo(port), "connect");
                h.upgrade("/ws?pin=000000", origin, host);
            }
            Client h;
            require(h.connectTo(port), "connect");
            const QByteArray reply = h.upgrade("/ws?pin=482913", origin, host);
            require(reply.startsWith("HTTP/1.1 429") && reply.contains("Retry-After"), "eight wrong PINs lock the address out");
        }
        // A server that asks no code: the page connects at once; the origin rule still holds
        {
            RemoteServer open(&engine);
            require(open.start(0, QString(), "127.0.0.1") && open.status().contains("no code"), "started without a code");
            const quint16 p = open.port();
            const QByteArray o = "http://127.0.0.1:" + QByteArray::number(p), hh = "127.0.0.1:" + QByteArray::number(p);
            Client i;
            require(i.connectTo(p), "connect");
            require(i.request("GET /info HTTP/1.1\r\nHost: x\r\n\r\n").contains("{\"pin\":false}"), "/info says no code is asked");
            Client w;
            require(w.connectTo(p), "connect");
            require(w.upgrade("/ws", o, hh).startsWith("HTTP/1.1 101"), "no code: it connects without one");
            w.pump(700);
            require(!w.messages().isEmpty() && w.messages()[0].value("t").toString() == "controls", "and gets the controls");
            Client x;
            require(x.connectTo(p), "connect");
            require(x.upgrade("/ws", "http://evil.example", hh).startsWith("HTTP/1.1 403"), "a page of another site is refused even without a code");
            open.stop();
        }
        // Stopped: nothing listens
        server.stop();
        for (int i = 0; i < 20; ++i) { QCoreApplication::processEvents(); QThread::msleep(10); }
        // Nothing answers any more (a refused connection, or one the system drops at once)
        Client gone;
        const bool answered = gone.connectTo(port) && gone.request("GET / HTTP/1.1\r\nHost: x\r\n\r\n").startsWith("HTTP/");
        require(!answered && !server.isRunning() && server.clientCount() == 0, "stopped");
        engine.shutdown();
        std::cout << "Remote server: page, policy, PIN, origin, lockout, both ways, limits passed\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
