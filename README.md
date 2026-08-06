<div align="center">

# m4-sentinel

**A system monitoring daemon for Apple Silicon, written in C11.**

CPU load, thermal pressure, and memory pressure through native Mach, notify, and sysctl interfaces. One binary, no dependencies.

<p>
  <img alt="Language" src="https://img.shields.io/badge/C-C11-1c1c1e?style=flat-square&logo=c&logoColor=A8B9CC" />
  <img alt="Platform" src="https://img.shields.io/badge/Apple%20Silicon-macOS-1c1c1e?style=flat-square&logo=apple&logoColor=white" />
  <img alt="Size" src="https://img.shields.io/badge/291-lines-1c1c1e?style=flat-square" />
  <img alt="Dependencies" src="https://img.shields.io/badge/dependencies-none-1c1c1e?style=flat-square" />
</p>

</div>

---

## Installation

```sh
git clone https://github.com/chakri192/m4-sentinel.git
cd m4-sentinel && make
```

Requires macOS on Apple Silicon and the Xcode Command Line Tools.

## Usage

| Invocation | Behaviour |
|---|---|
| `./sentinel` | Single reading, printed and logged |
| `./sentinel --daemon` | Detaches and samples every 60 seconds |
| `./sentinel --log <path>` | Redirects the log |

Stop the daemon with `pkill sentinel`. Records are written to `~/Library/Logs/m4_sentinel.log`:

```
Thu Aug  6 01:27:19 2026 | Thermal: Nominal (0) | Memory: Normal (1) | CPU: 23.50%
```

## Thermal and memory are distinct sensors

They are frequently conflated, so this reads them from separate sources and labels them separately.

**Thermal** comes from the notify(3) key `libkern` publishes as `kOSThermalNotificationPressureLevelName`. Levels run Nominal, Moderate, Heavy, Trapping, Sleeping.

**Memory** comes from `sysctlbyname("kern.memorystatus_vm_pressure_level", …)`, reporting the constants dispatch exposes as `NORMAL`, `WARN`, and `CRITICAL`. That sysctl is the polling equivalent of `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE`; the dispatch source is the better interface generally, but it delivers on a queue, and a program structured as `check, sleep(60)` has no run loop to receive events.

Alerts are edge-triggered — raised when a level first crosses its threshold and again only if it rises further. A level-triggered check would repost the same notification every minute for the duration of the condition.

> Earlier revisions read `sysctlbyname("vm.pressure_level", …)` and reported it as thermal. That OID does not exist on current macOS, and where it once did it reported memory pressure.

## CPU load requires two samples

`host_processor_info` returns cumulative tick counters that increase monotonically since boot. A single reading yields the machine's average utilisation across its entire uptime — stable and uninformative.

`get_cpu_load()` samples twice, 200 ms apart, and divides the deltas:

```
load = Δ(user + system) / Δ(user + system + idle)
```

summed across every core. The kernel allocates the counter array on each call, so it is released with `mach_vm_deallocate`; omitting that leaks steadily in a long-running daemon.

## Daemonisation

```c
fflush(NULL);               // buffered output would otherwise be duplicated into the child
if (fork() != 0) exit(0);
setsid();                   // new session, no controlling terminal
chdir("/");                 // release the launch directory
freopen("/dev/null", ...)   // stdin, stdout, stderr
```

`fflush(NULL)` matters because a piped stdout is fully buffered, and `fork()` copies that buffer into the child, which emits it a second time. `chdir("/")` releases the working directory, which a daemon would otherwise hold open for its lifetime — and is why the log path is resolved to an absolute path beforehand.

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
