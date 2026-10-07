/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Gleem
 */

#include "gleemhotspotcard.h"

#include "qrcodegen.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRectF>
#include <QSaveFile>

#include <optional>
#include <stdexcept>

namespace gleemhotspot {

namespace {

constexpr int kQuietZone = 4;

std::optional<qrcodegen::QrCode> encode(const QString &text)
{
    if (text.isEmpty())
        return std::nullopt;
    try {
        const QByteArray utf8 = text.toUtf8();
        return qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    } catch (const std::exception &) {
        // data_too_long, or a payload with an embedded NUL. Never print the
        // text: it carries the password.
        return std::nullopt;
    }
}

// Paint the modules of `qr` into `rect` (a square), quiet zone included.
void paintQr(QPainter &painter, const QRectF &rect, const qrcodegen::QrCode &qr)
{
    const int size = qr.getSize();
    const double module = rect.width() / (size + 2 * kQuietZone);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRect(rect);
    painter.setBrush(Qt::black);
    for (int y = 0; y < size; ++y) {
        int x = 0;
        while (x < size) {
            if (!qr.getModule(x, y)) {
                ++x;
                continue;
            }
            // Merge horizontal runs: fewer rectangles, and no hairline seams
            // between neighbouring modules in PDF viewers.
            int run = 1;
            while (x + run < size && qr.getModule(x + run, y))
                ++run;
            painter.drawRect(QRectF(rect.left() + (x + kQuietZone) * module,
                                    rect.top() + (y + kQuietZone) * module,
                                    run * module, module));
            x += run;
        }
    }
    painter.restore();
}

QString tr(const char *text)
{
    return QCoreApplication::translate("GleemHotspotCard", text);
}

} // namespace

QImage qrCodeImage(const QString &text, int modulePixels)
{
    const auto qr = encode(text);
    if (!qr || modulePixels < 1)
        return {};

    const int side = (qr->getSize() + 2 * kQuietZone) * modulePixels;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    paintQr(painter, QRectF(0, 0, side, side), *qr);
    painter.end();
    return image;
}

QString qrCodeDataUrl(const QString &text, int modulePixels)
{
    const QImage image = qrCodeImage(text, modulePixels);
    if (image.isNull())
        return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

void paintCard(QPainter &painter, const QRectF &target, const Credentials &credentials)
{
    painter.save();

    // Lay the card out in fixed logical units and let the transform scale it,
    // so the image and the PDF come out identical whatever their resolution.
    const double scale = qMin(target.width() / kCardWidth, target.height() / kCardHeight);
    painter.translate(target.left() + (target.width() - kCardWidth * scale) / 2,
                      target.top() + (target.height() - kCardHeight * scale) / 2);
    painter.scale(scale, scale);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF card(0, 0, kCardWidth, kCardHeight);
    painter.fillRect(card, Qt::white);

    QFont regular = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    QFont bold = regular;
    bold.setBold(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setStyleHint(QFont::Monospace);
    mono.setBold(true);

    const double margin = 60;
    const double width = kCardWidth - 2 * margin;
    double y = 50;

    painter.setPen(Qt::black);

    bold.setPixelSize(56);
    painter.setFont(bold);
    painter.drawText(QRectF(margin, y, width, 70), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("Gleem IRL Sidekick"));
    y += 75;

    regular.setPixelSize(34);
    painter.setFont(regular);
    painter.setPen(QColor(0x44, 0x44, 0x44));
    painter.drawText(QRectF(margin, y, width, 45), Qt::AlignHCenter | Qt::AlignVCenter,
                     tr("Setup hotspot"));
    y += 65;

    const double qrSide = 620;
    if (const auto qr = encode(wifiQrPayload(credentials)))
        paintQr(painter, QRectF((kCardWidth - qrSide) / 2, y, qrSide, qrSide), *qr);
    y += qrSide + 30;

    auto field = [&](const QString &label, const QString &value) {
        regular.setPixelSize(30);
        painter.setFont(regular);
        painter.setPen(QColor(0x44, 0x44, 0x44));
        painter.drawText(QRectF(margin, y, width, 40), Qt::AlignHCenter | Qt::AlignVCenter, label);
        y += 42;
        mono.setPixelSize(52);
        painter.setFont(mono);
        painter.setPen(Qt::black);
        painter.drawText(QRectF(margin, y, width, 66), Qt::AlignHCenter | Qt::AlignVCenter, value);
        y += 84;
    };
    field(tr("Network name"), credentials.ssid);
    field(tr("Password"), credentials.password);

    regular.setPixelSize(28);
    painter.setFont(regular);
    painter.setPen(QColor(0x22, 0x22, 0x22));
    painter.drawText(QRectF(margin, y, width, kCardHeight - y - 30),
                     Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                     tr("Scan the QR code with the Gleem IRL app to set up the box, "
                        "or join the network and open %1").arg(QLatin1String(kSetupUrl)));

    painter.restore();
}

QImage renderCardImage(const Credentials &credentials, int width)
{
    if (width < 100)
        width = 100;
    const int height = width * kCardHeight / kCardWidth;
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    paintCard(painter, QRectF(0, 0, width, height), credentials);
    painter.end();
    return image;
}

bool saveCardPng(const Credentials &credentials, const QString &path)
{
    if (!isValid(credentials) || path.isEmpty())
        return false;
    const QImage image = renderCardImage(credentials, 1000);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (!image.save(&file, "PNG")) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool saveCardPdf(const Credentials &credentials, const QString &path)
{
    if (!isValid(credentials) || path.isEmpty())
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    {
        QPdfWriter pdf(&file);
        pdf.setCreator(QStringLiteral("Gleem Imager"));
        pdf.setTitle(QStringLiteral("Gleem IRL Sidekick"));
        pdf.setResolution(300);
        pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                                      QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter));

        QPainter painter;
        if (!painter.begin(&pdf)) {
            file.cancelWriting();
            return false;
        }

        // Card about 90 mm wide at the top of the page, with a dashed cut line.
        const double dotsPerMm = pdf.resolution() / 25.4;
        const double cardWidth = 90 * dotsPerMm;
        const double cardHeight = cardWidth * kCardHeight / kCardWidth;
        const QRectF page = QRectF(QPointF(0, 0), QSizeF(pdf.width(), pdf.height()));
        const QRectF cardRect((page.width() - cardWidth) / 2, 0, cardWidth, cardHeight);

        paintCard(painter, cardRect, credentials);

        QPen cut(QColor(0x88, 0x88, 0x88));
        cut.setStyle(Qt::DashLine);
        cut.setWidthF(0.3 * dotsPerMm);
        painter.setPen(cut);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cardRect.adjusted(-3 * dotsPerMm, -3 * dotsPerMm,
                                           3 * dotsPerMm, 3 * dotsPerMm));

        QFont small = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        small.setPointSizeF(9);
        painter.setFont(small);
        painter.setPen(QColor(0x55, 0x55, 0x55));
        painter.drawText(QRectF(0, cardRect.bottom() + 8 * dotsPerMm, page.width(), 20 * dotsPerMm),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                         tr("Cut out the card and stick it on the box. Writing the card "
                            "again creates a new name and password."));
        painter.end();
    }

    return file.commit();
}

} // namespace gleemhotspot
