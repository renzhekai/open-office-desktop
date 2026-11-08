#include "assistant_art.h"

#include <QCache>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace openoffice {
namespace art {
namespace {

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(qBound<qreal>(0, alpha, 1));
    return color;
}

} // namespace

void paintBall(QPainter& painter, const QRectF& bounds, qreal t, qreal energy, bool dark)
{
    const qreal side = qMin(bounds.width(), bounds.height());
    const QPointF c = bounds.center();
    const qreal R = side / 2.0;
    const qreal hair = qMax<qreal>(0.7, R / 36.0);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    if (energy > 0.01) { // a quiet glow behind the glass while working
        QRadialGradient glow(c, R * 1.38);
        glow.setColorAt(0.68, withAlpha(QColor(0x5B, 0x8C, 0xFF), 0.30 * energy));
        glow.setColorAt(1.00, Qt::transparent);
        painter.setBrush(glow);
        painter.drawEllipse(c, R * 1.38, R * 1.38);
    }

    // Glass body: clear in the middle, denser toward the edge (Fresnel).
    QPainterPath body;
    body.addEllipse(c, R, R);
    QRadialGradient glass(c + QPointF(-0.22 * R, -0.30 * R), R * 1.32);
    if (dark) {
        glass.setColorAt(0.00, QColor(0x52, 0x5A, 0x6B));
        glass.setColorAt(0.55, QColor(0x2C, 0x31, 0x3C));
        glass.setColorAt(0.86, QColor(0x1C, 0x20, 0x28));
        glass.setColorAt(1.00, QColor(0x10, 0x13, 0x18));
    } else {
        glass.setColorAt(0.00, QColor(0xFF, 0xFF, 0xFF));
        glass.setColorAt(0.50, QColor(0xEC, 0xF1, 0xF9));
        glass.setColorAt(0.82, QColor(0xD0, 0xDA, 0xEA));
        glass.setColorAt(1.00, QColor(0xA6, 0xB6, 0xD0));
    }
    painter.fillPath(body, glass);

    painter.save();
    painter.setClipPath(body);
    // Three glass petals turning slowly around the core; faster while working.
    struct Tone { QColor deep, light; };
    static const Tone tones[] = {
        {QColor(0x2F, 0x6B, 0xFF), QColor(0x9C, 0xC0, 0xFF)}, // documents
        {QColor(0x0F, 0xB3, 0x83), QColor(0x95, 0xE6, 0xC8)}, // spreadsheets
        {QColor(0xFF, 0x86, 0x33), QColor(0xFF, 0xD2, 0x99)}, // presentations
    };
    const qreal spin = t * (6.0 + 54.0 * energy);
    painter.setCompositionMode(dark ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_SourceOver);
    for (int k = 0; k < 3; ++k) {
        painter.save();
        painter.translate(c);
        painter.rotate(spin + k * 120.0);
        // A comma-shaped petal leaning with the turn: together they read as
        // a swirl in motion rather than a static pinwheel.
        const QPointF base(0, 0.05 * R), tip(0.18 * R, -0.63 * R);
        QPainterPath petal;
        petal.moveTo(base);
        petal.cubicTo(QPointF(0.30 * R, -0.02 * R), QPointF(0.36 * R, -0.40 * R), tip);
        petal.cubicTo(QPointF(0.00 * R, -0.46 * R), QPointF(-0.24 * R, -0.12 * R), base);
        // Glass tint: light along the leading edge, deep along the trailing one,
        // fading into the core at the base.
        QLinearGradient tint(QPointF(-0.22 * R, -0.22 * R), QPointF(0.34 * R, -0.30 * R));
        tint.setColorAt(0.0, withAlpha(tones[k].deep, dark ? 0.92 : 0.93));
        tint.setColorAt(1.0, withAlpha(tones[k].light, dark ? 0.80 : 0.86));
        painter.setPen(Qt::NoPen);
        painter.setBrush(tint);
        painter.drawPath(petal);
        QLinearGradient depth(base, tip);
        depth.setColorAt(0.0, withAlpha(Qt::white, dark ? 0.10 : 0.35));
        depth.setColorAt(0.45, withAlpha(Qt::white, 0.0));
        depth.setColorAt(1.0, withAlpha(Qt::white, dark ? 0.12 : 0.22));
        painter.setBrush(depth);
        painter.drawPath(petal);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(withAlpha(Qt::white, dark ? 0.16 : 0.30), hair * 0.8));
        painter.drawPath(petal);
        painter.restore();
    }
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(Qt::NoPen);

    // Luminous core; it breathes a little while working.
    const qreal coreRadius = R * 0.26 * (1.0 + 0.10 * energy * qSin(t * 4.0));
    QRadialGradient core(c, coreRadius);
    core.setColorAt(0.00, withAlpha(Qt::white, 1.0));
    core.setColorAt(0.35, withAlpha(Qt::white, 0.86));
    core.setColorAt(1.00, withAlpha(Qt::white, 0.0));
    painter.setBrush(core);
    painter.drawEllipse(c, coreRadius, coreRadius);

    // Light gathered at the bottom of the glass.
    painter.save();
    painter.translate(c + QPointF(0, 0.64 * R));
    painter.scale(1.0, 0.42);
    QRadialGradient caustic(QPointF(0, 0), 0.62 * R);
    caustic.setColorAt(0, withAlpha(Qt::white, dark ? 0.20 : 0.72));
    caustic.setColorAt(1, Qt::transparent);
    painter.setBrush(caustic);
    painter.drawEllipse(QPointF(0, 0), 0.62 * R, 0.62 * R);
    painter.restore();

    // Broad sheen over the upper half and a crisp specular highlight.
    QLinearGradient sheen(c + QPointF(0, -R), c + QPointF(0, -0.05 * R));
    sheen.setColorAt(0, withAlpha(Qt::white, dark ? 0.26 : 0.72));
    sheen.setColorAt(1, Qt::transparent);
    painter.setBrush(sheen);
    painter.drawEllipse(QRectF(c.x() - 0.70 * R, c.y() - 0.97 * R, 1.40 * R, 0.92 * R));
    painter.save();
    painter.translate(c + QPointF(-0.36 * R, -0.53 * R));
    painter.rotate(-32);
    painter.scale(1.0, 0.50);
    QRadialGradient specular(QPointF(0, 0), 0.21 * R);
    specular.setColorAt(0.0, withAlpha(Qt::white, 0.96));
    specular.setColorAt(0.6, withAlpha(Qt::white, 0.50));
    specular.setColorAt(1.0, Qt::transparent);
    painter.setBrush(specular);
    painter.drawEllipse(QPointF(0, 0), 0.21 * R, 0.21 * R);
    painter.restore();
    painter.restore(); // clip

    // Edge: an outer hairline, then a lit inner rim brightest at the top.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(dark ? withAlpha(Qt::white, 0.22) : QColor(0x1E, 0x32, 0x64, 72), hair * 1.1));
    painter.drawEllipse(c, R - hair / 2, R - hair / 2);
    QLinearGradient lit(c + QPointF(0, -R), c + QPointF(0, R));
    lit.setColorAt(0.0, withAlpha(Qt::white, dark ? 0.42 : 0.95));
    lit.setColorAt(0.5, withAlpha(Qt::white, 0.0));
    lit.setColorAt(1.0, withAlpha(Qt::white, dark ? 0.10 : 0.35));
    painter.setPen(QPen(QBrush(lit), hair * 1.3));
    painter.drawEllipse(c, R - hair * 1.8, R - hair * 1.8);
    painter.restore();
}

