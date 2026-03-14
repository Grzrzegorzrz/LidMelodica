#pragma once

#include <array>

class SynthVoice {
public:
    SynthVoice(int midiNote, float sampleRate);

    int midiNote() const;
    bool isActive() const;
    bool isHeld() const;

    void noteOn();
    void noteOff();
    float sample(float dt);

private:
    enum class Stage {
        Attack,
        Decay,
        Sustain,
        Release,
        Off
    };

    float midiToFrequency(int midiNote) const;
    float nextEnvelopeValue(float dt);

    int m_midiNote;
    float m_sampleRate;
    float m_frequency;
    std::array<float, 6> m_phases;

    Stage m_stage;
    bool m_isHeld;
    float m_env;

    static constexpr float kAttackTime = 0.010f;
    static constexpr float kDecayTime = 0.050f;
    static constexpr float kSustainLevel = 0.80f;
    static constexpr float kReleaseTime = 0.080f;
};
