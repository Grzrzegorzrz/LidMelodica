#include "AudioEngine.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstdint>

AudioEngine::AudioGeneratorDevice::AudioGeneratorDevice(AudioEngine* engine)
    : m_engine(engine) {
}

bool AudioEngine::AudioGeneratorDevice::isSequential() const {
    return true;
}

qint64 AudioEngine::AudioGeneratorDevice::bytesAvailable() const {
    // Always report data available so QAudioSink keeps pulling instead of going idle.
    return 65536 + QIODevice::bytesAvailable();
}

qint64 AudioEngine::AudioGeneratorDevice::readData(char* data, qint64 maxlen) {
    return m_engine->generateSamples(data, maxlen);
}

qint64 AudioEngine::AudioGeneratorDevice::writeData(const char*, qint64 len) {
    return len;
}

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent),
      m_blowPressure(0.0f),
      m_dt(1.0f / static_cast<float>(kSampleRate)),
      m_lastPeakLevel(0.0f) {
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    if (!outputDevice.isFormatSupported(m_format)) {
        m_format = outputDevice.preferredFormat();
        qWarning() << "Requested 44.1kHz mono Int16 unsupported; using preferred format"
                   << m_format.sampleRate() << "Hz" << m_format.channelCount() << "ch"
                   << m_format.sampleFormat();
    }

    if (m_format.channelCount() != 1 || m_format.sampleFormat() != QAudioFormat::Int16) {
        // Keep the generator simple by forcing mono 16-bit if possible.
        QAudioFormat candidate = m_format;
        candidate.setChannelCount(1);
        candidate.setSampleFormat(QAudioFormat::Int16);
        if (outputDevice.isFormatSupported(candidate)) {
            m_format = candidate;
        }
    }

    m_dt = 1.0f / static_cast<float>(std::max(1, m_format.sampleRate()));

    m_sink = std::make_unique<QAudioSink>(outputDevice, m_format, this);
    m_sink->setBufferSize((m_format.sampleRate() / 20) * m_format.bytesPerFrame()); // ~50 ms

    m_generator = std::make_unique<AudioGeneratorDevice>(this);
    m_generator->open(QIODevice::ReadOnly);
    m_sink->start(m_generator.get());

    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && m_sink->error() != QAudio::NoError) {
            qWarning() << "QAudioSink stopped with error:" << m_sink->error();
        }
    });
}

AudioEngine::~AudioEngine() {
    if (m_sink) {
        m_sink->stop();
    }
    if (m_generator) {
        m_generator->close();
    }
}

void AudioEngine::noteOn(int midiNote) {
    if (midiNote < 0) {
        return;
    }

    ensureSinkRunning();
    QMutexLocker locker(&m_mutex);

    auto it = m_voices.find(midiNote);
    if (it != m_voices.end()) {
        it.value()->noteOn();
        return;
    }

    if (m_voices.size() >= kMaxPolyphony) {
        // Reclaim one released voice first; otherwise remove an arbitrary oldest hash entry.
        int keyToRemove = -1;
        for (auto voiceIt = m_voices.constBegin(); voiceIt != m_voices.constEnd(); ++voiceIt) {
            if (!voiceIt.value()->isHeld()) {
                keyToRemove = voiceIt.key();
                break;
            }
        }
        if (keyToRemove == -1 && !m_voices.isEmpty()) {
            keyToRemove = m_voices.constBegin().key();
        }
        if (keyToRemove != -1) {
            m_voices.remove(keyToRemove);
        }
    }

    auto voice = std::make_shared<SynthVoice>(midiNote, static_cast<float>(m_format.sampleRate()));
    voice->noteOn();
    m_voices.insert(midiNote, voice);
}

void AudioEngine::noteOff(int midiNote) {
    QMutexLocker locker(&m_mutex);
    auto it = m_voices.find(midiNote);
    if (it != m_voices.end()) {
        it.value()->noteOff();
    }
}

void AudioEngine::setBlowPressure(float pressure) {
    QMutexLocker locker(&m_mutex);
    m_blowPressure = std::clamp(pressure, 0.0f, 1.0f);
}

float AudioEngine::blowPressure() const {
    QMutexLocker locker(&m_mutex);
    return m_blowPressure;
}

int AudioEngine::activeVoiceCount() const {
    QMutexLocker locker(&m_mutex);
    return m_voices.size();
}

