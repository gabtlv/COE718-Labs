# Lab 3a assignment projects

| Folder | What it is |
|---|---|
| `Lab3a/` | In-lab demo project from the lab PC (counta / countb) |
| `Q1_RoundRobin/` | Q1: three-task round-robin "Power-On Self-Test", 15 ms slices |
| `Q2_OS_RoundRobin/` | Q2: five OS threads (Memory, CPU, App, Device, User) |

Both projects use the lab PC toolchain: ARM::CMSIS 5.7.0 (Keil RTX 4.82), Keil::LPC1700_DFP 2.6.0, default Arm Compiler 5, MicroLIB, simulator debug.
Both were rebuilt on the laptop with 0 errors and 0 warnings and run correctly in the simulator.

## Analysis vs demo version

Open `build_cfg.h` and change one line, then **Project → Rebuild all target files**:

- `#define DEMO 0`: **analysis** version (the one to hand in). No LED or LCD code is compiled, and the LCD driver file compiles to nothing.
- `#define DEMO 1`: **demo** version for the board. LEDs and the LCD show which thread is running. Flash it with **F8 (Load)** and press RESET on the board. The Debug tab can stay on Simulator because flashing uses the ULINK2 driver set in the Utilities tab.

## Q1: what it does

Three equal-priority threads, created in this order: tRAM, tPrime, tCRC. Each one has a finite workload, then deletes itself.

- **tRAM**: RAM march test, 300 passes over a 1 KB buffer. Expect `ramErrors = 0`.
- **tPrime**: counts the primes up to 20000. Expect `primeCount = 2262` (0x8D6).
- **tCRC**: CRC-32 of a 512-byte block, 100 rounds. Every round must match, so expect `crcErrors = 0`.

When all three tests are done, `testsDone = 3`, `postPassed = 1`, and `countIDLE` starts increasing.

RTX_Conf_CM.c settings: 10 MHz timer clock, 10000 µs tick, Round-Robin Timeout **15**.
The core really runs at 100 MHz, so one tick is 1 ms and the slice is 15 ms. The Event Viewer confirms this.

**Measured in the Event Viewer:** 15 ms slices rotating tPrime → tCRC → tRAM. All three tests finish at about 123 ms, and after that only Idle runs.

## Q2: what it does

Memory Management is created first, so it runs first.

- **Memory → CPU:** Memory sets a signal to CPU, then waits for CPU's signal back.
- **App → Device:** the App writes the first part of `logger` (under a mutex), then sets a signal to Device. This guarantees the App runs before Device. Device writes the ending of `logger` and signals the App back.
- **Clean-up:** every thread waits one tick with `osDelay(1)` where the handout asks for it, then deletes itself.

**Expected Watch values:**

- `memBitResult = 0x80000201`: bit-band writes to a word at 0x2007C000. Bit-banding only works in the 0x2000_0000 SRAM region, not the main SRAM at 0x1000_0000.
- `condResult = 9` and `barrelResult = 25`: the Lab 2 conditional-execution and barrel-shifter loops.
- `cpuHwOK = 1`: the assembly version (ITE GT block plus `ADD r0, r2, r1, LSL #2`) gives the same result as the C version.
- `appCounter = devCounter = userCount = memAccessCount = cpuAccessCount = 1`.
- `logger = "APP: open file -> DEV: written, closed"`.

## Screenshots to hand in (each question)

1. The **Configuration Wizard** tab of `RTE/CMSIS/RTX_Conf_CM.c`, with Expand All.
2. The **Event Viewer**: run, Stop, then click **All**, and zoom so the slices are readable.
   - For Q1, also hover over one slice to show the 15 ms duration.
   - For Q2, take one screenshot of the whole run and one zoomed in on the burst where the threads hand off to each other.
3. The **Performance Analyzer**, taken after every thread has finished. For Q2, stop after `logger` is complete and `countIDLE` has started counting, so every thread function shows up in the list.
4. **Watch 1** with the expected values above. The watch items are already preset.

## Q2: pros and cons of round-robin for this OS problem

**Pros**

- **Fairness.** Equal-priority threads (Memory, App, User) each get CPU time. No thread can hog the processor, so the User Interface is serviced even while other threads are busy.
- **Simple and predictable.** There are no priorities to tune. The worst-case wait for the CPU is bounded at (N − 1) × time slice.
- **Natural fit for independent jobs.** User Interface has no dependencies, so it simply interleaves with the others.

**Cons**

- **Blind to dependencies.** Round-robin ignores which thread needs to go first. The required order (Memory → CPU, App → Device) had to be enforced by hand with signals and a mutex. Without them, Device could write the end of the log before the App wrote the beginning.
- **Mostly irrelevant for short jobs.** These threads finish in microseconds, far shorter than the 10 ms slice. They block (wait for a signal or `osDelay`) long before the slice ends, so most switching comes from blocking, not from the time slice.
- **No urgency.** Memory and CPU management are more critical than counting users, but round-robin gives them the same share. A priority-based (preemptive) scheduler would let them run first.
- **Overhead.** If the time slice is too small, context-switch overhead grows. If it is too large, round-robin behaves like first-come-first-served and responsiveness drops.
