/*----------------------------------------------------------------------------
 * build_cfg.h  -  ANALYSIS / DEMO build switch (COE718 Lab 3a)
 *
 *   DEMO 0 : ANALYSIS version - runs in the uVision simulator. No LED or LCD
 *            code is compiled at all (the LCD driver and the LED/LCD helpers
 *            are compiled out). This is the version that is handed in.
 *   DEMO 1 : DEMO version - runs on the MCB1700 board. LEDs and the LCD show
 *            which thread is currently executing. Flash with F8 (Load).
 *
 * Change the one line below, then Project -> Rebuild all target files.
 *---------------------------------------------------------------------------*/
#ifndef BUILD_CFG_H
#define BUILD_CFG_H

#define DEMO   0

#endif /* BUILD_CFG_H */
