#pragma once

// =============================================================================
// NDJSON command parser. Spawns a Core-0 task that:
//   - reads bytes from Serial non-blocking,
//   - accumulates a line buffer until '\n',
//   - parses with ArduinoJson v7,
//   - dispatches to cfg::Config + rt::Control,
//   - emits ack / error via evt::.
// Refreshes cfg::g_sys.lastCmdMs on every accepted command (watchdog feed).
// =============================================================================

namespace uartjson {

void init();  // call after evt::init()

} // namespace uartjson
