#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *a){fprintf(stderr,"Usage: %s <cmd> [args]\n",a); exit(1);}
static double d(struct timespec a, struct timespec b){
    return (b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9;
}

int main(int c, char **v) {
    // Check for at least one argument (the command to run)
    if (c < 2) usage(v[0]);
    // Start time before creating child
    struct timespec t0, t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
        perror("clock_gettime");
        return 1;
    }
    // Create a child process
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    // CHILD: Execute the command given in v[1] with arguments v[1], v[2], ...
    if (pid == 0) {
        execvp(v[1], &v[1]);
        perror("execvp");
        _exit(127);
    }
    // PARENT: Wait for the child to finish.
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    // End time after child has finished
    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
        perror("clock_gettime");
        return 1;
    }
    // Compute elapsed time
    double elapsed = d(t0, t1);
    // Check how the child terminated and print results
    if (WIFEXITED(status)) {
        printf("pid=%d elapsed=%.3f exit=%d\n", (int)pid, elapsed, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("pid=%d elapsed=%.3f signal=%d\n", (int)pid, elapsed, WTERMSIG(status));
    } else {
        printf("pid=%d elapsed=%.3f exit=%d\n", (int)pid, elapsed, 1);
    }

    return 0;
}

