#include "MelodicaWindow.h"

#include "KeyMap.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTimer>

#include <algorithm>

MelodicaWindow::MelodicaWindow(AudioEngine* audioEngine, HingeSensor* sensor, QWidget* parent)
    : QWidget(parent),
      m_audioEngine(audioEngine),
      m_sensor(sensor),
      m_titleLabel(new QLabel(QStringLiteral("Qt Melodica Simulator"), this)),
      m_infoLabel(new QLabel(this)),
      m_statusLabel(new QLabel(this)),
      m_fallbackSlider(new QSlider(Qt::Vertical, this)),
      m_currentAngle(110.0f),
      m_currentPressure(0.0f),
      m_sensorPressure(0.0f),
      m_manualPressure(0.0f),
    m_prevAngle(0.0f),
    m_velocityDegPerSec(0.0f),
    m_velocityFullScale(50.0f),
    m_hasPrevAngle(false),
      m_sensorAvailable(false),
    m_baseOctave(4) {
    setWindowTitle(QStringLiteral("Qt Melodica"));
    setMinimumSize(960, 420);
    setFocusPolicy(Qt::StrongFocus);

    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 18px;"));
    m_infoLabel->setStyleSheet(QStringLiteral("color: #444;"));
    m_statusLabel->setStyleSheet(QStringLiteral("padding: 4px 8px; background: #2b2b2b; color: #e8e8e8; border: 1px solid #1a1a1a; font-family: monospace;"));
    m_statusLabel->setWordWrap(true);

    m_fallbackSlider->setRange(0, 100);
    m_fallbackSlider->setValue(0);
    m_fallbackSlider->setToolTip(QStringLiteral("Manual blow pressure (shown when sensor unavailable)"));
    m_fallbackSlider->hide();

    connect(m_fallbackSlider, &QSlider::valueChanged, this, &MelodicaWindow::onFallbackSliderChanged);
    connect(m_sensor, &HingeSensor::angleChanged, this, &MelodicaWindow::onAngleChanged);
    connect(m_sensor, &HingeSensor::sensorAvailableChanged, this, &MelodicaWindow::onSensorAvailableChanged);

    m_infoLabel->setText(QStringLiteral("Starting hinge sensor..."));
    updateStatusText();
    layoutControls();

    // Ensure key events are captured without requiring an initial click.
    QTimer::singleShot(0, this, [this]() { setFocus(); });

    auto* meterTimer = new QTimer(this);
    connect(meterTimer, &QTimer::timeout, this, [this]() { updateStatusText(); });
    meterTimer->start(120);
}

void MelodicaWindow::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect area = keyboardArea();
    p.fillRect(area, QColor(247, 247, 247));

    const QList<PianoKeyRect> keys = buildVisibleKeys(area);

    for (const PianoKeyRect& key : keys) {
        if (key.black) {
            continue;
        }
        const bool active = m_activeNotes.contains(key.note);
        p.setPen(QPen(QColor(70, 70, 70), 1));
        p.setBrush(active ? QColor(153, 222, 255) : QColor(255, 255, 255));
        p.drawRoundedRect(key.rect, 2.0, 2.0);
    }

    for (const PianoKeyRect& key : keys) {
        if (!key.black) {
            continue;
        }
        const bool active = m_activeNotes.contains(key.note);
        p.setPen(Qt::NoPen);
        p.setBrush(active ? QColor(42, 145, 210) : QColor(40, 40, 40));
        p.drawRoundedRect(key.rect, 2.0, 2.0);
    }

    // Blow pressure indicator.
    const QRect barRect(width() - 54, 84, 24, height() - 170);
    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(QColor(240, 240, 240));
    p.drawRect(barRect);

    const int fillH = static_cast<int>(barRect.height() * m_currentPressure);
    const QRect fillRect(barRect.x() + 2, barRect.bottom() - fillH + 1, barRect.width() - 3, std::max(0, fillH - 1));
    p.setBrush(QColor(53, 173, 110));
    p.setPen(Qt::NoPen);
    p.drawRect(fillRect);

    p.setPen(QColor(30, 30, 30));
    p.drawText(barRect.adjusted(-24, -24, 40, -barRect.height() - 6), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Blow\n%1%").arg(static_cast<int>(m_currentPressure * 100.0f)));
}

