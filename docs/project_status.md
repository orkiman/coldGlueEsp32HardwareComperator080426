# Cold Glue Controller — Project Status

> Living document. Reflects what is **actually implemented** in firmware.
> The original spec lives in `initial prompt.md` and should not be edited;
> this file records any decisions, deltas, and the current state.

Last updated: Stage 1 firmware scaffold complete, awaiting bench bring-up.

---

## 1. Stage Status

| Stage | Scope                                          | Status            |
| ----- | ---------------------------------------------- | ----------------- |
| 1     | ESP32-S3 main controller firmware              | **Code complete** — pending hardware bring-up |
| 2     | PC-side GUI over UART0 (replaces the original LVGL HMI plan) | Not started |

The original Stage 2 plan was an LVGL HMI on a second ESP32-S3 + 7" touch
panel. **Revised decision:** for now the operator UI runs on a PC and talks
to the main controller through the on-board CP2102 USB-UART. The wire
protocol (NDJSON over UART0 @ 115200) is identical, so swapping back to an
embedded HMI later is a drop-in.

---

## 2. Implemented Architecture

### Dual-core task layout

| Core | Task        | Prio | File                              | Purpose                                |
| ---- | ----------- | ---- | --------------------------------- | -------------------------------------- |
| 0    | `uart_rx`   | 4    | `comms/UartJson.cpp`              | NDJSON parser + command dispatcher     |
| 0    | `evt_emit`  | 5    | `comms/Events.cpp`                | Drains lock-free event queue -> Serial |
| 0    | `wdog`      | 3    | `sys/Watchdog.cpp`                | HMI link timeout (2 s)                 |
| 0    | `status`    | 2    | `sys/Status.cpp`                  | 5 Hz `{"event":"status"}`              |
| 1    | `pattern`   | 7    | `rt/PatternScheduler.cpp`         | Encoder poll + fire pattern events     |
| 1    | `dac`       | 6    | `hw/Dac.cpp`                      | Coalesced MCP4728 I2C writes           |
| 1    | ISRs        | IRAM | `hw/Encoder`, `rt/GunSequencer`, `sys/Fault` | Peak (x4), photocell, nFAULT, PCNT overflow |

### Key isolation rules (enforced by file layout)

- Only `evt_emit` task ever touches `Serial`. No other module may print.
- ISRs never block on I2C: DAC writes are queued to the Core-1 `dac` task.
- Config is published via atomic double-buffer pointer swap; hot path reads
  are lock-free.
- Cross-core events use FreeRTOS queues with `*FromISR` APIs.

---

## 3. Dot Sequence — Implementation Map

| Phase                | Trigger                | File / function                                          |
| -------------------- | ---------------------- | -------------------------------------------------------- |
| 1 Peak / Pick        | `seq::fire(g)`         | `rt/GunSequencer.cpp :: fire()`                          |
| 2 Hold (chopping)    | LM339 peak IRQ         | `rt/GunSequencer.cpp :: peakIsr()`                       |
| 3 Active Fast Decay  | `esp_timer` callback   | `rt/GunSequencer.cpp :: holdTimerCb() -> enterPhase3()`  |

Per-gun state machine is `Idle -> Peak -> Hold -> Decay -> Idle`, guarded
by a `compare_exchange_strong` so `fire()` is safe from any context.

---

## 4. Pattern Execution

- Per-gun **sheet FIFO depth = 4** (`SHEET_QUEUE_DEPTH` in `PatternScheduler.cpp`).
- Sheet entries store only the encoder pulse count at the photocell leading
  edge. Patterns are walked **lazily** at runtime — dots are not pre-expanded.
- Pattern coordinate origin = paper leading edge =
  `photocell pulse + photocell_offset_mm * pulses_per_mm`.
- If gap between sheets < `photocell_offset_mm`, multiple sheets are tracked
  in flight on the same gun. Queue overflow drops the new trigger silently.
- Speed-safety: if measured speed drops below `min_speed_mm_s`, the pattern
  task suppresses firing until speed recovers.

---

## 5. Diagnostic `test_open` Behaviour (revised)

`test_open` no longer issues a single raw dot. Behaviour depends on the gun's
currently configured pattern type (see `rt/TestRunner.cpp`):

| Pattern type | Test behaviour                                                       |
| ------------ | -------------------------------------------------------------------- |
| `lines`      | Single line, hardware-regulated, held for `timeout_ms`, then Phase 3 |
| `dots`       | Continuous dot train at 20 Hz until `timeout_ms` or `test_close`     |
| `none`       | `{"event":"error","cmd":"test_open","reason":"no_pattern_or_busy"}`  |

`gun:0` broadcasts the test to every gun with a configured pattern.
`timeout_ms` is capped at 5000 in `UartJson::handleTestOpen`.

---

## 6. Fault & Safety Path

- **nFAULT (GPIO 39, active LOW)**: IRAM ISR in `sys/Fault.cpp` immediately
  calls `drv::killAll()`, aborts all gun sequencers, sets
  `g_sys.fault = true`, `g_sys.active = false`, and emits
  `{"event":"error","reason":"hardware_fault"}`.
- Recovery requires a fresh `set_active:true` from the operator.
- **Watchdog**: any received NDJSON command (incl. `ping`) refreshes the
  timestamp. After 2 s of silence while active, outputs are killed and
  `{"event":"watchdog_timeout"}` is emitted.

---

## 7. Protocol — Deltas From `initial prompt.md` §8

The wire protocol is unchanged. The only addition is one new `error`
reason string:

| Where          | Reason                  | Meaning                                                 |
| -------------- | ----------------------- | ------------------------------------------------------- |
| `test_open`    | `no_pattern_or_busy`    | Gun has no pattern, or a test is already running on it. |

All other validation errors (`bad_pulses_per_mm`, `hold_ge_pick`, etc.) are
sanity checks added to `set_config` / `set_pattern`.

---

## 8. Open Items For Bench Bring-Up

- **`Adafruit_MCP4728::fastWrite` signature** — assumed `(uint16_t, uint16_t, uint16_t, uint16_t)` returning `bool`; will fail at compile if the installed lib differs.
- **PCNT glitch filter** at 100 APB ticks (~1.25 µs). Will tune once we see the 6N137's real edge behaviour on the scope.
- **`NEAR_ZERO_V = 0.10 V`** Phase-3 termination threshold. Tunable in `rt/GunSequencer.cpp`. Right now corresponds to ~0.05 A coil current.
- **Hold-timer dispatch via `esp_timer` task context** (not ISR). Worst-case latency ~50 µs; fine for hold ≥ 1 ms but introduces jitter if `hold_time_ms` is set absurdly small.

---

## 9. File Tree (current)

```
platformio.ini
README.md
docs/
  initial prompt.md            (immutable spec — do not edit)
  project_status.md            (this file)
src/
  main.cpp
  config/Config.{h,cpp}
  comms/Events.{h,cpp}    UartJson.{h,cpp}
  hw/Pins.h               Driver.{h,cpp}   Dac.{h,cpp}   Encoder.{h,cpp}
  rt/Control.{h,cpp}      GunSequencer.{h,cpp}   PatternScheduler.{h,cpp}   TestRunner.{h,cpp}
  sys/Fault.{h,cpp}       Watchdog.{h,cpp}       Status.{h,cpp}
```
