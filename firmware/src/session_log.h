#pragma once

#include <cstddef>

#include "metrics_model.h"

// Records a rowing session once per second and writes it to the SD card as
// a TCX file (/sessions/<start time>.tcx) when it ends. Samples live in
// PSRAM; the card is only touched in save() (see sd_card.h for why).
namespace session_log {

// Clears the samples for a new session.
void start();

// Call every loop while the session runs; keeps one sample per second of
// session time (RowerMetrics::elapsedSec, so pauses leave no gap).
void sample(const RowerMetrics& m);

enum class SaveResult { Saved, Empty, NoCard, Failed };

// Writes the session to the card. `finalMetrics` supplies the totals;
// `expander` is panel_runtime::ioExpanderBase(). On Saved, `fileName` gets
// the file's name (no folder). Sessions without a stroke are Empty and not
// written. Blocks for up to a few seconds on long sessions.
SaveResult save(const RowerMetrics& finalMetrics, void* expander, char* fileName, size_t cap);

} // namespace session_log
