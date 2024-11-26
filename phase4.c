#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <usloss.h>
#include <stdio.h>
#include <string.h>

// Data Structures

typedef struct ShadowProcess {
    int pid;
    int target_time;

    struct ShadowProcess* next;
} ShadowProcess;

ShadowProcess process_table[MAXPROC];
ShadowProcess* sleep_queue;

// Helper Functions

int sleep_daemon_process()
{
    while(1)
    {
        // Wait for clock interrupt to fire
        int status;
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);

        // Loop through and wakeup any sleeping processes that have reached their target time
        int has_proc = 1;
        while(has_proc)
        {
            ShadowProcess* sleep_proc = sleep_queue;

            // Stop looping if there are no more sleeping processes
            if(sleep_proc == NULL)
                has_proc = 0;
            else
            {
                int current_time = currentTime();

                if(sleep_proc->target_time <= current_time)
                {
                    // Wakeup the process
                    unblockProc(sleep_proc->pid);

                    // Remove from queue and clear process data
                    sleep_queue = sleep_proc->next;
                    memset(sleep_proc, 0, sizeof(ShadowProcess));
                }
                else // Next process is not ready to wake up, so stop looping
                    has_proc = 0;
            }
        }
    }

    return 0; // Should never reach this point
}

int terminal_daemon_process()
{
    return 0;
}

// Syscall Handlers

void sleep_handler(USLOSS_Sysargs* args)
{
    int seconds = (int)(long)args->arg1;

    if(seconds < 0) // Invalid number of seconds
        args->arg4 = (void*)-1;
    else
    {
        // Setup shadow process struct
        ShadowProcess* sleep_proc = &process_table[getpid() % MAXPROC];
        sleep_proc->pid = getpid();

        // Calculate target time
        int current_time = currentTime(); // µs
        sleep_proc->target_time = current_time + seconds * 1000 * 1000;

        // Add to sleep queue
        if(sleep_queue == NULL)
            sleep_queue = sleep_proc;
        else
        {
            ShadowProcess* current = sleep_queue;
            while(current->next != NULL)
                current = current->next;

            current->next = sleep_proc;
        }

        // Block the process
        blockMe();

        args->arg4 = (void*)0;
    }
}

void term_read_handler(USLOSS_Sysargs* args) {
    int unit = (int)(long)args->arg3; // Terminal number
    char *buffer = (char *)args->arg1; // User buffer
    int bufSize = (int)(long)args->arg2; // Buffer size

    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buffer == NULL || bufSize <= 0) {
        args->arg4 = (void *)-1; // Invalid arguments
        return;
    }

    // Wait for a line to be available
    int status;
    waitDevice(USLOSS_TERM_DEV, unit, &status);

    // Check status register for available input
    if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
        // Read character
        char c = USLOSS_TERM_STAT_CHAR(status);
        static char lineBuffer[MAXLINE];
        static int lineIndex = 0;

        lineBuffer[lineIndex++] = c;

        // Handle newline or buffer full
        if (c == '\n' || lineIndex >= MAXLINE) {
            strncpy(buffer, lineBuffer, bufSize);
            args->arg2 = (void *)(long)lineIndex; // Characters read
            lineIndex = 0; // Reset buffer
        } else {
            // Wait for more input
            blockMe();
        }
    }

    args->arg4 = (void *)0; // Success
}

void term_write_handler(USLOSS_Sysargs* args) {
    int unit = (int)(long)args->arg3; // Terminal number
    char *buffer = (char *)args->arg1; // User buffer
    int bufSize = (int)(long)args->arg2; // Buffer size

    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buffer == NULL || bufSize <= 0) {
        args->arg4 = (void *)-1; // Invalid arguments
        return;
    }

    for (int i = 0; i < bufSize; i++) {
        int status;
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        // Check if terminal is ready for output
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            // Write character
            int control = 0x1 | (buffer[i] << 8); // Send char
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)control);
        } else {
            // Terminal not ready, block and retry
            blockMe();
        }
    }

    args->arg2 = (void *)(long)bufSize; // Characters written
    args->arg4 = (void *)0; // Success
}

// Phase 4 Functions

void phase4_init()
{
    // Setup sleeping process structs
    sleep_queue = NULL;
    memset(process_table, 0, sizeof(ShadowProcess) * MAXPROC);

    // Assign syscall handlers
    systemCallVec[SYS_SLEEP] = sleep_handler;
    systemCallVec[SYS_TERMREAD] = term_read_handler;
    systemCallVec[SYS_TERMWRITE] = term_write_handler;
}

void phase4_start_service_processes()
{
    int sleep_daemon_pid = spork("Sleep Daemon Process", sleep_daemon_process, NULL, USLOSS_MIN_STACK, 3);
    int terminal_daemon_pid = spork("Terminal Daemon Process", terminal_daemon_process, NULL, USLOSS_MIN_STACK, 3);
}