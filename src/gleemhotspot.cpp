/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Gleem
 */

#include "gleemhotspot.h"

#include <QRandomGenerator>

#include <cstring>

namespace gleemhotspot {

namespace {

QString randomString(QRandomGenerator *rng, const char *alphabet, int length)
{
    const auto alphabetSize = static_cast<quint32>(std::strlen(alphabet));
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out.append(QLatin1Char(alphabet[rng->bounded(alphabetSize)]));
    return out;
}

void wipe(QString &s)
{
    // detach() first so a shared copy elsewhere is not overwritten under it;
    // fill() then writes over this instance's own buffer.
    if (!s.isEmpty()) {
        s.detach();
        s.fill(QLatin1Char('\0'));
    }
    s.clear();
}

} // namespace

void Credentials::clear()
{
    wipe(ssid);
    wipe(password);
}

Credentials generate(QRandomGenerator *rng)
{
    if (!rng)
        rng = QRandomGenerator::system();

    Credentials c;
    c.ssid = QString::fromLatin1(kSsidPrefix)
             + randomString(rng, kSsidAlphabet, kSsidSuffixLength);
    c.password = randomString(rng, kPasswordAlphabet, kPasswordLength);
    return c;
}

bool isValidSsid(const QString &ssid)
{
    const QByteArray utf8 = ssid.toUtf8();
    if (utf8.isEmpty() || utf8.size() > 32)
        return false;
    return !utf8.contains('\n') && !utf8.contains('\r') && !utf8.contains('\0');
}

bool isValidPassword(const QString &password)
{
    if (password.size() < 8 || password.size() > 63)
        return false;
    for (const QChar ch : password) {
        const ushort u = ch.unicode();
        if (u < 0x20 || u > 0x7e)
            return false;
    }
    return true;
}

bool isValid(const Credentials &credentials)
{
    return isValidSsid(credentials.ssid) && isValidPassword(credentials.password);
}

QByteArray confFileContents(const Credentials &credentials)
{
    QByteArray out;
    out.reserve(64);
    out += "SSID=";
    out += credentials.ssid.toUtf8();
    out += '\n';
    out += "PASSWORD=";
    out += credentials.password.toLatin1();
    out += '\n';
    return out;
}

QString escapeWifiQrValue(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char(';') || ch == QLatin1Char(',')
            || ch == QLatin1Char(':') || ch == QLatin1Char('"'))
            out.append(QLatin1Char('\\'));
        out.append(ch);
    }
    return out;
}

QString wifiQrPayload(const Credentials &credentials)
{
    return QStringLiteral("WIFI:S:") + escapeWifiQrValue(credentials.ssid)
           + QStringLiteral(";T:WPA;P:") + escapeWifiQrValue(credentials.password)
           + QStringLiteral(";;");
}

} // namespace gleemhotspot
