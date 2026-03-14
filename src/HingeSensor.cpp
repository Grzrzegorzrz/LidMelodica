#include "HingeSensor.h"

#include <QDebug>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {
constexpr const char* kScanEnablePath = "/sys/bus/iio/devices/iio:device1/scan_elements/in_angl0_en";
constexpr const char* kBufferEnablePath = "/sys/bus/iio/devices/iio:device1/buffer/enable";
constexpr const char* kBufferLengthPath  = "/sys/bus/iio/devices/iio:device1/buffer/length";
constexpr int kMinReadBytes = 4;
constexpr const char* kScalePath = "/sys/bus/iio/devices/iio:device1/in_angl_scale";
constexpr const char* kOffsetPath = "/sys/bus/iio/devices/iio:device1/in_angl_offset";
constexpr const char* kDevicePath = "/dev/iio:device1";
constexpr int kReadIntervalMs = 100;
constexpr int kInitRetryDelayMs = 300;
constexpr int kMaxIdlePollsBeforeReset = 25;
constexpr float kRadiansToDegrees = 57.2957795131f;
}

HingeSensor::HingeSensor(QObject* parent)
    : QThread(parent),
      m_running(false),
      m_scale(1.0f),
      m_offset(0.0f),
      m_sensorConfigured(false) {
}

HingeSensor::~HingeSensor() {
    requestStop();
    wait(500);
}

void HingeSensor::requestStop() {
    m_running.store(false);
}

void HingeSensor::run() {
    m_running.store(true);

    int deviceFd = -1;
    bool sensorAvailable = false;
    bool reportedFallback = false;
    int idlePolls = 0;
    float fallback = 135.0f;

    while (m_running.load()) {
        if (deviceFd < 0) {
            qDebug() << "[HingeSensor] Attempting sensor init...";
            if (initializeSensor(deviceFd)) {
                qDebug() << "[HingeSensor] Sensor online.";
                sensorAvailable = true;
                reportedFallback = false;
                idlePolls = 0;
                emit sensorAvailableChanged(true);
            } else {
                if (!reportedFallback) {
                    qDebug() << "[HingeSensor] Sensor unavailable; entering fallback mode.";
                    emit sensorAvailableChanged(false);
                    emit errorOccurred(QStringLiteral("Hinge sensor unavailable; retrying in background."));
                    reportedFallback = true;
                }

                emit angleChanged(fallback);
                msleep(kInitRetryDelayMs);
                continue;
            }
        }

        struct pollfd pfd;
        pfd.fd = deviceFd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        const int pollRet = ::poll(&pfd, 1, kReadIntervalMs);
        if (!m_running.load()) break;

        if (pollRet < 0) {
            qDebug() << "[HingeSensor] poll() error errno =" << errno;
            shutdownSensor(deviceFd);
            deviceFd = -1;
            if (sensorAvailable) {
                sensorAvailable = false;
                emit sensorAvailableChanged(false);
            }
            continue;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            qDebug() << "[HingeSensor] poll() device event error revents =" << pfd.revents;
            shutdownSensor(deviceFd);
            deviceFd = -1;
            if (sensorAvailable) {
                sensorAvailable = false;
                emit sensorAvailableChanged(false);
            }
            continue;
        }

        if (pollRet == 0 || !(pfd.revents & POLLIN)) {
            ++idlePolls;
            if (idlePolls >= kMaxIdlePollsBeforeReset) {
                qDebug() << "[HingeSensor] No samples for" << (idlePolls * kReadIntervalMs)
                         << "ms; resetting sensor stream.";
                shutdownSensor(deviceFd);
                deviceFd = -1;
                if (sensorAvailable) {
                    sensorAvailable = false;
                    emit sensorAvailableChanged(false);
                }
                idlePolls = 0;
            }
            continue;
        }

        std::uint8_t buffer[16] = {0};
        const ssize_t readCount = ::read(deviceFd, buffer, sizeof(buffer));
        if (readCount < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            qDebug() << "[HingeSensor] read() error errno =" << errno;
            shutdownSensor(deviceFd);
            deviceFd = -1;
            if (sensorAvailable) {
                sensorAvailable = false;
                emit sensorAvailableChanged(false);
            }
            continue;
        }

        if (readCount < static_cast<ssize_t>(kMinReadBytes)) {
            qDebug() << "[HingeSensor] short read:" << readCount << "bytes";
            continue;
        }

        idlePolls = 0;

        // Explicit little-endian parse matching main.c.
        const std::int16_t raw = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(buffer[0]) |
            (static_cast<std::uint16_t>(buffer[1]) << 8));

        const float angleDeg = (static_cast<float>(raw) + m_offset) * m_scale * kRadiansToDegrees;
        qDebug() << "[HingeSensor] raw =" << raw << "angle =" << angleDeg << "deg  (read" << readCount << "bytes)";
        emit angleChanged(angleDeg);
    }

    shutdownSensor(deviceFd);
    if (sensorAvailable) {
        emit sensorAvailableChanged(false);
    }
}