void MelodicaWindow::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        event->ignore();
        return;
    }

    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (KeyMap::isOctaveDownKey(key)) {
        m_baseOctave = std::max(2, m_baseOctave - 1);
        updateStatusText();
        update();
        return;
    }
    if (KeyMap::isOctaveUpKey(key)) {
        m_baseOctave = std::min(6, m_baseOctave + 1);
        updateStatusText();
        update();
        return;
    }

    if (m_activeKeyToNote.contains(static_cast<int>(key))) {
        return;
    }

    const int octaveOffset = m_baseOctave - 4;
    const int note = KeyMap::keyToNote(key, octaveOffset);
    if (note < 0) {
        event->ignore();
        return;
    }

    m_activeKeyToNote.insert(static_cast<int>(key), note);
    m_activeNotes.insert(note);
    m_audioEngine->noteOn(note);
    updateStatusText();
    update();
}

void MelodicaWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        event->ignore();
        return;
    }

    const int key = event->key();
    auto it = m_activeKeyToNote.find(key);
    if (it == m_activeKeyToNote.end()) {
        event->ignore();
        return;
    }

    const int note = it.value();
    m_activeKeyToNote.erase(it);
    m_activeNotes.remove(note);

    m_audioEngine->noteOff(note);
    updateStatusText();
    update();
}

void MelodicaWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutControls();
}

void MelodicaWindow::onAngleChanged(float angleDegrees) {
    m_currentAngle = angleDegrees;
    setPressureFromAngle(angleDegrees);
}

void MelodicaWindow::onSensorAvailableChanged(bool available) {
    m_sensorAvailable = available;
    m_fallbackSlider->setVisible(!available);

    if (available) {
        m_sensorPressure = 0.0f;
        m_currentPressure = 0.0f;
        m_velocityDegPerSec = 0.0f;
        m_hasPrevAngle = false;
        m_angleTimer.invalidate();
        m_audioEngine->setBlowPressure(0.0f);
        m_infoLabel->setText(QStringLiteral("Hinge sensor active — move lid speed to control blow pressure"));
    } else {
        m_sensorPressure = 0.0f;
        m_velocityDegPerSec = 0.0f;
        m_hasPrevAngle = false;
        m_angleTimer.invalidate();
        m_manualPressure = static_cast<float>(m_fallbackSlider->value()) / 100.0f;
        m_currentPressure = m_manualPressure;
        m_audioEngine->setBlowPressure(m_currentPressure);
        m_infoLabel->setText(QStringLiteral("Sensor unavailable — use the slider to control blow pressure"));
    }

    layoutControls();
    updateStatusText();
}

void MelodicaWindow::onFallbackSliderChanged(int value) {
    m_manualPressure = static_cast<float>(value) / 100.0f;
    if (!m_sensorAvailable) {
        m_currentPressure = m_manualPressure;
        m_audioEngine->setBlowPressure(m_currentPressure);
        updateStatusText();
        update();
    }
}

QRect MelodicaWindow::keyboardArea() const {
    return QRect(18, 82, width() - 110, height() - 148);
}

void MelodicaWindow::layoutControls() {
    m_titleLabel->setGeometry(16, 10, width() - 32, 28);
    m_infoLabel->setGeometry(16, 38, width() - 32, 24);
    m_statusLabel->setGeometry(16, height() - 74, width() - 32, 54);

    m_fallbackSlider->setGeometry(width() - 88, 108, 30, height() - 210);
}

