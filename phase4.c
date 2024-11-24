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

void term_read_handler(USLOSS_Sysargs* args)
{

}

void term_write_handler(USLOSS_Sysargs* args)
{

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