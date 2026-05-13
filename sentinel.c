#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#include <os/log.h>

#define LOG_FILE "~/Library/Logs/m4_sentinel.log"

void log_stats(int pressure_level, double cpu_load) {
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[strlen(timestamp) - 1] = '\0'; // Remove newline character

    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        os_log(OS_LOG_DEFAULT, "Failed to open log file: %s", LOG_FILE);
        return;
    }

    fprintf(log_file, "%s - Pressure Level: %d, CPU Load: %.2f\n", timestamp, pressure_level, cpu_load);
    fclose(log_file);
}

double get_cpu_load() {
    host_t host = mach_host_self();
    natural_t count;
    processor_info_array_t info;
    mach_msg_type_number_t info_count;

    kern_return_t kr = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &count, (processor_info_array_t *)&info, &info_count);
    if (kr != KERN_SUCCESS) {
        os_log(OS_LOG_DEFAULT, "Failed to get CPU load: %d", kr);
        return -1;
    }

    double total_user = 0, total_system = 0, total_idle = 0;

    for (natural_t i = 0; i < count; i++) {
        total_user += info[i].cpu_ticks[CPU_STATE_USER];
        total_system += info[i].cpu_ticks[CPU_STATE_SYSTEM];
        total_idle += info[i].cpu_ticks[CPU_STATE_IDLE];
    }

    mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)info, count * sizeof(processor_info_data_t));

    return (total_user + total_system) / (total_user + total_system + total_idle);
}

void check_thermal_pressure() {
    int pressure_level;
    size_t len = sizeof(pressure_level);

    if (sysctlbyname("vm.pressure_level", &pressure_level, &len, NULL, 0) == -1) {
        os_log(OS_LOG_DEFAULT, "Failed to get thermal pressure level");
        return;
    }

    double cpu_load = get_cpu_load();
    if (cpu_load < 0) {
        return;
    }

    log_stats(pressure_level, cpu_load);

    if (pressure_level > 0) {
        char command[256];
        snprintf(command, sizeof(command), "osascript -e 'display notification \"Thermal pressure detected: Level %d\" with title \"Sentinel Alert\"'", pressure_level);
        system(command);
    }
}

void run_daemon() {
    pid_t pid = fork();
    if (pid == -1) {
        os_log(OS_LOG_DEFAULT, "Failed to fork");
        exit(EXIT_FAILURE);
    } else if (pid != 0) {
        exit(EXIT_SUCCESS); // Parent process exits
    }

    setsid();

    while (1) {
        check_thermal_pressure();
        sleep(60); // Check every minute
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--daemon") == 0) {
        run_daemon();
    } else {
        check_thermal_pressure();
    }

    return 0;
}
