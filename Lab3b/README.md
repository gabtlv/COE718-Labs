# Lab 3b: pre-emptive scheduling with RTX

| Folder | What it is |
|---|---|
| `InLab/` | In-lab exercises 1 to 3 (counta / countb). Change `#define EXERCISE` in `Thread.c` |
| `Q1_Preemptive/` | Assignment Q1: five math tasks A to E with priorities |
| `Q2_OS_Preemptive/` | Assignment Q2: five OS threads with priorities 1/1/2/2/4 |

All three are copies of the Lab 3a projects, so they keep the lab PC toolchain: ARM::CMSIS 5.7.0 (Keil RTX 4.82), Keil::LPC1700_DFP 2.6.0, default Arm Compiler 5, MicroLIB, simulator debug.

`RTX_Conf_CM.c` in every project: **Round-Robin Thread switching is OFF**, SysTick is ON, 10 MHz timer clock, 10000 µs tick (so 1 tick = 1 ms, because the core runs at 100 MHz).

**Stack size (Q1 and Q2):** the analysis build uses the Configuration Wizard value, 128 words (512 bytes) per thread. The demo build automatically uses **256 words (1024 bytes)**, because the LCD and LED code needs more stack. This is set by a small `#if DEMO` block at the top of `RTX_Conf_CM.c` that reads `build_cfg.h`, so the one `DEMO` switch changes both the code and the stack size.

Analysis vs demo: same as 3a. Set `#define DEMO 0` (analysis, hand this in) or `1` (board demo) in `build_cfg.h`, then **Rebuild all**. Flash the demo with **F8**.

All builds (Q1 and Q2 in both DEMO modes, and in-lab exercises 1 to 3) were checked on the laptop: 0 errors, 0 warnings. Q1 and Q2 analysis versions were run in the simulator and gave the values below.

## Demo theme: airport control tower

The runway is the CPU, every thread is a flight, and the tower is the scheduler. The LCD shows a flight board (one line per thread with its priority and status), a tower radio line, and a runway where the running thread's plane lands. LED n = thread n is on the runway, and all LEDs light at the end.

- **Q1:** Task C is the MAYDAY flight. Halfway through Task A's landing, A is told to "go around" (its plane moves up to the holding lane), C lands, then A lands from where it stopped. Each result replaces the status when the flight lands. The end screen shows `ALL LANDED: CADBE`.
- **Q2:** LANDING = running, CIRCLING = waiting for a signal, TAXI = osDelay(1), AT GATE = deleted. The radio line shows each signal hand-off (MEM>CPU, CPU>MEM, APP>DEV, DEV>APP). The end screen shows the full logger message.
- The drawing code is in `demo_io.c` (same file in both projects). The flight board is drawn from main() before `osKernelStart()`, so the LCD mutex is only used once the kernel runs (`osKernelRunning()`).
- A preview of both demos (the same code on a simulated LCD) was checked before trying it on the board.

## Priority mapping

The lab uses 1 = highest. CMSIS-RTOS uses names:

| Lab priority | CMSIS priority |
|---|---|
| 1 | `osPriorityHigh` |
| 2 | `osPriorityAboveNormal` |
| 3 | `osPriorityNormal` (same as `main`) |
| 4 | `osPriorityBelowNormal` |

All threads are created **before** `osKernelStart()`. If they were created after it, every higher-priority thread would pre-empt `main()` the moment it was created, so the first one created would run first no matter its priority.

## In-lab exercises

- **Ex 1** (`EXERCISE 1`): Thread1 is AboveNormal and never blocks, so only Thread1 runs. `countb` stays 0. Note the handout's explanation says "Thread2" has the higher priority, but its code line changes **Thread1**. The project follows the code line.
- **Ex 2** (`EXERCISE 2`): both Normal, each calls `osThreadYield()` after one increment. The Event Viewer shows a very fast ping-pong (about 2.5 µs per thread). It looks like round-robin, but the switch is **voluntary** (the thread gives up the CPU) instead of forced by a time slice. CPU utilization is about 100%: `countIDLE` stays 0.
- **Ex 3** (`EXERCISE 3`): Thread2 `osDelay(1)`, Thread1 `osDelay(2)`. Each thread runs for a few µs per tick and then blocks, so the idle demon runs almost all the time. `countb` grows about twice as fast as `counta`, and `countIDLE` grows quickly. CPU utilization is very low (well under 1%). Read the exact figure from the Performance Analyzer.

## Q1: five tasks

| Task | Formula | Lab prio | Result (Watch 1) |
|---|---|---|---|
| A | Σ (x + (x+2)), x = 0..256 | 2 | `resultA = 66306` |
| B | Σ 2ⁿ/n!, n = 1..16 | 3 | `resultB = 6.389056` (about e² − 1) |
| C | Σ (n+1)/n, n = 1..16 | 1 | `resultC = 19.380729` (16 + H₁₆) |
| D | 1 + 5/1! + 5²/2! + … + 5⁵/5! | 2 | `resultD = 91.416667` |
| E | πr² + 2πr² + … + 12πr² = 78πr² | 3 | `resultE = 980.176908` |

