#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#include <notify.h>
#include <libkern/OSThermalNotification.h>

// Default log lives under the user's Library, not the cwd: a daemon chdir()s
// to / and must not depend on wherever it happened to be launched from.
// Override with --log <path> or SENTINEL_LOG=<path> when debugging.
#define DEFAULT_LOG_RELATIVE "Library/Logs/m4_sentinel.log"
#define FALLBACK_LOG_PATH    "/tmp/m4_sentinel.log"

// Memory pressure levels, as reported by kern.memorystatus_vm_pressure_level.
// These are the same constants dispatch publishes as
// DISPATCH_MEMORYPRESSURE_{NORMAL,WARN,CRITICAL} — the sysctl is the polling
// equivalent of DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, which this loop cannot
// use because it has no run loop to deliver events on.
#define MEMPRESSURE_NORMAL   1
#define MEMPRESSURE_WARN     2
#define MEMPRESSURE_CRITICAL 4

// Alert thresholds, expressed in each API's own level constants.
#define THERMAL_ALERT_LEVEL     kOSThermalPressureLevelModerate
#define MEMPRESSURE_ALERT_LEVEL MEMPRESSURE_WARN

// Returned by the sensor readers when the value could not be obtained.
#define LEVEL_UNAVAILABLE (-1)

static char g_log_path[PATH_MAX];

static const char *thermal_level_name(int level) {
    switch (level) {
        case kOSThermalPressureLevelNominal:  return "Nominal";
        case kOSThermalPressureLevelModerate: return "Moderate";
        case kOSThermalPressureLevelHeavy:    return "Heavy";
        case kOSThermalPressureLevelTrapping: return "Trapping";
        case kOSThermalPressureLevelSleeping: return "Sleeping";
        default:                              return "Unavailable";
    }
}

static const char *mempressure_level_name(int level) {
    switch (level) {
        case MEMPRESSURE_NORMAL:   return "Normal";
        case MEMPRESSURE_WARN:     return "Warn";
        case MEMPRESSURE_CRITICAL: return "Critical";
        default:                   return "Unavailable";
    }
}

// Genuine thermal pressure, via the notify(3) key libkern publishes as
// kOSThermalNotificationPressureLevelName ("com.apple.system.thermalpressurelevel").
// This is the documented polling path on Apple Silicon; the old
// sysctlbyname("vm.pressure_level") OID does not exist on modern macOS, and
// was memory pressure rather than thermal even when it did.
static int get_thermal_pressure(void) {
    static int token = -1;
    static int registered = 0;

    if (!registered) {
        if (notify_register_check(kOSThermalNotificationPressureLevelName, &token) != NOTIFY_STATUS_OK) {
            fprintf(stderr, "notify_register_check(%s) failed\n", kOSThermalNotificationPressureLevelName);
            return LEVEL_UNAVAILABLE;
        }
        registered = 1;
    }

    uint64_t state = 0;
    if (notify_get_state(token, &state) != NOTIFY_STATUS_OK) return LEVEL_UNAVAILABLE;
    return (int)state;
}

// Memory pressure, via the sysctl that backs the dispatch memory-pressure
// source. Levels are MEMPRESSURE_* above.
static int get_memory_pressure(void) {
    int level = 0;
    size_t len = sizeof(level);
    if (sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &len, NULL, 0) != 0) {
        perror("sysctl kern.memorystatus_vm_pressure_level");
        return LEVEL_UNAVAILABLE;
    }
    return level;
}

void log_stats(int thermal_level, int mem_level, double cpu_load) {
    printf("--- Sentinel Update: Thermal %s, Memory %s, CPU %.2f%% ---\n",
           thermal_level_name(thermal_level), mempressure_level_name(mem_level), cpu_load * 100);

    FILE *log_file = fopen(g_log_path, "a");
    if (log_file) {
        time_t now = time(NULL);
        char *ts = ctime(&now);
        ts[strlen(ts) - 1] = '\0';
        fprintf(log_file, "%s | Thermal: %s (%d) | Memory: %s (%d) | CPU: %.2f%%\n",
                ts,
                thermal_level_name(thermal_level), thermal_level,
                mempressure_level_name(mem_level), mem_level,
                cpu_load * 100);
        fclose(log_file);
        printf("Successfully logged to %s\n", g_log_path);
    } else {
        perror("FAILED to open log file");
    }
}

// Reads the *cumulative* (since-boot) busy and idle CPU tick totals.
// Returns 0 on success, -1 on failure. host_processor_info counters are
// monotonic totals, so a single reading is a since-boot average — the
// caller must diff two samples to get an instantaneous load.
static int read_cpu_ticks(double *out_used, double *out_idle) {
    host_t host = mach_host_self();
    natural_t count;
    processor_info_array_t info;
    mach_msg_type_number_t info_count;

    kern_return_t kr = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &count, &info, &info_count);
    if (kr != KERN_SUCCESS) return -1;

    double total_user = 0, total_system = 0, total_idle = 0;
    processor_cpu_load_info_t cpu_info = (processor_cpu_load_info_t)info;

    for (natural_t i = 0; i < count; i++) {
        total_user   += cpu_info[i].cpu_ticks[CPU_STATE_USER];
        total_system += cpu_info[i].cpu_ticks[CPU_STATE_SYSTEM];
        total_idle   += cpu_info[i].cpu_ticks[CPU_STATE_IDLE];
    }

    mach_vm_deallocate(mach_task_self(), (vm_address_t)info, info_count * sizeof(integer_t));

    *out_used = total_user + total_system;
    *out_idle = total_idle;
    return 0;
}

