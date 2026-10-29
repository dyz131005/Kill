#include "widget_effects.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QPointer>
#include <QEasingCurve>

SlideFadeEffect::SlideFadeEffect(QObject *parent)
    : QGraphicsEffect(parent)
{
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
    // The painter is in device coordinates here; draw the source pixmap at its
    // device offset plus our animated slide offset.
    const qreal dpr = pixmap.devicePixelRatio() > 0 ? pixmap.devicePixelRatio() : 1.0;
    painter->drawPixmap(QPointF(offset.x() + m_offsetX * dpr, offset.y() + m_offsetY * dpr), pixmap);
    painter->restore();
}

namespace WidgetAnim {

void slideIn(QWidget *widget, bool fromRight, int durationMs)
{
    if (!widget)
        return;

    auto *effect = new SlideFadeEffect(widget);
    widget->setGraphicsEffect(effect);

    const int width = qMax(40, widget->width());
    const qreal startX = fromRight ? qreal(width) * 0.35 : -qreal(width) * 0.35;

    effect->setOffsetX(startX);
    effect->setOpacity(0.0);

    auto *animX = new QPropertyAnimation(effect, "offsetX", widget);
    animX->setDuration(durationMs);
    animX->setStartValue(startX);
    animX->setEndValue(0.0);
    animX->setEasingCurve(QEasingCurve::OutCubic);

    auto *animO = new QPropertyAnimation(effect, "opacity", widget);
    animO->setDuration(durationMs);
    animO->setStartValue(0.0);
    animO->setEndValue(1.0);
    animO->setEasingCurve(QEasingCurve::OutCubic);

    QPointer<QWidget> guard(widget);
    QObject::connect(animX, &QPropertyAnimation::finished, widget, [guard]() {
        if (guard)
            guard->setGraphicsEffect(nullptr);
    });

    animX->start(QAbstractAnimation::DeleteWhenStopped);
    animO->start(QAbstractAnimation::DeleteWhenStopped);
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
