#pragma once

#include <Arduino.h>

#include "ftms_encoder.h"
#include "metrics_model.h"

class BleFtmsService {
public:
    // What an app asked for through the Fitness Machine Control Point.
    enum class Command : uint8_t { None, Reset, Start, Stop, Pause };

    void begin(const String& deviceName);
    void notify(const RowerMetrics& metrics);

    // Main loop: the next queued control point command, or None. Commands
    // arrive on the NimBLE task and are acknowledged there; acting on them
    // (session state, screens) belongs to the main loop.
    Command takeCommand();

    // Fitness Machine Status notification, for changes from the app or the
    // display alike.
    void notifyStatus(ftms::status::Event event, uint8_t parameter = 0);

private:
    bool ready_ = false;
};
