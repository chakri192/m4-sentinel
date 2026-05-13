# Sentinel System Monitor

## Overview

Sentinel is a C-based system monitor designed for macOS on Apple Silicon. It collects thermal pressure level and CPU load, logs the data to a file, and triggers alerts if necessary.

## Core Logic

- **Data Collection**: Uses `sysctlbyname` to poll `vm.pressure_level`.
- **CPU Load**: Calculates total CPU usage across all cores using `mach_host_self()` and `host_processor_info`.
- **Logging**: Appends timestamps and stats to `~/Library/Logs/m4_sentinel.log`.
- **Threshold Alert**: Triggers a macOS notification if thermal pressure is greater than 0.

## System Design

- **Daemon Mode**: Implements a `--daemon` flag that runs the monitor in the background using `fork()` and `setsid()`.
- **Build System**: Provides a `Makefile` to link the CoreFoundation and IOKit frameworks.
- **Professionalism**: Includes a README.md explaining the sysctl architecture and how to kill the daemon process.

## Constraints

- Uses C11 standards.
- Includes proper error handling for Mach service calls.

## Usage

To run Sentinel as a daemon:
