# Cold Glue Controller — Main Controller Firmware (Stage 1)

ESP32-S3-WROOM-1-N16R8 firmware for a 4-channel high-speed cold-glue gun
controller. Up to 330 Hz droplet rate; current chopping is handled by the
on-board LM339 hardware loop. The ESP32 only orchestrates phase transitions.

See `docs/initial prompt.md` for the full hardware architecture and operational
spec.

## Toolchain

PlatformIO (recommended). Open the project folder in VS Code with the
PlatformIO extension and use `PlatformIO: Upload` / `Monitor`.

```
pio run -t upload
pio device monitor
```

## Source Tree

```
src/
  main.cpp                          task wiring + boot order
  config/Config.{h,cpp}             double-buffered config + pattern storage
  comms/
    Events.{h,cpp}                  Core1->Core0 lock-free event queue
    UartJson.{h,cpp}                NDJSON parser + command dispatcher
  hw/
    Pins.h                          pin map (verbatim from docs)
    Driver.{h,cpp}                  IN1/IN2/MUX_SELECT abstraction, killAll
    Dac.{h,cpp}                     MCP4728 with task-serialized I2C writes
    Encoder.{h,cpp}                 PCNT counter + photocell ISR (any-edge)
  rt/
    GunSequencer.{h,cpp}            per-gun Peak/Hold/Decay state machine
    PatternScheduler.{h,cpp}        sheet FIFO + lazy pattern walker
    Control.{h,cpp}                 surface called by JSON dispatcher
  sys/
    Fault.{h,cpp}                   nFAULT ISR -> master kill
    Watchdog.{h,cpp}                HMI link timeout
    Status.{h,cpp}                  5 Hz status emitter
```

## Task Layout

| Core | Task        | Prio | Purpose                                |
| ---- | ----------- | ---- | -------------------------------------- |
| 0    | `uart_rx`   | 4    | NDJSON parser                          |
| 0    | `evt_emit`  | 5    | Drains event queue to Serial           |
| 0    | `wdog`      | 3    | HMI link watchdog                      |
| 0    | `status`    | 2    | 5 Hz `{"event":"status"}`              |
| 1    | `pattern`   | 7    | Encoder poll + fire pattern events     |
| 1    | `dac`       | 6    | Coalesced MCP4728 I2C writes           |
| 1    | ISRs        | IRAM | Peak (x4), photocell, nFAULT, PCNT     |

## Protocol

Newline-delimited JSON over UART0 (115200 8N1) on GPIO 43 TX / GPIO 44 RX,
routed through the on-board CP2102 USB-UART for development.

Commands and events follow `docs/initial prompt.md` Section 8 exactly.
