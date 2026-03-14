# Qt Melodica Simulator

A qt6 app to play a melodica using a Lenovo Yoga 7's lid pitch sensor + the keyboard.

No Yoga 7? No issue! The app features a manual pressure slider!

## Features

- 2-row QWERTY note mapping with black and white key layout.
- Octave shift with Left/Right arrows (base octave range 2-6).
- Polyphonic voice engine using `QAudioSink`.
- Hinge sensor reader thread using `/sys/bus/iio/devices/iio:device1/` and `/dev/iio:device1`.

## Build Dependencies

Generic dependencies:
- Qt6 Widgets
- Qt6 Multimedia
- CMake 3.16+
- C++17 compiler

<details>
<summary>Windows</summary>
<br>
Note: The lid angle sensor access on Windows will not work
<br>
<pre><code>- Install [Qt 6.5](https://www.qt.io/download) with MSVC 2019 or 2022.
- Install [CMake](https://cmake.org/download/) and add it to PATH.
- Install [Ninja](https://ninja-build.org/) and add it to PATH.
</code></pre>
</details>

<details>
<summary>Debian/Ubuntu</summary>
<pre><code>sudo apt update
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build build-essential
</code></pre>
</details>

<details>
<summary>Arch</summary>
<pre><code>sudo pacman -S qt6-base qt6-multimedia cmake ninja gcc
</code></pre>
```
</details>

## Build

```bash
cd ./LidMelodica
cmake -S . -B build -G Ninja
cmake --build build
```

## Run

```bash
./build/qt-melodica
```

## Keyboard Mapping

White keys:

- Lower row: `Z X C V B N M ,` -> `C4 D4 E4 F4 G4 A4 B4 C5`
- Upper row: `Q W E R T Y U I` -> `C5 D5 E5 F5 G5 A5 B5 C6`

Black keys:

- Lower row: `S D G H J` -> `C#4 D#4 F#4 G#4 A#4`
- Upper row: `2 3 5 6 7` -> `C#5 D#5 F#5 G#5 A#5`

## Permissions and Sensor Access

The sensor needs read/write access to `/dev/iio:device1` and several sysfs attributes.
Install the included udev rule once — no group creation or logout required:

```bash
sudo cp udev/99-yoga7-iio.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
```

To verify:

```bash
getfacl /dev/iio:device1          # should show your user with rw
cat /sys/bus/iio/devices/iio:device1/in_angl0_scale   # should succeed without sudo
```

If sensor initialization still fails, the app falls back to the manual pressure slider automatically.
