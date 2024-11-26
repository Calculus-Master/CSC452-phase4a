#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase3_kernelInterfaces.h>
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

typedef struct TerminalData {
    // Reading
    int mailbox_id; // Holds the 10 terminal buffers
    char working_buffer[MAXLINE + 1]; // Current working buffer
    int working_buffer_index; // Index of first free space in working buffer

    // Writing
    int write_semaphore_id; // Semaphore lock for terminal write operations
} TerminalData;

TerminalData terminal_data[USLOSS_TERM_UNITS];

// Helper Functions

void clear_working_buffer(TerminalData* data)
{
    memset(data->working_buffer, 0, MAXLINE + 1);
    data->working_buffer_index = 0;
}

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

int terminal_daemon_process(void* arg)
{
    int unit = (int)(long)arg;
    TerminalData* term = &terminal_data[unit];

    while(1)
    {
        // Wait for the current terminal interrupt to fire
        int status;
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        // Handle reads
        int read_status = USLOSS_TERM_STAT_RECV(status);

        if(read_status == USLOSS_DEV_ERROR) // Should not happen, but this is a failsafe
        {
            USLOSS_Console("Terminal %d (Read): Error from waitDevice status.\n", unit);
            USLOSS_Halt(USLOSS_DEV_ERROR);
        }
        else if(status == USLOSS_DEV_READY) {} // Do nothing for reads
        else if(status == USLOSS_DEV_BUSY)
        {
            char c = USLOSS_TERM_STAT_CHAR(status);

            term->working_buffer[term->working_buffer_index++] = c;

            // End of line reached
            if(c == 0 || c == '\n' || term->working_buffer_index == MAXLINE)
            {
                // Append a null terminator
                term->working_buffer[term->working_buffer_index] = '\0';

                // Send the line to the mailbox (if mailbox is full, it gets deleted)
                MboxCondSend(term->mailbox_id, term->working_buffer, MAXLINE + 1);

                // Clear the working buffer
                clear_working_buffer(term);
            }
        }

        // Handle writes
        int write_status = USLOSS_TERM_STAT_XMIT(status);

        if(write_status == USLOSS_DEV_ERROR) // Should not happen, but this is a failsafe
        {
            USLOSS_Console("Terminal %d (Write): Error from waitDevice status.\n", unit);
            USLOSS_Halt(USLOSS_DEV_ERROR);
        }
        else if(write_status == USLOSS_DEV_BUSY) {} // Do nothing for writes
        else if(write_status == USLOSS_DEV_READY)
        {

        }
    }

    return 0; // Should never reach this point
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
    int unit = (int)(long)args->arg3; // Terminal number
    char *buffer = (char *)args->arg1; // User buffer
    int bufSize = (int)(long)args->arg2; // Buffer size

    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buffer == NULL || bufSize <= 0)
    {
        args->arg2 = (void *)0; // No characters read
        args->arg4 = (void *)-1; // Invalid arguments
        return;
    }

    TerminalData* term = &terminal_data[unit];

    // Reads any available lines, or blocks until the daemon process sends a line
    MboxRecv(term->mailbox_id, buffer, bufSize);

    args->arg2 = (void *)(long)strlen(buffer); // Characters read
    args->arg4 = (void *)0; // Success
}

void term_write_handler(USLOSS_Sysargs* args) {
    int unit = (int)(long)args->arg3; // Terminal number

    // Grab semaphore lock
    kernSemP(terminal_write_semaphores[unit]);

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

    // Release semaphore lock
    kernSemV(terminal_write_semaphores[unit]);
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

    // Setup terminal r/w data structures
    memset(terminal_data, 0, sizeof(TerminalData) * USLOSS_TERM_UNITS);
    for(int i = 0; i < 4; i++)
    {
        TerminalData* data = &terminal_data[i];

        // Create Terminal Read Mailbox
        int mbox_id = MboxCreate(10, MAXLINE + 1);
        data->mailbox_id = mbox_id;

        // Empty Working Buffers
        clear_working_buffer(data);

        // Create Terminal Write Semaphores
        kernSemCreate(1, &data->write_semaphore_id);
    }
}

void phase4_start_service_processes()
{
    spork("Sleep Daemon", sleep_daemon_process, NULL, USLOSS_MIN_STACK, 1);

    spork("Terminal 0 Daemon Process", terminal_daemon_process, 0, USLOSS_MIN_STACK, 1);
    spork("Terminal 1 Daemon Process", terminal_daemon_process, 1, USLOSS_MIN_STACK, 1);
    spork("Terminal 2 Daemon Process", terminal_daemon_process, 2, USLOSS_MIN_STACK, 1);
    spork("Terminal 3 Daemon Process", terminal_daemon_process, 3, USLOSS_MIN_STACK, 1);
}