void paintBallPresence(QPainter& painter, const QPointF& centre, qreal radius, qreal energy, bool dark)
{
    QPainterPath disc;
    disc.addEllipse(centre, radius, radius);
    const qreal boost = 1.0 + 0.5 * qBound<qreal>(0, energy, 1);
    const int aura = qMin(255, qRound((dark ? 120 : 100) * boost));
    // An even blue aura, warmed and cooled by faint touches of the other petals.
    paintSoftShadow(painter, disc, 13, QPointF(0, 1), QColor(0x3D, 0x74, 0xFF, aura));
    paintSoftShadow(painter, disc, 12, QPointF(5, -3), QColor(0xFF, 0x86, 0x33, aura / 3));
    paintSoftShadow(painter, disc, 12, QPointF(-4, 4), QColor(0x0F, 0xB3, 0x83, aura / 3));
    // Contact shadow: the ball floats just above the page.
    QPainterPath footprint;
    footprint.addEllipse(centre, radius * 0.88, radius * 0.88);
    paintSoftShadow(painter, footprint, 9, QPointF(0, radius * 0.28), QColor(0, 0, 0, dark ? 150 : 105));
}

void paintActivityArc(QPainter& painter, const QPointF& centre, qreal radius, qreal t, qreal strength, bool dark)
{
    if (strength <= 0.01) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = dark ? QColor(0x5E, 0x9B, 0xFF) : QColor(0x2F, 0x6B, 0xFF);
    // The head sweeps clockwise; the tail fades out behind it.
    QConicalGradient sweep(centre, -t * 300.0);
    sweep.setColorAt(0.00, accent);
    sweep.setColorAt(0.70, withAlpha(accent, 0.0));
    sweep.setColorAt(1.00, withAlpha(accent, 0.0));
    painter.setBrush(Qt::NoBrush);
    painter.setOpacity(strength * 0.22);
    painter.setPen(QPen(QBrush(sweep), 6.0));
    painter.drawEllipse(centre, radius, radius);
    painter.setOpacity(strength);
    painter.setPen(QPen(QBrush(sweep), 2.0));
    painter.drawEllipse(centre, radius, radius);
    painter.restore();
}

