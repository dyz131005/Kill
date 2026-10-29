#include "widget_effects.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QPointer>
#include <QEasingCurve>
#include <QLabel>
#include <QGraphicsOpacityEffect>

SlideFadeEffect::SlideFadeEffect(QObject *parent)
    : QGraphicsEffect(parent)
{
}

QRectF SlideFadeEffect::boundingRectFor(const QRectF &r) const
{
    // Keep the bounding rect equal to the source rect. drawSource() then paints
    // the content at its exact natural position (no padding offset), and the
    // slide is applied purely via a logical translate in draw(). This avoids any
    // interaction between bounding-rect padding and the translate that could
    // shift the content.
    return r.translated(m_offsetX, m_offsetY);
}

void SlideFadeEffect::draw(QPainter *painter)
{
    QPoint offset;
    const QPixmap pixmap = sourcePixmap(Qt::DeviceCoordinates, &offset,
                                        QGraphicsEffect::PadToEffectiveBoundingRect);
    if (pixmap.isNull())
        return;

    painter->save();
    painter->setOpacity(qBound(0.0, m_opacity, 1.0));
    // offset already accounts for the translated bounding rect, so drawing at
    // exactly offset places the content at natural position + slide offset.
    painter->drawPixmap(offset, pixmap);
    painter->restore();
}

namespace WidgetAnim {

void slideIn(QWidget *widget, bool fromRight, int durationMs)
{
    if (!widget || !widget->parentWidget())
        return;

    QWidget *parent = widget->parentWidget();
    const QRect geo = widget->geometry();
    if (geo.width() <= 0 || geo.height() <= 0)
        return;

    // Snapshot the page content into a floating label over the parent.
    QPixmap snap = widget->grab();
    auto *overlay = new QLabel(parent);
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    overlay->setPixmap(snap);
    overlay->setFixedSize(geo.size());
    overlay->raise();
    overlay->show();

    // The real page stays visible underneath; the overlay starts offset and
    // slides into the exact page position, so the page appears to slide in.
    const int dist = qMax(60, geo.width() / 3);
    const QPoint startPos = geo.topLeft() + QPoint(fromRight ? dist : -dist, 0);
    overlay->move(startPos);

    auto *eff = new QGraphicsOpacityEffect(overlay);
    overlay->setGraphicsEffect(eff);
    eff->setOpacity(0.0);

    auto *animPos = new QPropertyAnimation(overlay, "pos", overlay);
    animPos->setDuration(durationMs);
    animPos->setStartValue(startPos);
    animPos->setEndValue(geo.topLeft());
    animPos->setEasingCurve(QEasingCurve::OutCubic);

    auto *animOp = new QPropertyAnimation(eff, "opacity", overlay);
    animOp->setDuration(durationMs);
    animOp->setStartValue(0.0);
    animOp->setEndValue(1.0);
    animOp->setEasingCurve(QEasingCurve::OutCubic);

    QPointer<QLabel> guard(overlay);
    QObject::connect(animPos, &QPropertyAnimation::finished, overlay, [guard]() {
        if (guard)
            guard->deleteLater();
    });

    animPos->start(QAbstractAnimation::DeleteWhenStopped);
    animOp->start(QAbstractAnimation::DeleteWhenStopped);
}

void slideInVertical(QWidget *widget, bool fromBottom, int durationMs)
{
    if (!widget || !widget->parentWidget())
        return;

    QWidget *parent = widget->parentWidget();
    const QRect geo = widget->geometry();
    if (geo.width() <= 0 || geo.height() <= 0)
        return;

    QPixmap snap = widget->grab();
    auto *overlay = new QLabel(parent);
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    overlay->setPixmap(snap);
    overlay->setFixedSize(geo.size());
    overlay->raise();
    overlay->show();

    const int dist = qMax(60, geo.height() / 3);
    const QPoint startPos = geo.topLeft() + QPoint(0, fromBottom ? dist : -dist);
    overlay->move(startPos);

    auto *eff = new QGraphicsOpacityEffect(overlay);
    overlay->setGraphicsEffect(eff);
    eff->setOpacity(0.0);

    auto *animPos = new QPropertyAnimation(overlay, "pos", overlay);
    animPos->setDuration(durationMs);
    animPos->setStartValue(startPos);
    animPos->setEndValue(geo.topLeft());
    animPos->setEasingCurve(QEasingCurve::OutCubic);

    auto *animOp = new QPropertyAnimation(eff, "opacity", overlay);
    animOp->setDuration(durationMs);
    animOp->setStartValue(0.0);
    animOp->setEndValue(1.0);
    animOp->setEasingCurve(QEasingCurve::OutCubic);

    QPointer<QLabel> guard(overlay);
    QObject::connect(animPos, &QPropertyAnimation::finished, overlay, [guard]() {
        if (guard)
            guard->deleteLater();
    });

    animPos->start(QAbstractAnimation::DeleteWhenStopped);
    animOp->start(QAbstractAnimation::DeleteWhenStopped);
}
void slideUpOut(QWidget *widget, int durationMs, const std::function<void()> &onDone)
{
    if (!widget) {
        if (onDone) onDone();
        return;
    }

    auto *effect = new SlideFadeEffect(widget);
    widget->setGraphicsEffect(effect);
    effect->setOffsetY(0.0);
    effect->setOpacity(1.0);

    auto *animY = new QPropertyAnimation(effect, "offsetY", widget);
    animY->setDuration(durationMs);
    animY->setStartValue(0.0);
    animY->setEndValue(-qreal(qMax(60, widget->height())));
    animY->setEasingCurve(QEasingCurve::InCubic);

    auto *animO = new QPropertyAnimation(effect, "opacity", widget);
    animO->setDuration(durationMs);
    animO->setStartValue(1.0);
    animO->setEndValue(0.0);
    animO->setEasingCurve(QEasingCurve::InCubic);

    QPointer<QWidget> guard(widget);
    QObject::connect(animY, &QPropertyAnimation::finished, widget, [guard, onDone]() {
        if (guard)
            guard->setGraphicsEffect(nullptr);
        if (onDone)
            onDone();
    });

    animY->start(QAbstractAnimation::DeleteWhenStopped);
    animO->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace WidgetAnim