bool HingeSensor::initializeSensor(int& deviceFd) {
    m_sensorConfigured = false;
    m_scale = 1.0f;
    m_offset = 0.0f;

    qDebug() << "[HingeSensor] Disabling buffer...";
    if (!writeSysfs(kBufferEnablePath, "0"))
        qDebug() << "[HingeSensor] WARNING: could not disable buffer (may already be off)";

    qDebug() << "[HingeSensor] Enabling scan element in_angl0_en...";
    if (!writeSysfs(kScanEnablePath, "1")) {
        qDebug() << "[HingeSensor] FAIL: could not write" << kScanEnablePath;
        return false;
    }

    qDebug() << "[HingeSensor] Setting buffer length to 4...";
    if (!writeSysfs(kBufferLengthPath, "4")) {
        qDebug() << "[HingeSensor] FAIL: could not write" << kBufferLengthPath;
        writeSysfs(kScanEnablePath, "0");
        return false;
    }

    qDebug() << "[HingeSensor] Enabling buffer...";
    if (!writeSysfs(kBufferEnablePath, "1")) {
        qDebug() << "[HingeSensor] FAIL: could not write" << kBufferEnablePath;
        writeSysfs(kScanEnablePath, "0");
        return false;
    }

    float scale = 0.0f;
    float offset = 0.0f;
    if (!readFloatFromSysfs(kScalePath, scale)) {
        qDebug() << "[HingeSensor] Could not read scale from" << kScalePath << "- using default 0.017453293";
        scale = 0.017453293f;
    } else {
        qDebug() << "[HingeSensor] scale =" << scale;
    }
    if (!readFloatFromSysfs(kOffsetPath, offset)) {
        qDebug() << "[HingeSensor] Could not read offset from" << kOffsetPath << "- using 0";
        offset = 0.0f;
    } else {
        qDebug() << "[HingeSensor] offset =" << offset;
    }

    qDebug() << "[HingeSensor] Opening device node" << kDevicePath << "...";
    deviceFd = ::open(kDevicePath, O_RDONLY | O_NONBLOCK);
    if (deviceFd < 0) {
        qDebug() << "[HingeSensor] FAIL: open(" << kDevicePath << ") errno =" << errno;
        writeSysfs(kBufferEnablePath, "0");
        writeSysfs(kScanEnablePath, "0");
        return false;
    }

    qDebug() << "[HingeSensor] Init OK — fd =" << deviceFd << "scale =" << scale << "offset =" << offset;
    m_scale = scale;
    m_offset = offset;
    m_sensorConfigured = true;
    return true;
}

void HingeSensor::shutdownSensor(int deviceFd) {
    if (deviceFd >= 0) {
        ::close(deviceFd);
    }

    if (m_sensorConfigured) {
        writeSysfs(kBufferEnablePath, "0");
        writeSysfs(kScanEnablePath, "0");
        m_sensorConfigured = false;
    }
}

bool HingeSensor::writeSysfs(const char* path, const char* value) {
    const int fd = ::open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }

    const ssize_t len = static_cast<ssize_t>(std::strlen(value));
    const ssize_t written = ::write(fd, value, len);
    ::close(fd);
    return written == len;
}

bool HingeSensor::readFloatFromSysfs(const char* path, float& out) const {
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    char buf[64] = {0};
    const ssize_t bytes = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (bytes <= 0) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const float val = std::strtof(buf, &end);
    if (errno != 0 || end == buf) {
        return false;
    }

    out = val;
    return true;
}
