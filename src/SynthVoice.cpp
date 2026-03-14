#include "SynthVoice.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr std::array<float, 6> kHarmonics = {1.0f, 0.65f, 0.45f, 0.25f, 0.15f, 0.08f};
constexpr float kNormalization = 1.0f / 2.58f;
}

SynthVoice::SynthVoice(int midiNote, float sampleRate)
    : m_midiNote(midiNote),
      m_sampleRate(sampleRate),
      m_frequency(midiToFrequency(midiNote)),
      m_phases{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      m_stage(Stage::Off),
      m_isHeld(false),
      m_env(0.0f) {
}

int SynthVoice::midiNote() const {
    return m_midiNote;
}

bool SynthVoice::isActive() const {
    return m_stage != Stage::Off;
}

bool SynthVoice::isHeld() const {
    return m_isHeld;
}

void SynthVoice::noteOn() {
    m_isHeld = true;
    m_stage = Stage::Attack;
}

void SynthVoice::noteOff() {
    m_isHeld = false;
    if (m_stage != Stage::Off) {
        m_stage = Stage::Release;
    }
}

float SynthVoice::sample(float dt) {
    if (m_stage == Stage::Off) {
        return 0.0f;
    }

    const float envelope = nextEnvelopeValue(dt);

    float out = 0.0f;
    for (std::size_t i = 0; i < kHarmonics.size(); ++i) {
        const float harmonicNum = static_cast<float>(i + 1);
        const float phaseStep = kTwoPi * (m_frequency * harmonicNum) * dt;
        m_phases[i] += phaseStep;
        if (m_phases[i] >= kTwoPi) {
            m_phases[i] = std::fmod(m_phases[i], kTwoPi);
        }
        out += std::sin(m_phases[i]) * kHarmonics[i];
    }

    return out * kNormalization * envelope;
}

float SynthVoice::midiToFrequency(int midiNote) const {
    const float semitones = static_cast<float>(midiNote - 69) / 12.0f;
    return 440.0f * std::pow(2.0f, semitones);
}

float SynthVoice::nextEnvelopeValue(float dt) {
    switch (m_stage) {
    case Stage::Attack: {
        const float step = dt / kAttackTime;
        m_env += step;
        if (m_env >= 1.0f) {
            m_env = 1.0f;
            m_stage = Stage::Decay;
        }
        break;
    }
    case Stage::Decay: {
        const float step = dt * (1.0f - kSustainLevel) / kDecayTime;
        m_env -= step;
        if (m_env <= kSustainLevel) {
            m_env = kSustainLevel;
            m_stage = Stage::Sustain;
        }
        break;
    }
    case Stage::Sustain:
        m_env = kSustainLevel;
        break;
    case Stage::Release: {
        const float step = dt * std::max(m_env, 0.0001f) / kReleaseTime;
        m_env -= step;
        if (m_env <= 0.0005f) {
            m_env = 0.0f;
            m_stage = Stage::Off;
        }
        break;
    }
    case Stage::Off:
        m_env = 0.0f;
        break;
    }

    return m_env;
}
