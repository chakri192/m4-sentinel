# m4-sentinel 

A lightweight system monitor daemon for **macOS on Apple Silicon (M-series)**. Written in C11, it continuously polls thermal pressure and CPU load using native macOS APIs, logs timestamped stats to a file, and fires a system notification when pressure thresholds are exceeded.

---

## Features

- Polls `vm.pressure_level` via `sysctlbyname` for real-time thermal pressure
- Calculates total CPU usage across all cores using `mach_host_self()` and `host_processor_info`
- Appends timestamped entries to `~/Library/Logs/m4_sentinel.log`
- Triggers a native macOS notification when thermal pressure exceeds the threshold
- Runs silently in the background with `--daemon` mode (uses `fork()` + `setsid()`)
- Proper error handling for all Mach service calls
- Built with CoreFoundation and IOKit via a simple Makefile

---

## Requirements

- macOS 13+ on Apple Silicon (M1/M2/M3/M4)
- Xcode Command Line Tools (`xcode-select --install`)

---

## Build

```bash
git clone https://github.com/chakri192/m4-sentinel.git
cd m4-sentinel
make
```

This compiles `sentinel.c` and links CoreFoundation + IOKit, producing a `sentinel` binary.

---

## Usage

### Run interactively (foreground)

```bash
./sentinel
```

Useful for testing — logs and alerts print to stdout alongside the log file.

### Run as a background daemon

```bash
./sentinel --daemon
```

The process detaches from the terminal using `fork()` and `setsid()`. Logs continue writing to:

```
~/Library/Logs/m4_sentinel.log
```

### Stop the daemon

```bash
pkill sentinel
```

Or find the PID and kill it manually:

```bash
pgrep sentinel
kill <PID>
```

---

## Log Format

Each log entry looks like:

```
[2025-07-01 08:42:31]  thermal_pressure=0  cpu_load=34.7%
[2025-07-01 08:42:41]  thermal_pressure=1  cpu_load=78.2%  ⚠ ALERT SENT
```

Logs are appended — they persist across runs. Rotate manually if needed:

```bash
: > ~/Library/Logs/m4_sentinel.log   # clear the log
```

---

## How It Works

| Component        | API Used                                      |
|------------------|-----------------------------------------------|
| Thermal pressure | `sysctlbyname("kern.memorystatus_level", ...)` |
| CPU load         | `host_processor_info(mach_host_self(), ...)`  |
| Notifications    | `osascript` / CoreFoundation alert            |
| Daemon mode      | POSIX `fork()` + `setsid()`                   |
| Build system     | `Makefile` linking `-framework CoreFoundation -framework IOKit` |

---

## Project Structure

```
m4-sentinel/
├── sentinel.c     # Core monitor logic
├── Makefile       # Build and link instructions
├── .gitignore
└── README.md
```

---

