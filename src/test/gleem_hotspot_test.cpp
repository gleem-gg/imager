/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Gleem
 *
 * The Gleem IRL Sidekick setup hotspot: the credentials the Imager makes up
 * per write, the gleem-hotspot.conf the box parses on first boot, the Wi-Fi
 * QR code and the printable card, and how an OS list entry switches it on.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "app_resources.h"
#include "gleemhotspot.h"
#include "gleemhotspotcard.h"
#include "imagewriter.h"
#include "oslistmodel.h"
#include "test_scratch.h"

#include <QByteArray>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>

using gleemhotspot::Credentials;

namespace {

// What the box's own parser does with the file (irl-sidekick PR #30):
// KEY=VALUE per line, the value running to the end of the line, CRLF allowed.
QMap<QByteArray, QByteArray> parseConf(const QByteArray &contents)
{
    QMap<QByteArray, QByteArray> out;
    for (QByteArray line : contents.split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        out.insert(line.left(eq), line.mid(eq + 1));
    }
    return out;
}

class TestableImageWriter : public ImageWriter
{
public:
    TestableImageWriter() : ImageWriter(nullptr) {}

    void feedOsList(const QByteArray &json)
    {
        onOsListFetchComplete(json, QUrl(QStringLiteral("https://example.invalid/os_list.json")),
                              QUrl(QStringLiteral("https://example.invalid/os_list.json")));
    }
};

} // namespace

TEST_CASE("Hotspot credentials follow the box's own scheme", "[gleem][hotspot]")
{
    static const QRegularExpression ssidPattern(
        QStringLiteral("^IRL-Sidekick-[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{4}$"));
    static const QRegularExpression passwordPattern(
        QStringLiteral("^[abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789]{12}$"));

    QSet<QString> ssids, passwords;
    for (int i = 0; i < 200; ++i) {
        const Credentials c = gleemhotspot::generate();
        INFO("ssid " << c.ssid.toStdString());
        CHECK(ssidPattern.match(c.ssid).hasMatch());
        CHECK(passwordPattern.match(c.password).hasMatch());
        CHECK(gleemhotspot::isValid(c));
        ssids.insert(c.ssid);
        passwords.insert(c.password);
    }
    // 56^12 passwords: any repeat in 200 draws means the generator is broken.
    CHECK(passwords.size() == 200);
    // 32^4 = 1,048,576 names; 200 draws repeat with a probability of ~2%,
    // so only ask for "clearly not constant".
    CHECK(ssids.size() > 150);
}

TEST_CASE("No look-alike characters in either alphabet", "[gleem][hotspot]")
{
    for (const char *alphabet : { gleemhotspot::kSsidAlphabet, gleemhotspot::kPasswordAlphabet }) {
        const QString a = QString::fromLatin1(alphabet);
        for (const QChar bad : { QChar('0'), QChar('O'), QChar('1'), QChar('I'), QChar('l') })
            CHECK_FALSE(a.contains(bad));
    }
}

TEST_CASE("A given generator gives reproducible credentials", "[gleem][hotspot]")
{
    QRandomGenerator a(42), b(42);
    const Credentials x = gleemhotspot::generate(&a);
    const Credentials y = gleemhotspot::generate(&b);
    CHECK(x.ssid == y.ssid);
    CHECK(x.password == y.password);
}

TEST_CASE("gleem-hotspot.conf has exactly the two lines the box parses", "[gleem][hotspot]")
{
    const Credentials c{ QStringLiteral("IRL-Sidekick-AB12"), QStringLiteral("abcdefghijkmnpqrstuv") };
    const QByteArray conf = gleemhotspot::confFileContents(c);
    CHECK(conf == QByteArray("SSID=IRL-Sidekick-AB12\nPASSWORD=abcdefghijkmnpqrstuv\n"));
    CHECK(QByteArray(gleemhotspot::kConfFileName) == QByteArray("gleem-hotspot.conf"));

    // Round trip through the box's parsing rules, for a freshly generated pair.
    const Credentials g = gleemhotspot::generate();
    const auto parsed = parseConf(gleemhotspot::confFileContents(g));
    CHECK(parsed.size() == 2);
    CHECK(parsed.value("SSID") == g.ssid.toUtf8());
    CHECK(parsed.value("PASSWORD") == g.password.toUtf8());
}

