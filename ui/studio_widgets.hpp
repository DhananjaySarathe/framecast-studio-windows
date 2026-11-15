#pragma once
#include "studio_model.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <cmath>

namespace framecast::ui {
inline const QColor accent("#7D6BFF"), foreground("#EEF0F6"), muted("#A8AFBF"), hairline("#232733");
inline QIcon icon(const QString &name, QColor colour = muted) {
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    p.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (name == "camera") {
        p.drawRoundedRect(QRectF(2, 5, 11, 10), 2, 2);
        p.drawLine(13, 8, 18, 5);
        p.drawLine(18, 5, 18, 15);
        p.drawLine(18, 15, 13, 12);
    } else if (name == "screen") {
        p.drawRoundedRect(QRectF(2, 3, 16, 11), 2, 2);
        p.drawLine(10, 14, 10, 18);
        p.drawLine(6, 18, 14, 18);
    } else if (name == "mic") {
        p.drawRoundedRect(QRectF(7, 2, 6, 11), 3, 3);
        p.drawArc(QRectF(4, 6, 12, 10), 180 * 16, 180 * 16);
        p.drawLine(10, 16, 10, 19);
    } else if (name == "plus") {
        p.drawLine(10, 4, 10, 16);
        p.drawLine(4, 10, 16, 10);
    } else if (name == "broadcast") {
        p.drawEllipse(QPointF(10, 10), 1.5, 1.5);
        p.drawArc(QRectF(5, 5, 10, 10), -55 * 16, 110 * 16);
        p.drawArc(QRectF(5, 5, 10, 10), 125 * 16, 110 * 16);
        p.drawArc(QRectF(1, 1, 18, 18), -50 * 16, 100 * 16);
        p.drawArc(QRectF(1, 1, 18, 18), 130 * 16, 100 * 16);
        p.drawLine(10, 13, 10, 18);
    } else if (name == "gear") {
        p.drawEllipse(QRectF(4, 4, 12, 12));
        p.drawEllipse(QRectF(8, 8, 4, 4));
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(10, 10);
            p.rotate(i * 45);
            p.drawLine(0, -6, 0, -9);
            p.restore();
        }
    } else if (name == "chat" || name == "room") {
        p.drawRoundedRect(QRectF(3, 3, 14, 11), 2, 2);
        p.drawLine(6, 14, 6, 18);
        p.drawLine(6, 18, 10, 14);
        if (name == "room")
            p.drawLine(6, 7, 14, 7);
    } else if (name == "guests" || name == "angles") {
        p.drawEllipse(QRectF(7, 2, 6, 6));
        p.drawArc(QRectF(3, 10, 14, 12), 0, 180 * 16);
        if (name == "angles")
            p.drawRect(QRectF(1, 2, 3, 3));
    } else if (name == "mixer") {
        for (int i = 0; i < 5; ++i) {
            int h = 4 + ((i * 7) % 11);
            p.drawLine(3 + i * 3, 10 - h / 2, 3 + i * 3, 10 + h / 2);
        }
    } else if (name == "layers" || name == "overlays") {
        QPolygonF a;
        a << QPointF(10, 2) << QPointF(18, 7) << QPointF(10, 12) << QPointF(2, 7);
        p.drawPolygon(a);
        p.drawLine(2, 11, 10, 16);
        p.drawLine(10, 16, 18, 11);
        if (name == "layers") {
            p.drawLine(2, 15, 10, 19);
            p.drawLine(10, 19, 18, 15);
        }
    } else if (name == "captions") {
        p.drawRoundedRect(QRectF(1, 4, 18, 12), 2, 2);
        p.drawArc(QRectF(4, 7, 5, 6), 50 * 16, 260 * 16);
        p.drawArc(QRectF(11, 7, 5, 6), 50 * 16, 260 * 16);
    } else if (name == "rundown") {
        for (int i = 0; i < 3; ++i) {
            p.drawEllipse(QPointF(3, 5 + i * 5), .5, .5);
            p.drawLine(7, 5 + i * 5, 17, 5 + i * 5);
        }
    } else if (name == "eye") {
        QPainterPath path;
        path.moveTo(1, 10);
        path.cubicTo(5, 2, 15, 2, 19, 10);
        path.cubicTo(15, 18, 5, 18, 1, 10);
        p.drawPath(path);
        p.drawEllipse(QRectF(7, 7, 6, 6));
    } else if (name == "record") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#FF4D5E"));
        p.drawEllipse(QRectF(5, 5, 10, 10));
    } else if (name == "stop") {
        p.setBrush(QColor("#FFB3BC"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(5, 5, 10, 10), 2, 2);
    } else if (name == "horizontal")
        p.drawRoundedRect(QRectF(2, 5, 16, 10), 2, 2);
    else if (name == "vertical")
        p.drawRoundedRect(QRectF(6, 2, 8, 16), 2, 2);
    else if (name == "both") {
        p.drawRoundedRect(QRectF(1, 6, 11, 8), 1, 1);
        p.drawRoundedRect(QRectF(14, 3, 5, 14), 1, 1);
    } else if (name == "arrow") {
        p.drawLine(3, 10, 17, 10);
        p.drawLine(12, 5, 17, 10);
        p.drawLine(17, 10, 12, 15);
    } else if (name == "folder") {
        QPainterPath path;
        path.moveTo(2, 16);
        path.lineTo(2, 4);
        path.lineTo(8, 4);
        path.lineTo(10, 6);
        path.lineTo(18, 6);
        path.lineTo(18, 16);
        path.closeSubpath();
        p.drawPath(path);
    }
    return QIcon(pixmap);
}
class CanvasView final : public QWidget {
  public:
    explicit CanvasView(bool vertical, QWidget *parent = nullptr) : QWidget(parent), vertical_(vertical) {
        setMinimumSize(120, 120);
        setFocusPolicy(Qt::StrongFocus);
    }
    QImage image;
    bool selected = false, preview = false, onAir = false;
    std::function<void()> clicked;

