#pragma once

#include <QGraphicsEffect>
#include <QObject>
#include <QWidget>
#include <functional>

class QPainter;

// Paints the target widget with horizontal/vertical offsets and opacity.
// Used to build smooth slide-in / slide-out transitions that work
// regardless of the widget being managed by a layout (since it only
// affects painting, not geometry).
class SlideFadeEffect : public QGraphicsEffect
{
    Q_OBJECT
    Q_PROPERTY(qreal offsetX READ offsetX WRITE setOffsetX)
    Q_PROPERTY(qreal offsetY READ offsetY WRITE setOffsetY)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit SlideFadeEffect(QObject *parent = nullptr);

    qreal offsetX() const { return m_offsetX; }
    void setOffsetX(qreal x) { m_offsetX = x; update(); }

    qreal offsetY() const { return m_offsetY; }
    void setOffsetY(qreal y) { m_offsetY = y; update(); }

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o) { m_opacity = o; update(); }

protected:
    void draw(QPainter *painter) override;
    QRectF boundingRectFor(const QRectF &sourceRect) const override;

private:
    qreal m_offsetX = 0.0;
    qreal m_offsetY = 0.0;
    qreal m_opacity = 1.0;
};

namespace WidgetAnim {
// Slide the widget in horizontally with a fade.
// `fromRight` true  -> starts offset to the right, slides left into place.
// `fromRight` false -> starts offset to the left,  slides right into place.
void slideIn(QWidget *widget, bool fromRight, int durationMs = 220);

// Vertical slide-in. `fromBottom` true -> starts below and slides up; false ->
// starts above and slides down.
void slideInVertical(QWidget *widget, bool fromBottom, int durationMs = 220);

// Slide the widget upward off-screen with a fade, then invoke `onDone`.
void slideUpOut(QWidget *widget, int durationMs = 260, const std::function<void()> &onDone = {});
}
