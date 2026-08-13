#pragma once

#include <Arduino.h>

#include "metrics_model.h"

class BleFtmsService {
public:
    void begin(const String& deviceName);
    void notify(const RowerMetrics& metrics);

private:
    bool ready_ = false;
};
