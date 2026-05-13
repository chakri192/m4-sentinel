#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/mach_vm.h>

// Using local path for immediate visibility
#define LOG_FILE "sentinel.log"

void log_stats(int pressure_level, double cpu_load) {
    printf("--- Sentinel Update: Pressure %d, CPU %.2f ---\n", pressure_level, cpu_load);
    
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        time_t now = time(NULL);
        char *ts = ctime(&now);
        ts[strlen(ts) - 1] = '\0';
        fprintf(log_file, "%s | Thermal: %d | CPU: %.2f%%\n", ts, pressure_level, cpu_load * 100);
        fclose(log_file);
        printf("Successfully logged to %s\n", LOG_FILE);
    } else {
        perror("FAILED to open log file");
    }
}

double get_cpu_load() {
    host_t host = mach_host_self();
    natural_t count;
    processor_info_array_t info;
    mach_msg_type_number_t info_count;

    kern_return_t kr = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &count, &info, &info_count);
    if (kr != KERN_SUCCESS) return -1.0;

    double total_user = 0, total_system = 0, total_idle = 0;
    processor_cpu_load_info_t cpu_info = (processor_cpu_load_info_t)info;

    for (natural_t i = 0; i < count; i++) {
        total_user   += cpu_info[i].cpu_ticks[CPU_STATE_USER];
        total_system += cpu_info[i].cpu_ticks[CPU_STATE_SYSTEM];
        total_idle   += cpu_info[i].cpu_ticks[CPU_STATE_IDLE];
    }

    mach_vm_deallocate(mach_task_self(), (vm_address_t)info, info_count * sizeof(int));
    
    double used = total_user + total_system;
    return used / (used + total_idle);
}

void check_system() {
    int pressure_level = 0;
    size_t len = sizeof(pressure_level);
    sysctlbyname("vm.pressure_level", &pressure_level, &len, NULL, 0);

    double cpu_load = get_cpu_load();
    log_stats(pressure_level, cpu_load);

    if (pressure_level > 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "osascript -e 'display notification \"M4 Thermal Pressure: Level %d\" with title \"Sentinel Alert\"'", pressure_level);
        system(cmd);
    }
}

void run_daemon() {
    if (fork() != 0) exit(0);
    setsid();
    while (1) {
        check_system();
        sleep(60);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--daemon") == 0) {
        printf("Starting Sentinel Daemon...\n");
        run_daemon();
    } else {
        printf("Running Sentinel One-Shot Check...\n");
        check_system();
    }
    return 0;
}