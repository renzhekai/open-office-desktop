#pragma once

#include <QIcon>
#include <QPainterPath>

class QPainter;

namespace openoffice {

// Visual identity and shape helpers of the assistant, drawn entirely with
// QPainter by this project. Apple informs the craft (materials, smooth
// corners, restrained motion); the artwork itself is original.
namespace art {
// The assistant ball: a glass sphere (Fresnel rim, specular highlight, light
// gathered at the bottom) holding three glass petals - blue, mint and amber,
// for documents, spreadsheets and presentations - around a luminous core.
// t: seconds of animation time; energy: 0 idle .. 1 working.
void paintBall(QPainter& painter, const QRectF& bounds, qreal t, qreal energy, bool dark);
// Presence around the floating ball: a deeper contact shadow and a faint aura
// in the petal colours, so the ball stands out on a white page. Not used for
// toolbar-sized icons. energy 0..1 strengthens it while working.
void paintBallPresence(QPainter& painter, const QPointF& centre, qreal radius, qreal energy, bool dark);
// Working indicator: a fine arc sweeping around the ball with a fading tail.
void paintActivityArc(QPainter& painter, const QPointF& centre, qreal radius, qreal t, qreal strength, bool dark);
QIcon assistantIcon(int size, bool dark = false);
// Honors Windows "Show animations" (SPI_GETCLIENTAREAANIMATION) and the
// OPEN_OFFICE_REDUCED_MOTION=1 override; reduced motion means static frames.
bool prefersReducedMotion();
// Damped spring 0→1 with a small overshoot, for QEasingCurve::setCustomType.
qreal springEase(qreal progress);
// Rounded rectangle with continuous curvature ("smooth corners"): each corner
// eases from the straight edge into a shorter arc through two cubic curves,
// so curvature never jumps. smoothing 0 = plain circular corners; 0.6 is close
// to the iOS look. Implements the published corner-smoothing construction
// (Figma, "Desperately seeking squircles").
QPainterPath continuousRoundedRect(const QRectF& rect, qreal radius, qreal smoothing = 0.6);
// Real blurred shadow of a shape (three box-blur passes approximate a
// Gaussian). Results are cached per shape size, so steady frames are cheap.
void paintSoftShadow(QPainter& painter, const QPainterPath& shape, qreal blur, const QPointF& offset,
                     const QColor& color);
} // namespace art

} // namespace openoffice
