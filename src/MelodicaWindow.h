#pragma once

#include "AudioEngine.h"
#include "HingeSensor.h"

#include <QHash>
#include <QElapsedTimer>
#include <QLabel>
#include <QSet>
#include <QSlider>
#include <QWidget>

class MelodicaWindow : public QWidget {
    Q_OBJECT

public:
    explicit MelodicaWindow(AudioEngine* audioEngine, HingeSensor* sensor, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAngleChanged(float angleDegrees);
    void onSensorAvailableChanged(bool available);
    void onFallbackSliderChanged(int value);

private:
    struct PianoKeyRect {
        int note;
        QRectF rect;
        bool black;
    };

    QRect keyboardArea() const;
    void layoutControls();
    void updateStatusText();
    void setPressureFromAngle(float angleDegrees);
    QList<PianoKeyRect> buildVisibleKeys(const QRect& area) const;

    AudioEngine* m_audioEngine;
    HingeSensor* m_sensor;

    QLabel* m_titleLabel;
    QLabel* m_infoLabel;
    QLabel* m_statusLabel;
    QSlider* m_fallbackSlider;

    QHash<int, int> m_activeKeyToNote;
    QSet<int> m_activeNotes;

    float m_currentAngle;
    float m_currentPressure;
    float m_sensorPressure;
    float m_manualPressure;
    float m_prevAngle;
    float m_velocityDegPerSec;
    float m_velocityFullScale;
    bool m_hasPrevAngle;
    bool m_sensorAvailable;
    QElapsedTimer m_angleTimer;

    int m_baseOctave;
};