// Instantaneous CPU utilisation (0..1) measured over a short window, by
// diffing two tick samples. A single host_processor_info reading only
// yields the average since boot, which barely moves — this returns the
// real current load.
double get_cpu_load() {
    double used1, idle1, used2, idle2;
    if (read_cpu_ticks(&used1, &idle1) != 0) return -1.0;
    usleep(200000);  // 200 ms sampling window
    if (read_cpu_ticks(&used2, &idle2) != 0) return -1.0;

    double used_delta = used2 - used1;
    double idle_delta = idle2 - idle1;
    double total_delta = used_delta + idle_delta;
    if (total_delta <= 0) return 0.0;  // no ticks elapsed
    return used_delta / total_delta;
}

static void notify_user(const char *message) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'display notification \"%s\" with title \"Sentinel Alert\"'", message);
    system(cmd);
}

// Alerts are edge-triggered: notify when a level first rises to or past its
// threshold, and again only if it climbs higher. Level-triggering would fire
// an identical notification every pass for as long as the condition lasts.
static void maybe_alert(int thermal_level, int mem_level) {
    static int last_thermal = kOSThermalPressureLevelNominal;
    static int last_mem = MEMPRESSURE_NORMAL;
    char message[256];

    if (thermal_level >= THERMAL_ALERT_LEVEL && thermal_level > last_thermal) {
        snprintf(message, sizeof(message), "Thermal pressure: %s", thermal_level_name(thermal_level));
        notify_user(message);
    }
    if (thermal_level != LEVEL_UNAVAILABLE) last_thermal = thermal_level;

    if (mem_level >= MEMPRESSURE_ALERT_LEVEL && mem_level > last_mem) {
        snprintf(message, sizeof(message), "Memory pressure: %s", mempressure_level_name(mem_level));
        notify_user(message);
    }
    if (mem_level != LEVEL_UNAVAILABLE) last_mem = mem_level;
}

void check_system() {
    int thermal_level = get_thermal_pressure();
    int mem_level = get_memory_pressure();
    double cpu_load = get_cpu_load();

    log_stats(thermal_level, mem_level, cpu_load);
    maybe_alert(thermal_level, mem_level);
}

// Resolves the log path once, before any fork/chdir, so a relative override
// still means "relative to where the user ran this".
static void resolve_log_path(const char *override) {
    if (override && *override) {
        if (override[0] == '/') {
            snprintf(g_log_path, sizeof(g_log_path), "%s", override);
        } else {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                snprintf(g_log_path, sizeof(g_log_path), "%s/%s", cwd, override);
            } else {
                snprintf(g_log_path, sizeof(g_log_path), "%s", override);
            }
        }
        return;
    }

    const char *home = getenv("HOME");
    if (!home || !*home) {
        fprintf(stderr, "HOME unset; logging to %s\n", FALLBACK_LOG_PATH);
        snprintf(g_log_path, sizeof(g_log_path), "%s", FALLBACK_LOG_PATH);
        return;
    }

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/Library/Logs", home);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) perror("mkdir ~/Library/Logs");

    snprintf(g_log_path, sizeof(g_log_path), "%s/%s", home, DEFAULT_LOG_RELATIVE);
}

void run_daemon() {
    // Drain stdout first: if it is a pipe or file it is fully buffered, and
    // fork() would hand the child a copy of the pending bytes to print again.
    fflush(NULL);
    if (fork() != 0) exit(0);
    setsid();
    // Release the launch directory so it can be unmounted or deleted. The log
    // path was already resolved to an absolute path before this point.
    if (chdir("/") != 0) perror("chdir /");
    // Detach stdio: after setsid() the controlling terminal is gone, so
    // printf/perror would otherwise write to a defunct fd. Send them to
    // /dev/null (stdout/stderr) — the log file is written separately.
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    while (1) {
        check_system();
        sleep(60);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--daemon] [--log <path>]\n"
            "  --daemon      detach and sample every 60s\n"
            "  --log <path>  write to <path> instead of ~/%s\n"
            "                (SENTINEL_LOG=<path> does the same)\n",
            argv0, DEFAULT_LOG_RELATIVE);
}

int main(int argc, char *argv[]) {
    int daemonize = 0;
    const char *log_override = getenv("SENTINEL_LOG");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0) {
            daemonize = 1;
        } else if (strcmp(argv[i], "--log") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--log requires a path\n");
                return 1;
            }
            log_override = argv[++i];
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    resolve_log_path(log_override);

    if (daemonize) {
        printf("Starting Sentinel Daemon, logging to %s\n", g_log_path);
        run_daemon();
    } else {
        printf("Running Sentinel One-Shot Check...\n");
        check_system();
    }
    return 0;
}
