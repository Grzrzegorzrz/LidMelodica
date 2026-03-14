#pragma once

#include <QThread>
#include <atomic>

class HingeSensor : public QThread {
    Q_OBJECT

public:
    explicit HingeSensor(QObject* parent = nullptr);
    ~HingeSensor() override;

    void requestStop();

signals:
    void angleChanged(float degrees);
    void sensorAvailableChanged(bool available);
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    bool initializeSensor(int& deviceFd);
    void shutdownSensor(int deviceFd);
    bool writeSysfs(const char* path, const char* value);
    bool readFloatFromSysfs(const char* path, float& out) const;

    std::atomic<bool> m_running;
    float m_scale;
    float m_offset;
    bool m_sensorConfigured;
};