  protected:
    void mousePressEvent(QMouseEvent *) override {
        if (clicked)
            clicked();
        setFocus();
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
            if (clicked)
                clicked();
        } else
            QWidget::keyPressEvent(e);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        double aspect = vertical_ ? 9. / 16 : 16. / 9;
        QSizeF size(width() - 8, (width() - 8) / aspect);
        if (size.height() > height() - 8)
            size = QSizeF((height() - 8) * aspect, height() - 8);
        QRectF box((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
        p.setPen(QPen(selected  ? accent
                      : preview ? QColor("#3DD68C")
                      : onAir   ? QColor("#FF4D5E")
                                : hairline,
                      selected || preview || onAir ? 2 : 1));
        p.setBrush(QColor("#14171D"));
        p.drawRoundedRect(box, 8, 8);
        QPainterPath clip;
        clip.addRoundedRect(box.adjusted(2, 2, -2, -2), 6, 6);
        p.setClipPath(clip);
        if (!image.isNull())
            p.drawImage(box.adjusted(2, 2, -2, -2), image);
        else {
            p.setPen(muted);
            p.drawText(box.adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                       "Add sources to see your show");
        }
        p.setClipping(false);
        QRectF badge(box.x() + 9, box.y() + 9, preview ? 94 : 46, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(preview ? QColor("#226348") : selected ? accent : QColor("#262C3A"));
        p.drawRoundedRect(badge, 4, 4);
        p.setPen(foreground);
        auto f = p.font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        p.drawText(badge, Qt::AlignCenter,
                   (preview ? "PREVIEW · " : "") + QString(vertical_ ? "9:16" : "16:9"));
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#B8ADFF"), 1, Qt::DashLine));
            p.drawRoundedRect(box.adjusted(-2, -2, 2, 2), 10, 10);
        }
    }

  private:
    bool vertical_;
};
class SceneButton final : public QPushButton {
  public:
    explicit SceneButton(const QString &name, QWidget *parent = nullptr) : QPushButton(parent), name(name) {
        setFixedHeight(154);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(name);
    }
    QString name;
    QImage thumbnail;
    bool onAir = false, preview = false;
    int shortcut = 0;
    std::function<void()> doubleClicked;

  protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        QPushButton::mouseDoubleClickEvent(event);
        if (doubleClicked)
            doubleClicked();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto surface = rect().adjusted(0, 0, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(onAir ? QColor("#191927") : underMouse() ? QColor("#161A22") : QColor("#101318"));
        p.drawRoundedRect(surface, 13, 13);
        QRectF thumb(6, 6, width() - 12, 116);
        p.setPen(QPen(onAir ? accent : preview ? QColor("#3DD68C") : hairline, onAir || preview ? 2 : 1));
        p.setBrush(QColor("#07080B"));
        p.drawRoundedRect(thumb, 10, 10);
        if (!thumbnail.isNull()) {
            p.save();
            QPainterPath path;
            path.addRoundedRect(thumb.adjusted(2, 2, -2, -2), 8, 8);
            p.setClipPath(path);
            auto bounds = thumb.adjusted(2, 2, -2, -2);
            auto size = thumbnail.size().scaled(bounds.size().toSize(), Qt::KeepAspectRatio);
            p.drawImage(QRectF(bounds.center().x() - size.width() / 2.,
                               bounds.center().y() - size.height() / 2., size.width(), size.height()),
                        thumbnail);
            p.restore();
        }
        if (onAir || preview) {
            QRectF tag(12, 12, preview && !onAir ? 66 : 57, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(onAir ? accent : QColor("#226348"));
            p.drawRoundedRect(tag, 8, 8);
            p.setPen(foreground);
            auto f = p.font();
            f.setPixelSize(9);
            f.setBold(true);
            p.setFont(f);
            p.drawText(tag, Qt::AlignCenter, onAir ? "ON AIR" : "PREVIEW");
        }
        auto f = p.font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.setPen(onAir ? foreground : muted);
        p.drawText(QRect(8, 126, width() - 48, 22), Qt::AlignVCenter,
                   p.fontMetrics().elidedText(name, Qt::ElideRight, width() - 48));
        if (shortcut) {
            f.setPixelSize(9);
            f.setBold(false);
            p.setFont(f);
            p.setPen(QColor("#7F8798"));
            p.drawText(QRect(width() - 48, 126, 40, 22), Qt::AlignRight | Qt::AlignVCenter,
                       "Ctrl+" + QString::number(shortcut));
        }
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#B8ADFF"), 1, Qt::DashLine));
            p.drawRoundedRect(surface.adjusted(1, 1, -1, -1), 12, 12);
        }
    }
};
class Meter final : public QWidget {
  public:
    explicit Meter(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(6);
        setAccessibleName("Audio level");
    }
    void setLevel(float level) {
        level_ = level;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#262C3A"));
        float normalized = std::clamp((20 * std::log10(std::max(level_, .001f)) + 60) / 60, 0.f, 1.f);
        p.fillRect(QRectF(0, 0, width() * normalized, height()),
                   level_ > .9f ? QColor("#FF4D5E") : QColor("#3DD68C"));
    }

  private:
    float level_ = 0;
};
} // namespace framecast::ui
