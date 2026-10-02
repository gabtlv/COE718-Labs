# COE 718 Lab 2: Cortex-M3 Features for Performance Efficiency

Keil µVision project for the NXP LPC1768 (MCB1700 board) exploring three Cortex-M3 features:

- **Bit banding**: set/clear single bits via alias addresses instead of read-modify-write masking
- **Conditional execution**: S-suffix flag updates and IT/ITE blocks instead of branches
- **Barrel shifter**: shifts folded into ALU instructions (`ADD r0, r1, r2, LSL #2`)

## Files

| File | Purpose |
| --- | --- |
| `lab2_debug.c` | Simulator version: times the three LED methods (mask, BitBand() function, direct bit banding) in the Performance Analyzer |
| `lab2_demo.c` | Board version: blinks LEDs P1.28, P1.29, P2.2 with each method and shows the method + barrel-shifter result on the LCD |
| `RTE/bitbanding`, `RTE/cond_exe`, `RTE/Barrel_shifting` | Course-provided example code for each section |
| `GLCD_SPI_LPC1700.c`, `GLCD.h`, `Font_*.h` | Keil LCD driver (from Lab 1) |

Only one file with `main()` is included in the build at a time (Options for File → Include in Target Build).

## Toolchain

- Keil MDK 5 with Arm Compiler 5.06u7
- Packs: Keil::LPC1700_DFP 2.7.2, Keil::MCB1700_BSP, Keil::MDK-Middleware 7.17.0, ARM::CMSIS 5.9.0
