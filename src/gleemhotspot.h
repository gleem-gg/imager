/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Gleem
 *
 * Setup-hotspot credentials for the Gleem IRL Sidekick.
 *
 * Every card written with an image marked "gleem_hotspot": true in the OS
 * list gets its own hotspot name and password. They are written to
 * gleem-hotspot.conf in the root of the FAT boot partition; the box reads the
 * file on first boot, moves the credentials into /var/lib/irl-sidekick and
 * deletes it. The same values are shown on the done screen as text and as a
 * Wi-Fi QR code.
 *
 * The password must never be logged or sent anywhere: nothing in here prints
 * it, and callers must not either.
 */

#ifndef GLEEMHOTSPOT_H
#define GLEEMHOTSPOT_H

#include <QByteArray>
#include <QString>

class QRandomGenerator;

namespace gleemhotspot {

// Name of the file in the root of the FAT boot partition (/boot/firmware on
// Raspberry Pi OS). The box parses exactly this name.
inline constexpr const char *kConfFileName = "gleem-hotspot.conf";

// The scheme the box itself uses when it has to make its own credentials:
// "IRL-Sidekick-" plus four characters, and a 12-character password, both
// from alphabets without look-alike characters (no 0/O, 1/I/l).
inline constexpr const char *kSsidPrefix = "IRL-Sidekick-";
inline constexpr const char *kSsidAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
inline constexpr const char *kPasswordAlphabet =
    "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
inline constexpr int kSsidSuffixLength = 4;
inline constexpr int kPasswordLength = 12;

struct Credentials
{
    QString ssid;
    QString password;

    bool isEmpty() const { return ssid.isEmpty() && password.isEmpty(); }
    // Overwrite before releasing, so the password does not linger in freed
    // memory longer than it has to.
    void clear();
};

// New random credentials. Uses QRandomGenerator::system() (the operating
// system's cryptographically secure generator) unless a generator is given;
// the parameter exists for tests only.
Credentials generate(QRandomGenerator *rng = nullptr);

// What the box accepts: SSID 1-32 bytes, password 8-63 printable ASCII
// (WPA2 passphrase rules). Neither may contain a line break, since the file
// is line-based.
bool isValidSsid(const QString &ssid);
bool isValidPassword(const QString &password);
bool isValid(const Credentials &credentials);

// Contents of gleem-hotspot.conf:
//   SSID=<ssid>\n
//   PASSWORD=<password>\n
QByteArray confFileContents(const Credentials &credentials);

// Escape a value for a WIFI: QR payload: \ ; , : " get a backslash.
QString escapeWifiQrValue(const QString &value);

// The payload phones understand as "join this network":
//   WIFI:S:<ssid>;T:WPA;P:<password>;;
QString wifiQrPayload(const Credentials &credentials);

// Address of the box's local interface while it runs the setup hotspot.
inline constexpr const char *kSetupUrl = "http://10.42.0.1/";

} // namespace gleemhotspot

#endif // GLEEMHOTSPOT_H
