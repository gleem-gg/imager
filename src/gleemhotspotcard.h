/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Gleem
 *
 * The QR code and the printable card for the IRL Sidekick's setup hotspot
 * (see gleemhotspot.h). GUI builds only: it needs QtGui.
 */

#ifndef GLEEMHOTSPOTCARD_H
#define GLEEMHOTSPOTCARD_H

#include "gleemhotspot.h"

#include <QImage>
#include <QString>

class QPainter;
class QRectF;

namespace gleemhotspot {

// A QR code for `text`, black on white, `modulePixels` per module and the
// standard four-module quiet zone. Null image if the text cannot be encoded.
QImage qrCodeImage(const QString &text, int modulePixels = 8);

// The same as a PNG data: URL, for an Image element. Empty on failure.
QString qrCodeDataUrl(const QString &text, int modulePixels = 8);

// Draw the card (title, QR code, name, password, short instructions) into
// `target`, keeping its aspect ratio. Shared by the PNG and the PDF so both
// say the same thing.
void paintCard(QPainter &painter, const QRectF &target, const Credentials &credentials);

// Logical size the card is laid out in; paintCard scales it to the target.
inline constexpr int kCardWidth = 1000;
inline constexpr int kCardHeight = 1220;

// The card as an image, `width` pixels wide.
QImage renderCardImage(const Credentials &credentials, int width = 1000);

// Save the card. PNG: the image above. PDF: one A4 page with the card at
// print size (about 9 cm wide) and a cut line, so it can be stuck on the box.
bool saveCardPng(const Credentials &credentials, const QString &path);
bool saveCardPdf(const Credentials &credentials, const QString &path);

} // namespace gleemhotspot

#endif // GLEEMHOTSPOTCARD_H