QIcon assistantIcon(int size, bool dark)
{
    QIcon icon;
    for (const qreal ratio : {1.0, 1.5, 2.0}) {
        QPixmap image(qCeil(size * ratio), qCeil(size * ratio));
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paintBall(painter, QRectF(0.5, 0.5, size - 1.0, size - 1.0), 1.2, 0.0, dark);
        painter.end();
        icon.addPixmap(image);
    }
    return icon;
}

bool prefersReducedMotion()
{
    if (qEnvironmentVariableIntValue("OPEN_OFFICE_REDUCED_MOTION") == 1) return true;
#ifdef Q_OS_WIN
    BOOL animations = TRUE;
    if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) return !animations;
#endif
    return false;
}

QPainterPath continuousRoundedRect(const QRectF& rect, qreal radius, qreal smoothing)
{
    QPainterPath path;
    const qreal budget = qMin(rect.width(), rect.height()) / 2.0;
    radius = qBound<qreal>(0, radius, budget);
    if (radius <= 0.01) {
        path.addRect(rect);
        return path;
    }
    // Corner length along each edge; reduce smoothing rather than the radius
    // when the side is too short to hold both.
    qreal s = qBound<qreal>(0, smoothing, 1);
    qreal p = (1 + s) * radius;
    if (p > budget) {
        s = qMax<qreal>(0, budget / radius - 1);
        p = qMin(p, budget);
    }
    const auto rad = [](qreal degrees) { return qDegreesToRadians(degrees); };
    const qreal arcMeasure = 90 * (1 - s);
    const qreal arcSection = qSin(rad(arcMeasure / 2)) * radius * M_SQRT2;
    const qreal alpha = (90 - arcMeasure) / 2;
    const qreal p3ToP4 = radius * qTan(rad(alpha / 2));
    const qreal beta = 45 * s;
    const qreal c = p3ToP4 * qCos(rad(beta));
    const qreal d = c * qTan(rad(beta));
    const qreal b = (p - arcSection - c - d) / 3;
    const qreal a = 2 * b;

    struct Corner { QPointF vertex, in, out; };
    const Corner corners[] = {
        {rect.topRight(), {1, 0}, {0, 1}},
        {rect.bottomRight(), {0, 1}, {-1, 0}},
        {rect.bottomLeft(), {-1, 0}, {0, -1}},
        {rect.topLeft(), {0, -1}, {1, 0}},
    };
    path.moveTo(rect.topLeft() + QPointF(p, 0));
    for (const Corner& corner : corners) {
        const QPointF start = corner.vertex - corner.in * p;
        const auto at = [&](qreal along, qreal across) { return start + corner.in * along + corner.out * across; };
        path.lineTo(start);
        path.cubicTo(at(a, 0), at(a + b, 0), at(a + b + c, d));
        const QPointF centre = corner.vertex - corner.in * radius + corner.out * radius;
        const QPointF from = at(a + b + c, d);
        const qreal startAngle = qRadiansToDegrees(qAtan2(-(from.y() - centre.y()), from.x() - centre.x()));
        path.arcTo(QRectF(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius), startAngle, -arcMeasure);
        const QPointF arcEnd = from + (corner.in + corner.out) * arcSection;
        const auto after = [&](qreal along, qreal across) { return arcEnd + corner.in * along + corner.out * across; };
        path.cubicTo(after(d, c), after(d, b + c), after(d, a + b + c));
    }
    path.closeSubpath();
    return path;
}

