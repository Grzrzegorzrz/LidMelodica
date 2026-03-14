#pragma once

#include <QString>
#include <Qt>
#include <unordered_map>

class KeyMap {
public:
    static constexpr int kMinNote = 53; // F3
    static constexpr int kMaxNote = 89; // F6

    static int keyToNote(Qt::Key key, int octaveOffset) {
        static const std::unordered_map<int, int> baseMap = {
            // Lower row white keys
            {Qt::Key_Z, 60}, {Qt::Key_X, 62}, {Qt::Key_C, 64}, {Qt::Key_V, 65},
            {Qt::Key_B, 67}, {Qt::Key_N, 69}, {Qt::Key_M, 71}, {Qt::Key_Comma, 72},
            // Lower row black keys
            {Qt::Key_S, 61}, {Qt::Key_D, 63}, {Qt::Key_G, 66}, {Qt::Key_H, 68}, {Qt::Key_J, 70},
            // Upper row white keys
            {Qt::Key_Q, 72}, {Qt::Key_W, 74}, {Qt::Key_E, 76}, {Qt::Key_R, 77},
            {Qt::Key_T, 79}, {Qt::Key_Y, 81}, {Qt::Key_U, 83}, {Qt::Key_I, 84},
            // Upper row black keys
            {Qt::Key_2, 73}, {Qt::Key_3, 75}, {Qt::Key_5, 78}, {Qt::Key_6, 80}, {Qt::Key_7, 82},
        };

        const auto it = baseMap.find(static_cast<int>(key));
        if (it == baseMap.end()) {
            return -1;
        }

        const int shifted = it->second + (octaveOffset * 12);
        if (shifted < kMinNote || shifted > kMaxNote) {
            return -1;
        }

        return shifted;
    }

    static bool isOctaveDownKey(Qt::Key key) {
        return key == Qt::Key_Left;
    }

    static bool isOctaveUpKey(Qt::Key key) {
        return key == Qt::Key_Right;
    }

    static QString midiToNoteName(int midiNote) {
        static const char* names[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        if (midiNote < 0) {
            return QStringLiteral("-");
        }

        const int octave = (midiNote / 12) - 1;
        return QString("%1%2").arg(names[midiNote % 12]).arg(octave);
    }
};