- **Parameters are in Watch 1 too:** `A_xMax = 256`, `B_nMax = 16`, `C_nMax = 16`, `D_kMax = 5`, `E_kMax = 12`, `E_r = 2.0`. The tasks read their limits from these variables, so the Watch window shows exactly what each calculation used.
- **r = 2** for Task E (the handout gives no value). Change `E_r` if your TA wants another value, and say which r you used in the report.
- **Pre-emption you can see:** Task C (prio 1) starts by waiting for a "request" signal. Task A sends it when it is halfway done. C is now ready with a higher priority than A, so RTX interrupts A in the middle of its calculation, runs C to completion, then resumes A. `A_repsAtPreempt = 50` proves A was frozen at 50% while C ran. In the Event Viewer, A's bar is split in two with C in the middle.
- Finish order: **`finishOrder = "CADBE"`**, `tasksDone = 5`. C (prio 1) first, then A and D (prio 2, in creation order), then B and E (prio 3).
- Each sum is recomputed `WORK_REPEAT = 100` times only so each task is wide enough to see in the Event Viewer. The answer doesn't change. All five finish at about **58 ms** in the simulator, then only idle runs.
- Results are shown in the Watch window (the handout allows printf or the watch window).
- Demo: LED 0 to 4 = task A to E. Each task takes about 1.5 s with its LED lit. Halfway through A, LED 2 (Task C) takes over, then A resumes. Each task's result appears on the LCD when it finishes, and the finish order is shown at the end.

## Q2: OS problem

Priorities: Memory and CPU = 1 (High), App and Device = 2 (AboveNormal), User = 4 (BelowNormal). The code is the 3a version with the priorities changed. The signals and logger mutex are the same.

Order: Memory → CPU → Memory (osDelay) → App → Device → App (osDelay) → User (osDelay) → idle. All threads are blocked by about **0.27 ms**. One tick later the delayed threads wake up, pre-empt idle in priority order, and delete themselves.

Expected Watch values (checked in the simulator): `memBitResult = 0x80000201`, `condResult = 9`, `barrelResult = 25`, `cpuHwOK = 1`, all five counters = 1, `logger = "APP: open file -> DEV: written, closed"`.

## Screenshots to hand in (each question)

1. **Configuration Wizard** tab of `RTE/CMSIS/RTX_Conf_CM.c` (Expand All), showing Round-Robin unticked.
2. **Event Viewer**: run, Stop, click **All**, zoom in.
   - Q1: one shot showing the order C, A, D, B, E.
   - Q2: one shot of the whole run, one zoomed on the first 0.3 ms (the hand-offs), and one on the wake-up after the 1-tick delay.
3. **Performance Analyzer**, after every thread has finished. For Q2, stop after `countIDLE` has started counting so every thread function is listed.
4. **Watch 1** with the values above (watch items are preset).

## Q2: pros and cons of pre-emptive scheduling for this OS problem

**Pros**

- **Urgent work goes first.** Memory and CPU management (prio 1) always run before the App, Device and User threads. The User Interface (prio 4) only runs when nothing more important is ready.
- **Fast response.** When a high-priority thread becomes ready (for example Memory waking up after its 1-tick delay), it pre-empts a lower-priority one right away instead of waiting for a time slice to end.
- **Low overhead.** There are no forced time-slice switches. The only context switches happen when a thread blocks, finishes, or a higher-priority thread wakes up.
- **Predictable order.** The priorities alone decide most of the order, so it is easy to read from the Event Viewer and reason about.

**Cons**

- **Starvation.** A high-priority thread that never blocks (like in-lab Exercise 1) stops all lower-priority threads forever. The User Interface could starve if the higher threads kept running.
- **Priorities have to be chosen carefully.** A wrong priority changes the whole behaviour. Equal-priority threads (Memory and CPU, App and Device) don't share the CPU on their own; they need signals or yields.
- **Priority inversion risk.** If a low-priority thread holds a mutex that a high-priority thread needs, the high-priority thread waits. Here the logger mutex is only used by two threads of the same priority, so it doesn't happen, but it could in a bigger system.
- **Dependencies are still manual.** Priorities don't know that Device must run after the App. The signals are still needed.

## Round-robin (3a) vs pre-emptive (3b) for the OS problem

| | Round-robin (Lab 3a) | Pre-emptive (Lab 3b) |
|---|---|---|
| Who runs next | Next thread in the ready list when the 10 ms slice ends or a thread blocks | Highest-priority ready thread |
| Priorities | All equal (Normal) | 1/1/2/2/4 |
| Order | Creation order, then whoever is ready | Memory/CPU first, then App/Device, User last |
| Switching cause | Time slice or blocking | Blocking, finishing, or a higher-priority thread waking up |
| Fairness | Every thread gets CPU time | Lower priorities can starve |
| Response to urgent work | Has to wait up to (N − 1) × slice | Immediate |
| Ordering of dependent threads | Needs signals | Still needs signals (for equal priorities) |

For this problem the threads are so short (microseconds) that they block long before a 10 ms slice ends. So in 3a the time slice almost never mattered, and most switching came from blocking. With priorities, the more critical OS work (memory and CPU management) is guaranteed to finish first, and the User Interface runs last. That fits an OS better than round-robin's equal sharing.