float AudioEngine::debugPeakLevel() const {
    QMutexLocker locker(&m_mutex);
    return m_lastPeakLevel;
}

QString AudioEngine::debugOutputFormat() const {
    return QStringLiteral("%1 Hz, %2 ch, fmt=%3")
        .arg(m_format.sampleRate())
        .arg(m_format.channelCount())
        .arg(static_cast<int>(m_format.sampleFormat()));
}

QString AudioEngine::debugSinkState() const {
    if (!m_sink) {
        return QStringLiteral("sink=null");
    }

    const QAudio::State state = m_sink->state();
    const QAudio::Error err = m_sink->error();
    QString stateText = QStringLiteral("unknown");

    switch (state) {
    case QAudio::StoppedState:
        stateText = QStringLiteral("stopped");
        break;
    case QAudio::ActiveState:
        stateText = QStringLiteral("active");
        break;
    case QAudio::SuspendedState:
        stateText = QStringLiteral("suspended");
        break;
    case QAudio::IdleState:
        stateText = QStringLiteral("idle");
        break;
    }

    return QStringLiteral("%1 err=%2").arg(stateText).arg(static_cast<int>(err));
}

void AudioEngine::ensureSinkRunning() {
    if (!m_sink || !m_generator) {
        return;
    }

    const QAudio::State state = m_sink->state();
    if (state == QAudio::SuspendedState) {
        m_sink->resume();
        return;
    }

    if (state == QAudio::StoppedState &&
        (m_sink->error() == QAudio::NoError || m_sink->error() == QAudio::UnderrunError)) {
        m_sink->start(m_generator.get());
    }
}

qint64 AudioEngine::generateSamples(char* data, qint64 maxlen) {
    if (maxlen <= 0) {
        return 0;
    }

    const int channels = std::max(1, m_format.channelCount());
    const int bytesPerSample = m_format.bytesPerSample();
    const int bytesPerFrame = m_format.bytesPerFrame();
    if (bytesPerSample <= 0 || bytesPerFrame <= 0) {
        std::fill(data, data + maxlen, 0);
        return maxlen;
    }

    const qint64 frameCount = maxlen / static_cast<qint64>(bytesPerFrame);

    QMutexLocker locker(&m_mutex);

    QList<int> inactiveNotes;
    char* writePtr = data;
    float peak = 0.0f;

    for (qint64 i = 0; i < frameCount; ++i) {
        float mixed = 0.0f;

        for (auto it = m_voices.begin(); it != m_voices.end(); ++it) {
            SynthVoice* voice = it.value().get();
            mixed += voice->sample(m_dt);
            if (!voice->isActive()) {
                inactiveNotes.append(it.key());
            }
        }

        mixed *= m_blowPressure;
        mixed = std::clamp(mixed, -1.0f, 1.0f);
        peak = std::max(peak, std::abs(mixed));

        const auto format = m_format.sampleFormat();
        if (format == QAudioFormat::Int16) {
            const std::int16_t sample = static_cast<std::int16_t>(mixed * 32767.0f);
            auto* out = reinterpret_cast<std::int16_t*>(writePtr);
            for (int ch = 0; ch < channels; ++ch) {
                out[ch] = sample;
            }
        } else if (format == QAudioFormat::Int32) {
            const std::int32_t sample = static_cast<std::int32_t>(mixed * 2147483647.0f);
            auto* out = reinterpret_cast<std::int32_t*>(writePtr);
            for (int ch = 0; ch < channels; ++ch) {
                out[ch] = sample;
            }
        } else if (format == QAudioFormat::Float) {
            auto* out = reinterpret_cast<float*>(writePtr);
            for (int ch = 0; ch < channels; ++ch) {
                out[ch] = mixed;
            }
        } else if (format == QAudioFormat::UInt8) {
            const std::uint8_t sample = static_cast<std::uint8_t>((mixed * 0.5f + 0.5f) * 255.0f);
            auto* out = reinterpret_cast<std::uint8_t*>(writePtr);
            for (int ch = 0; ch < channels; ++ch) {
                out[ch] = sample;
            }
        } else {
            std::fill(writePtr, writePtr + bytesPerFrame, 0);
        }

        writePtr += bytesPerFrame;
    }

    for (int note : std::as_const(inactiveNotes)) {
        m_voices.remove(note);
    }

    m_lastPeakLevel = (m_lastPeakLevel * 0.82f) + (peak * 0.18f);
    return frameCount * static_cast<qint64>(bytesPerFrame);
}
