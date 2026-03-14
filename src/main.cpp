#include "AudioEngine.h"
#include "HingeSensor.h"
#include "MelodicaWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    AudioEngine audioEngine;
    HingeSensor hingeSensor;

    MelodicaWindow window(&audioEngine, &hingeSensor);
    window.show();

    hingeSensor.start();
    const int rc = app.exec();

    hingeSensor.requestStop();
    hingeSensor.wait(500);
    return rc;
}