void MelodicaWindow::updateStatusText() {
    QStringList notes;
    for (int note : std::as_const(m_activeNotes)) {
        notes << KeyMap::midiToNoteName(note);
    }
    std::sort(notes.begin(), notes.end());

    const QString pressed = notes.isEmpty() ? QStringLiteral("(none)") : notes.join(QStringLiteral(", "));
    const QString status = QStringLiteral("Base octave: %1 | Angle: %2 deg | Velocity: %3 deg/s | Pressure: %4% | Pressed: %5")
                               .arg(m_baseOctave)
                               .arg(QString::number(m_currentAngle, 'f', 1))
                               .arg(QString::number(m_velocityDegPerSec, 'f', 1))
                               .arg(static_cast<int>(m_currentPressure * 100.0f))
                               .arg(pressed);

    const QString audioDebug = QStringLiteral("Audio: voices=%1 peak=%2 sink=%3 | format=%4")
                                   .arg(m_audioEngine->activeVoiceCount())
                                   .arg(QString::number(m_audioEngine->debugPeakLevel(), 'f', 3))
                                   .arg(m_audioEngine->debugSinkState())
                                   .arg(m_audioEngine->debugOutputFormat());

    m_statusLabel->setText(status + QStringLiteral("\n") + audioDebug);
}

void MelodicaWindow::setPressureFromAngle(float angleDegrees) {
    if (!m_sensorAvailable) {
        return;
    }

    if (!m_hasPrevAngle) {
        m_prevAngle = angleDegrees;
        m_hasPrevAngle = true;
        m_velocityDegPerSec = 0.0f;
        m_sensorPressure = 0.0f;
        m_currentPressure = 0.0f;
        m_audioEngine->setBlowPressure(0.0f);
        if (!m_angleTimer.isValid()) {
            m_angleTimer.start();
        } else {
            m_angleTimer.restart();
        }
        updateStatusText();
        update();
        return;
    }

    if (!m_angleTimer.isValid()) {
        m_angleTimer.start();
        m_prevAngle = angleDegrees;
        return;
    }

    const qint64 elapsedMs = m_angleTimer.restart();
    if (elapsedMs <= 0) {
        m_prevAngle = angleDegrees;
        return;
    }

    const float dt = static_cast<float>(elapsedMs) / 1000.0f;
    const float delta = angleDegrees - m_prevAngle;
    float speed = std::abs(delta) / dt;
    if (speed < 1.0f) {
        speed = 0.0f;
    }
    m_velocityDegPerSec = speed;

    const float targetPressure = std::clamp(speed / m_velocityFullScale, 0.0f, 1.0f);
    if (targetPressure >= m_sensorPressure) {
        m_sensorPressure = (0.35f * m_sensorPressure) + (0.65f * targetPressure);
    } else {
        m_sensorPressure = (0.10f * m_sensorPressure) + (0.90f * targetPressure);
    }
    m_prevAngle = angleDegrees;
    m_currentPressure = m_sensorPressure;
    m_audioEngine->setBlowPressure(m_currentPressure);

    updateStatusText();
    update();
}

QList<MelodicaWindow::PianoKeyRect> MelodicaWindow::buildVisibleKeys(const QRect& area) const {
    QList<PianoKeyRect> out;

    const int whiteNotes[] = {
        60, 62, 64, 65, 67, 69, 71,
        72, 74, 76, 77, 79, 81, 83,
        84
    };

    const int blackNotes[] = {
        61, 63, 66, 68, 70,
        73, 75, 78, 80, 82
    };

    const int octaveOffset = (m_baseOctave - 4) * 12;

    const int whiteCount = static_cast<int>(std::size(whiteNotes));
    const qreal whiteW = static_cast<qreal>(area.width()) / whiteCount;
    const qreal blackW = whiteW * 0.62;
    const qreal blackH = area.height() * 0.60;

    for (int i = 0; i < whiteCount; ++i) {
        QRectF r(area.x() + i * whiteW, area.y(), whiteW, area.height());
        out.append({whiteNotes[i] + octaveOffset, r, false});
    }

    const int blackSlots[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    for (int i = 0; i < static_cast<int>(std::size(blackNotes)); ++i) {
        const qreal x = area.x() + (blackSlots[i] + 1) * whiteW - (blackW / 2.0);
        QRectF r(x, area.y(), blackW, blackH);
        out.append({blackNotes[i] + octaveOffset, r, true});
    }

    return out;
}
