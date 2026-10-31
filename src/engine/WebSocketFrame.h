#pragma once
// The frames of a WebSocket (RFC 6455) as a small server needs them: text and control frames, no fragmentation, no extensions.
// A client's frames are masked, a server's are not.

#include <QByteArray>
#include <QtEndian>

namespace websocket {
enum Opcode { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

// A server frame to send (`opcode`, `payload`)
inline QByteArray encode(int opcode, const QByteArray &payload)
{
    QByteArray f;
    f.append(char(0x80 | opcode));
    if (payload.size() < 126) {
        f.append(char(payload.size()));
    } else if (payload.size() < 65536) {
        f.append(char(126));
        char n[2];
        qToBigEndian(quint16(payload.size()), n);
        f.append(n, 2);
    } else {
        f.append(char(127));
        char n[8];
        qToBigEndian(quint64(payload.size()), n);
        f.append(n, 8);
    }
    return f + payload;
}

// The first frame of `buffer`, taken out of it. Returns 1 when one was read (opcode, payload), 0 when more bytes are needed, -1
// when the peer must be dropped (a frame above `maxPayload`, a fragmented message, an unmasked client frame)
inline int decode(QByteArray &buffer, int *opcode, QByteArray *payload, quint64 maxPayload)
{
    if (buffer.size() < 2) return 0;
    const bool fin = buffer[0] & 0x80;
    *opcode = buffer[0] & 0x0F;
    const bool masked = buffer[1] & 0x80;
    quint64 len = quint8(buffer[1]) & 0x7F;
    int pos = 2;
    if (len == 126) {
        if (buffer.size() < 4) return 0;
        len = qFromBigEndian<quint16>(buffer.constData() + 2);
        pos = 4;
    } else if (len == 127) {
        if (buffer.size() < 10) return 0;
        len = qFromBigEndian<quint64>(buffer.constData() + 2);
        pos = 10;
    }
    if (len > maxPayload || !fin || *opcode == Continuation || !masked) return -1;
    const int maskAt = pos;
    pos += 4;
    if (quint64(buffer.size()) < quint64(pos) + len) return 0;
    *payload = buffer.mid(pos, int(len));
    for (int k = 0; k < payload->size(); ++k) (*payload)[k] = char((*payload)[k] ^ buffer[maskAt + (k & 3)]);
    buffer.remove(0, pos + int(len));
    return 1;
}
} // namespace websocket