TEST_CASE("Hotspot credential validation matches the box's limits", "[gleem][hotspot]")
{
    CHECK(gleemhotspot::isValidSsid(QStringLiteral("a")));
    CHECK(gleemhotspot::isValidSsid(QString(32, QLatin1Char('x'))));
    CHECK_FALSE(gleemhotspot::isValidSsid(QString()));
    CHECK_FALSE(gleemhotspot::isValidSsid(QString(33, QLatin1Char('x'))));
    // 32 bytes, not characters: eleven three-byte characters are 33 bytes.
    CHECK_FALSE(gleemhotspot::isValidSsid(QString(11, QChar(0x20AC))));
    CHECK_FALSE(gleemhotspot::isValidSsid(QStringLiteral("a\nb")));

    CHECK(gleemhotspot::isValidPassword(QStringLiteral("12345678")));
    CHECK(gleemhotspot::isValidPassword(QString(63, QLatin1Char('x'))));
    CHECK_FALSE(gleemhotspot::isValidPassword(QStringLiteral("1234567")));
    CHECK_FALSE(gleemhotspot::isValidPassword(QString(64, QLatin1Char('x'))));
    CHECK_FALSE(gleemhotspot::isValidPassword(QStringLiteral("abcdefg\x7f")));
    CHECK_FALSE(gleemhotspot::isValidPassword(QStringLiteral("abcdefgé")));
}

TEST_CASE("The Wi-Fi QR payload escapes its special characters", "[gleem][hotspot]")
{
    CHECK(gleemhotspot::escapeWifiQrValue(QStringLiteral("a\\b;c,d:e\"f"))
          == QStringLiteral("a\\\\b\\;c\\,d\\:e\\\"f"));

    const Credentials plain{ QStringLiteral("IRL-Sidekick-AB12"), QStringLiteral("abcdefghijkmnpqrstuv") };
    CHECK(gleemhotspot::wifiQrPayload(plain)
          == QStringLiteral("WIFI:S:IRL-Sidekick-AB12;T:WPA;P:abcdefghijkmnpqrstuv;;"));

    const Credentials odd{ QStringLiteral("My;Net"), QStringLiteral("pa:ss,wo\"rd") };
    CHECK(gleemhotspot::wifiQrPayload(odd)
          == QStringLiteral("WIFI:S:My\\;Net;T:WPA;P:pa\\:ss\\,wo\\\"rd;;"));
}

TEST_CASE("The QR code renders", "[gleem][hotspot]")
{
    const Credentials c = gleemhotspot::generate();
    const QImage qr = gleemhotspot::qrCodeImage(gleemhotspot::wifiQrPayload(c), 4);
    REQUIRE_FALSE(qr.isNull());
    CHECK(qr.width() == qr.height());
    // A ~60-byte payload at ECC M needs version 4 (33 modules) or 5 (37);
    // plus the 4-module quiet zone either side, times 4 pixels.
    CHECK((qr.width() == (33 + 8) * 4 || qr.width() == (37 + 8) * 4));
    // Quiet zone white, finder pattern's corner black.
    CHECK(qr.pixelColor(0, 0) == QColor(Qt::white));
    CHECK(qr.pixelColor(4 * 4, 4 * 4) == QColor(Qt::black));

    const QString url = gleemhotspot::qrCodeDataUrl(gleemhotspot::wifiQrPayload(c));
    CHECK(url.startsWith(QStringLiteral("data:image/png;base64,")));
    QImage decoded;
    REQUIRE(decoded.loadFromData(QByteArray::fromBase64(url.mid(22).toLatin1()), "PNG"));
    CHECK(decoded.width() == decoded.height());

    CHECK(gleemhotspot::qrCodeImage(QString()).isNull());
    CHECK(gleemhotspot::qrCodeDataUrl(QString()).isEmpty());
}

TEST_CASE("The hotspot card saves as PNG and PDF", "[gleem][hotspot]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const Credentials c = gleemhotspot::generate();

    const QString png = dir.filePath(QStringLiteral("card.png"));
    REQUIRE(gleemhotspot::saveCardPng(c, png));
    QImage image(png);
    REQUIRE_FALSE(image.isNull());
    CHECK(image.width() == 1000);
    CHECK(image.height() == 1000 * gleemhotspot::kCardHeight / gleemhotspot::kCardWidth);

    const QString pdf = dir.filePath(QStringLiteral("card.pdf"));
    REQUIRE(gleemhotspot::saveCardPdf(c, pdf));
    QFile f(pdf);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    CHECK(bytes.startsWith("%PDF-"));
    CHECK(bytes.size() > 1000);

    // Keep a copy to look at: GLEEM_CARD_DIR=<dir>.
    const QString keep = qEnvironmentVariable("GLEEM_CARD_DIR");
    if (!keep.isEmpty()) {
        QFile::remove(keep + QStringLiteral("/hotspot-card.png"));
        QFile::remove(keep + QStringLiteral("/hotspot-card.pdf"));
        QFile::copy(png, keep + QStringLiteral("/hotspot-card.png"));
        QFile::copy(pdf, keep + QStringLiteral("/hotspot-card.pdf"));
    }

    // Nothing to print without credentials.
    CHECK_FALSE(gleemhotspot::saveCardPng(Credentials{}, dir.filePath(QStringLiteral("x.png"))));
    CHECK_FALSE(gleemhotspot::saveCardPdf(Credentials{}, dir.filePath(QStringLiteral("x.pdf"))));
}

