/*----------------------------------------------------------------------------
 * build_cfg.h  -  ANALYSIS / DEMO build switch (COE718 Lab 4, Part 1)
 *
 *   DEMO 0 : ANALYSIS version - runs in the uVision simulator. No LED or LCD
 *            code is compiled. Use Event Viewer + Watch 1 to check the
 *            timeline. This is the version that is handed in.
 *   DEMO 1 : DEMO version - runs on the MCB1700 board. LEDs and the LCD
 *            "RMS scope" show the calculated and the live timeline.
 *            Thread stacks grow to 256 words (RTX_Conf_CM.c) for the LCD code.
 *
 * Change the one line below, then Project -> Rebuild all target files.
 *---------------------------------------------------------------------------*/
#ifndef BUILD_CFG_H
#define BUILD_CFG_H

#define DEMO   0

#endif /* BUILD_CFG_H */
