#include "common.h"              
#include <ctype.h>          
#include <errno.h>              
#include <stdio.h>              
#include <stdlib.h>              
#include <string.h>              
#include <unistd.h>              


static void usage(const char *a){fprintf(stderr,"Usage: %s <pid>\n",a); exit(1);}
static int isnum(const char*s){for(;*s;s++) if(!isdigit(*s)) return 0; return 1;}

/*
 * Handle errors opening /proc files.
 */
static void open_fail(const char *path) {
    if (errno == ENOENT) {
        fprintf(stderr, "Error: PID not found (missing %s)\n", path);
    } else if (errno == EACCES) {
        fprintf(stderr, "Error: Permission denied reading %s\n", path);
    } else {
        fprintf(stderr, "Error: Could not open %s: %s\n", path, strerror(errno));
    }
    exit(1);
}


static int parse_processor_from_rest(const char *rest) {
   
    const int want_index = 36;
    char *tmp = strdup(rest);
    if (!tmp) return -1;

    int idx = 0;
    char *save = NULL;
    char *tok = strtok_r(tmp, " ", &save);

    while (tok) {
        if (idx == want_index) {
            int cpu = atoi(tok);
            free(tmp);
            return cpu;
        }
        idx++;
        tok = strtok_r(NULL, " ", &save);
    }

    free(tmp);
    return -1; // not found 
}

static void read_stat(const char *path,
                      char *out_state,
                      long *out_ppid,
                      unsigned long long *out_utime,
                      unsigned long long *out_stime,
                      int *out_processor) {

    // Open /proc/<pid>/stat
    FILE *f = fopen(path, "r");
    if (!f) open_fail(path);

    // Read the whole line
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, f);
    fclose(f);

    // If getline fails, something is wrong
    if (n <= 0) {
        fprintf(stderr, "Error: Failed to read %s\n", path);
        free(line);
        exit(1);
    }

    
    char *rp = strrchr(line, ')');
    if (!rp || rp[1] != ' ') {
        fprintf(stderr, "Error: Unexpected format in %s\n", path);
        free(line);
        exit(1);
    }

    
    char *rest = rp + 2;

    
    char state = 0;
    long ppid = -1;

    // We scan fields 4..15 by skipping what we don't need
    long pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
    unsigned long long flags = 0, minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;
    unsigned long long utime = 0, stime = 0;

    int got = sscanf(rest,
                     "%c %ld %ld %ld %ld %ld %llu %llu %llu %llu %llu %llu %llu",
                     &state,
                     &ppid, &pgrp, &session, &tty_nr, &tpgid,
                     &flags, &minflt, &cminflt, &majflt, &cmajflt,
                     &utime, &stime);

    if (got != 13) {
        fprintf(stderr, "Error: Could not parse %s (got %d fields)\n", path, got);
        free(line);
        exit(1);
    }

    // Processor number comes later in the stat line 
    int processor = parse_processor_from_rest(rest);

    // Send results back to caller
    *out_state = state;
    *out_ppid = ppid;
    *out_utime = utime;
    *out_stime = stime;
    *out_processor = processor;

    free(line);
}

/*
 * /proc/<pid>/cmdline contains the command line arguments,
 * separated by NUL bytes. We read it and convert to spaces.
 */
static void read_cmdline(const char *path, char *buf, size_t bufsz) {
    FILE *f = fopen(path, "r");
    if (!f) open_fail(path);

    // Read up to bufsz-1 so we can NUL-terminate safely
    size_t n = fread(buf, 1, bufsz - 1, f);
    fclose(f);

    buf[n] = '\0';  // make it a valid C-string

    
    if (n == 0) {
        snprintf(buf, bufsz, "(empty)");
        return;
    }

    // Replace NUL bytes with spaces
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\0') buf[i] = ' ';
    }

    // Trim trailing spaces
    while (n > 0 && buf[n - 1] == ' ') {
        buf[n - 1] = '\0';
        n--;
    }
}

/*
 * Read VmRSS from /proc/<pid>/status.
 * Returns -1 if not found.
 */
static long read_vmrss_kb(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) open_fail(path);

    char *line = NULL;
    size_t cap = 0;
    long rss = -1;

    while (getline(&line, &cap, f) > 0) {
        
        if (strncmp(line, "VmRSS:", 6) == 0) {
            char *p = line + 6;

            // Skip whitespace
            while (*p && !isdigit((unsigned char)*p)) p++;
            if (*p) rss = strtol(p, NULL, 10);
            break;
        }
    }

    free(line);
    fclose(f);
    return rss; // -1 means not found
}

int main(int argc, char **argv) {
    if (argc != 2 || !isnum(argv[1])) usage(argv[0]);

    const char *pid = argv[1];

    char stat_path[256], status_path[256], cmdline_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", pid);
    snprintf(status_path, sizeof(status_path), "/proc/%s/status", pid);
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", pid);

    char state = 0;
    long ppid = -1;
    unsigned long long utime = 0, stime = 0;
    int processor = -1;

    // Read /proc/<pid>/stat (state, ppid, utime, stime, processor)
    read_stat(stat_path, &state, &ppid, &utime, &stime, &processor);

    // Read /proc/<pid>/cmdline (command + args)
    char cmdline[4096];
    read_cmdline(cmdline_path, cmdline, sizeof(cmdline));

    // Read VmRSS from /proc/<pid>/status
    long vmrss_kb = read_vmrss_kb(status_path);

    // Convert CPU time from ticks to seconds
    long ticks = sysconf(_SC_CLK_TCK);
    double cpu_sec = 0.0;
    if (ticks > 0) cpu_sec = (utime + stime) / (double)ticks;

    // Print results
    printf("PID: %s\n", pid);
    printf("State: %c\n", state);
    printf("PPID: %ld\n", ppid);
    printf("Cmd: %s\n", cmdline);
    printf("CPU: %d %.3f\n", processor, cpu_sec);
    printf("VmRSS: %ld\n", vmrss_kb);

    return 0;
}