TEST_CASE("Credentials::clear() forgets both values", "[gleem][hotspot]")
{
    Credentials c = gleemhotspot::generate();
    const QString copy = c.password;
    c.clear();
    CHECK(c.isEmpty());
    // A copy taken before is its own string and is left alone.
    CHECK(copy.size() == gleemhotspot::kPasswordLength);
}

TEST_CASE("The OS list's gleem_hotspot key reaches the model in three states",
          "[gleem][hotspot][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.0.1", "devices": [] },
        "os_list": [
            { "name": "Sidekick Pi", "url": "https://example.invalid/pi.img.xz",
              "extract_size": 1000, "gleem_hotspot": true },
            { "name": "Sidekick OPi", "url": "https://example.invalid/opi.img.xz",
              "extract_size": 1000, "gleem_hotspot": false },
            { "name": "Something else", "url": "https://example.invalid/x.img.xz",
              "extract_size": 1000 },
            { "name": "Not a bool", "url": "https://example.invalid/y.img.xz",
              "extract_size": 1000, "gleem_hotspot": "yes" }
        ]
    })JSON"));
    OSListModel *osList = writer.getOSList();
    REQUIRE(osList->reload());
    // The overrides are protected on the subclass; QML drives the base.
    const QAbstractItemModel *model = osList;

    QMap<QString, QVariant> byName;
    for (int i = 0; i < model->rowCount(QModelIndex()); ++i) {
        const QModelIndex idx = model->index(i, 0);
        byName.insert(idx.data(OSListModel::NameRole).toString(),
                      idx.data(OSListModel::GleemHotspotRole));
    }
    REQUIRE(byName.contains(QStringLiteral("Sidekick Pi")));
    CHECK(byName.value(QStringLiteral("Sidekick Pi")) == QVariant(true));
    CHECK(byName.value(QStringLiteral("Sidekick OPi")) == QVariant(false));
    CHECK_FALSE(byName.value(QStringLiteral("Something else")).isValid());
    CHECK_FALSE(byName.value(QStringLiteral("Not a bool")).isValid());
}

TEST_CASE("ImageWriter resets the hotspot mode with every new source", "[gleem][hotspot]")
{
    TestableImageWriter writer;
    CHECK(writer.gleemHotspotMode() == ImageWriter::GleemHotspotMode::GleemHotspotNone);

    writer.setSrc(QUrl(QStringLiteral("https://example.invalid/pi.img.xz")), 10, 10);
    writer.setGleemHotspotMode(ImageWriter::GleemHotspotMode::GleemHotspotWriteCredentials);
    CHECK(writer.gleemHotspotMode() == ImageWriter::GleemHotspotMode::GleemHotspotWriteCredentials);

    // Choosing anything else -- a custom image included -- turns it off
    // until an OS list entry turns it back on.
    writer.setSrc(QUrl(QStringLiteral("https://example.invalid/other.img.xz")), 10, 10);
    CHECK(writer.gleemHotspotMode() == ImageWriter::GleemHotspotMode::GleemHotspotNone);

    // Nothing written yet, so nothing to show, save or clear.
    CHECK(writer.gleemHotspotSsid().isEmpty());
    CHECK(writer.gleemHotspotPassword().isEmpty());
    CHECK_FALSE(writer.saveGleemHotspotCardToFile(QStringLiteral("/tmp/never.png"), QStringLiteral("png")));
    CHECK(writer.gleemHotspotQrDataUrl(QString(), QString()).isEmpty());
    CHECK(writer.gleemHotspotQrDataUrl(QStringLiteral("IRL-Sidekick-AB12"),
                                       QStringLiteral("abcdefghijkmnpqrstuv"))
              .startsWith(QStringLiteral("data:image/png;base64,")));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    initAppResources();
    rpi_imager_test::useScratchPaths(QStringLiteral("gleem_hotspot_test"));
    return Catch::Session().run(argc, argv);
}