namespace {

void blurLine(QRgb* data, int stride, int count, int radius, QVector<QRgb>& copy)
{
    for (int i = 0; i < count; ++i) copy[i] = data[i * stride];
    const int window = 2 * radius + 1;
    int a = 0, r = 0, g = 0, b = 0;
    const auto add = [&](QRgb value, int sign) {
        a += sign * qAlpha(value);
        r += sign * qRed(value);
        g += sign * qGreen(value);
        b += sign * qBlue(value);
    };
    for (int i = 0; i <= radius && i < count; ++i) add(copy[i], 1);
    for (int i = 0; i < count; ++i) {
        data[i * stride] = qRgba(r / window, g / window, b / window, a / window);
        if (i - radius >= 0) add(copy[i - radius], -1);
        if (i + radius + 1 < count) add(copy[i + radius + 1], 1);
    }
}

// Three box passes per axis: a close Gaussian approximation. The image is
// padded with transparency, so treating outside pixels as empty is exact.
void boxBlur(QImage& image, int radius)
{
    if (radius < 1) return;
    const int width = image.width(), height = image.height();
    QVector<QRgb> copy(qMax(width, height));
    const int stride = image.bytesPerLine() / 4;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < height; ++y)
            blurLine(reinterpret_cast<QRgb*>(image.scanLine(y)), 1, width, radius, copy);
        auto* pixels = reinterpret_cast<QRgb*>(image.bits());
        for (int x = 0; x < width; ++x) blurLine(pixels + x, stride, height, radius, copy);
    }
}

} // namespace

void paintSoftShadow(QPainter& painter, const QPainterPath& shape, qreal blur, const QPointF& offset,
                     const QColor& color)
{
    if (shape.isEmpty() || color.alpha() == 0) return;
    const qreal ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    const QRectF bounds = shape.boundingRect().adjusted(-2 * blur, -2 * blur, 2 * blur, 2 * blur);
    static QCache<QString, QImage> cache(24);
    const QString key = QStringLiteral("%1x%2/%3/%4/%5/%6").arg(qRound(bounds.width() * 4)).arg(qRound(bounds.height() * 4))
        .arg(shape.elementCount()).arg(blur).arg(color.rgba()).arg(ratio);
    QImage* image = cache.object(key);
    if (!image) {
        image = new QImage(QSize(qCeil(bounds.width() * ratio), qCeil(bounds.height() * ratio)),
                           QImage::Format_ARGB32_Premultiplied);
        image->fill(Qt::transparent);
        QPainter mask(image);
        mask.setRenderHint(QPainter::Antialiasing);
        mask.scale(ratio, ratio);
        mask.translate(-bounds.topLeft());
        mask.fillPath(shape, color);
        mask.end();
        boxBlur(*image, qMax(1, qRound(blur * ratio / 2.0)));
        image->setDevicePixelRatio(ratio);
        cache.insert(key, image);
    }
    painter.drawImage(bounds.topLeft() + offset, *image);
}

qreal springEase(qreal progress)
{
    // Critically-underdamped spring settling at 1 (overshoot about 8%).
    const qreal p = qBound<qreal>(0, progress, 1);
    return 1.0 - qExp(-6.5 * p) * qCos(8.0 * p);
}

} // namespace art

} // namespace openoffice
