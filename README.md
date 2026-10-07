# COE718 Labs: Embedded Systems Design

Lab work for **COE718 Embedded Systems Design** (Toronto Metropolitan University, Fall 2026). Every lab targets the **NXP LPC1768 (ARM Cortex-M3)** on the **Keil MCB1700** board and is built in **Keil µVision 5**.

> Shared for reference and portfolio purposes. If you're taking COE718, don't submit any of this as your own work.

## Labs

| Lab | Topic | What's in it |
|---|---|---|
| [Lab 1](Lab1/Boards/Keil/MCB1700/Blinky_ULp) | Intro to µVision and the MCB1700 | Joystick-driven LED and LCD program. Each direction (up, down, left, right) lights its own LED and shows the direction on the LCD. SELECT clears the LEDs. The LCD code can be switched on or off with a `_USE_LCD` macro. |
| [Lab 2](Lab2) | Cortex-M3 features for performance | Compares three ways to drive the LEDs: read-modify-write masking, a bit-band helper function, and direct bit-band alias stores. Also covers conditional execution (IT blocks) and the barrel shifter. The simulator version times each method in the Performance Analyzer. The board version races them as bars on the LCD. |
| [Lab 3a](Lab3a) | RTX multitasking with round-robin scheduling | CMSIS-RTOS (Keil RTX) projects. **Q1** is a three-thread power-on self-test (RAM march test, prime-count ALU test, CRC-32 integrity check) with 15 ms time slices. **Q2** simulates OS components (memory, CPU, application, device and user management) that coordinate through signals and a mutex. |

## Toolchain

The projects are pinned to the versions installed on the TMU lab PCs, so they build the same way on a personal laptop and in the lab.

| Item | Version |
|---|---|
| IDE | Keil µVision 5 (MDK) |
| Compiler | Arm Compiler 5 (`Use default compiler version 5`), MicroLIB |
| Device pack | Keil::LPC1700_DFP 2.6.0 |
| CMSIS pack | ARM::CMSIS 5.7.0, which includes Keil RTX 4.82 for CMSIS-RTOS v1 |
| Debug | µVision simulator for analysis; ULINK2/ME for flashing the board |

CMSIS 6.x removed RTX v4. If µVision asks about packs, pin CMSIS 5.7.0 in **Project → Manage → Select Software Packs** instead of choosing "use latest".

## Building

1. Open the lab's `.uvprojx` in µVision.
2. **Project → Rebuild all target files** (always do this after moving a project to another PC).
3. **Simulator:** Debug → Start/Stop Debug Session (Ctrl+F5), then Run (F5).
4. **Board:** Flash → Download (F8), then press RESET on the MCB1700.

Lab 3a projects have two build modes, switched in `build_cfg.h`:

```c
#define DEMO 0   // analysis version: simulator only, no LED/LCD code compiled
#define DEMO 1   // demo version: LEDs + LCD show the running thread
```

See [Lab3a/README.md](Lab3a/README.md) for expected Watch values, the screenshot checklist and a round-robin discussion.

## Repository layout

```
COE718-Labs/
├── Lab1/Boards/Keil/MCB1700/Blinky_ULp/   # Lab 1 project
├── Lab2/                                  # Lab 2 project (bit banding, IT blocks, barrel shifter)
└── Lab3a/
    ├── Lab3a/                             # in-lab RTX round-robin demo
    ├── Q1_RoundRobin/                     # assignment Q1
    └── Q2_OS_RoundRobin/                  # assignment Q2
```

Build output (`Objects/`, `Listings/`) and per-user µVision layout files (`*.uvguix.*`) are git-ignored.

The LCD driver (`GLCD_SPI_LPC1700.c`, `GLCD.h`, `Font_*.h`) and the device startup files come from Keil's MCB1700 examples and are subject to Keil's licence.

## Author

Gab Talavera, Computer Engineering, Toronto Metropolitan University
[GitHub](https://github.com/gabtlv) · [LinkedIn](https://linkedin.com/in/gabtlv) · [Portfolio](https://gabtlv.vercel.app)
