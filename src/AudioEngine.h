#pragma once

#include "SynthVoice.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QAudio>
#include <QIODevice>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>

#include <memory>

class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    void noteOn(int midiNote);
    void noteOff(int midiNote);
    void setBlowPressure(float pressure);

    float blowPressure() const;
    int activeVoiceCount() const;
    float debugPeakLevel() const;
    QString debugOutputFormat() const;
    QString debugSinkState() const;

private:
    class AudioGeneratorDevice : public QIODevice {
    public:
        explicit AudioGeneratorDevice(AudioEngine* engine);

        bool isSequential() const override;
        qint64 bytesAvailable() const override;
        qint64 readData(char* data, qint64 maxlen) override;
        qint64 writeData(const char* data, qint64 len) override;

    private:
        AudioEngine* m_engine;
    };

    qint64 generateSamples(char* data, qint64 maxlen);
    void ensureSinkRunning();

    mutable QMutex m_mutex;
    QAudioFormat m_format;
    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<AudioGeneratorDevice> m_generator;

    QHash<int, std::shared_ptr<SynthVoice>> m_voices;
    float m_blowPressure;
    float m_dt;
    float m_lastPeakLevel;

    static constexpr int kSampleRate = 44100;
    static constexpr int kMaxPolyphony = 10;
};
