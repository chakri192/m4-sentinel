<div align="center">

<img src="docs/watch.svg" width="840" alt="" />

# m4-sentinel

**A system monitoring daemon for Apple Silicon, written in C11.**

Reads CPU load, thermal pressure, and memory pressure through native Mach, notify, and sysctl interfaces. No dependencies and no runtime — a single binary and a log file.

<p>
  <img alt="Language" src="https://img.shields.io/badge/C-C11-1c1c1e?style=flat-square&logo=c&logoColor=A8B9CC" />
  <img alt="Platform" src="https://img.shields.io/badge/Apple%20Silicon-macOS-1c1c1e?style=flat-square&logo=apple&logoColor=white" />
  <img alt="Size" src="https://img.shields.io/badge/291-lines-1c1c1e?style=flat-square" />
  <img alt="Dependencies" src="https://img.shields.io/badge/dependencies-none-1c1c1e?style=flat-square" />
</p>

</div>

---

## Overview

m4-sentinel samples three system metrics on a fixed interval, appends a timestamped record to a log file, and raises a system notification when either pressure level crosses its threshold. It is written against the native APIs directly, with no supporting libraries.

## Requirements

macOS on Apple Silicon, and the Xcode Command Line Tools (`xcode-select --install`). Full Xcode is not required.

## Installation

```sh
git clone https://github.com/chakri192/m4-sentinel.git
cd m4-sentinel && make
```

The `Makefile` compiles with `-std=c11 -Os -Wall -Wextra` and links CoreFoundation and IOKit.

## Usage

| Invocation | Behaviour |
|---|---|
| `./sentinel` | Single reading; prints to stdout, appends one record, exits |
| `./sentinel --daemon` | Detaches and samples every 60 seconds |
| `./sentinel --log <path>` | Redirects the log; combines with `--daemon` |

Terminate the daemon with `pkill sentinel`.

## Metrics

### Thermal and memory pressure are distinct

These two are frequently conflated. m4-sentinel reads them from separate sources and reports them under separate labels.

**Thermal pressure** is obtained from the notify(3) key published by `libkern` as `kOSThermalNotificationPressureLevelName` (`com.apple.system.thermalpressurelevel`). A check token is registered once, then queried with `notify_get_state()` on each pass.

| Value | Level |
|---|---|
| 0 | `kOSThermalPressureLevelNominal` |
| 1 | `kOSThermalPressureLevelModerate` |
| 2 | `kOSThermalPressureLevelHeavy` |
| 3 | `kOSThermalPressureLevelTrapping` |
| 4 | `kOSThermalPressureLevelSleeping` |

**Memory pressure** is obtained from `sysctlbyname("kern.memorystatus_vm_pressure_level", …)`, which reports the constants exposed by dispatch as `DISPATCH_MEMORYPRESSURE_NORMAL` (1), `WARN` (2), and `CRITICAL` (4). This sysctl is the polling equivalent of `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE`. The dispatch source is generally the better interface, but it delivers events on a queue, and a program structured as `check, sleep(60)` has no run loop to receive them.

> **Historical note.** Earlier revisions read `sysctlbyname("vm.pressure_level", …)` and reported the result as `Thermal:`. This was incorrect in two respects: the OID does not exist on current macOS, so the read failed on every pass and the value defaulted to `0`; and where it did once exist, it reported memory pressure rather than thermal pressure.

### CPU load requires two samples

`host_processor_info` returns cumulative per-core tick counters — user, system, and idle — which increase monotonically since boot. A single reading therefore yields the machine's average utilisation across its entire uptime, a figure that is both stable and uninformative.

`get_cpu_load()` takes two samples 200 ms apart and divides the deltas:

```
load = Δ(user + system) / Δ(user + system + idle)
```

summed across every core, which on an M-series processor includes both performance and efficiency cores. The kernel allocates the counter array on each call, so it is released with `mach_vm_deallocate`; omitting this causes a steady leak in a long-running daemon.

## Alerting

Alerts are edge-triggered: a notification is raised when a level first rises to or beyond its threshold, and again only if it rises further. A level-triggered check would repost an identical notification every 60 seconds for the duration of the condition.

| Sensor | Threshold |
|---|---|
| Thermal | `>= kOSThermalPressureLevelModerate` |
| Memory | `>= DISPATCH_MEMORYPRESSURE_WARN` |

A sensor that cannot be read is logged as `Unavailable (-1)` and never triggers an alert.

## Logging

Records are written to `~/Library/Logs/m4_sentinel.log`, with `$HOME` expanded at runtime and the directory created if absent.

```
Thu Aug  6 01:27:19 2026 | Thermal: Nominal (0) | Memory: Normal (1) | CPU: 23.50%
```

Timestamps use `ctime()`; each level is printed as both name and raw value. The log is appended and never rotated — clear it with `: > ~/Library/Logs/m4_sentinel.log`.

`--log <path>` or `SENTINEL_LOG` redirects it, with `--log` taking precedence. Relative paths are resolved against the invoking working directory before any fork occurs, so `--log ./sentinel.log --daemon` writes where the command was issued rather than following the daemon to `/`. If `$HOME` is unset, the path falls back to `/tmp/m4_sentinel.log` with a notice on stderr.

## Daemonisation

```c
fflush(NULL);               // avoid duplicating buffered output into the child
if (fork() != 0) exit(0);   // the parent exits; the child is reparented to init
setsid();                   // new session, no controlling terminal
chdir("/");                 // release the launch directory
freopen("/dev/null", ...)   // stdin, stdout, stderr
```

Three of these four steps are commonly omitted.

`fflush(NULL)` is required because when stdout is a pipe or file rather than a terminal it is fully buffered. Any output produced before the fork remains in the buffer, and `fork()` copies that buffer into the child, which emits it a second time.

`chdir("/")` matters because a process holds a reference to its working directory. A daemon started from a removable volume or a directory intended for deletion keeps that directory in use for its lifetime. This is also why the log path must be resolved to an absolute path beforehand.

`freopen` is last because after `setsid()` the controlling terminal no longer exists, and every `printf` and `perror` in the program would otherwise write to a file descriptor that leads nowhere.

## Project structure

```
m4-sentinel/
├── sentinel.c     read_cpu_ticks · get_cpu_load · check_system · run_daemon
└── Makefile       gcc -std=c11 -Os, links CoreFoundation and IOKit
```

## Contributors

| | |
|---|---|
| [chakri192](https://github.com/chakri192) | Author |
| [aider](https://github.com/Aider-AI/aider) | AI pair programmer |
