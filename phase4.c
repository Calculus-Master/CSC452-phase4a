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
    int read_mbox_id; // Holds the 10 terminal buffers
    int read_semaphore_id; // Semaphore lock for terminal read operations
    char working_buffer[MAXLINE + 1]; // Current working buffer
    int working_buffer_index; // Index of first free space in working buffer
    int deliver_size; // Size of the target buffer to read into
    int is_reading; // Flag to indicate if a read is in progress

    // Writing
    int write_semaphore_id; // Semaphore lock for terminal write operations
    int daemon_mutex;
    int write_mbox_id; // Holds characters to be written to terminal
    int has_char; // Flag to indicate if a character is ready to be written

    char write_buffer[MAXLINE + 1];
    int write_buffer_index;
    int is_writing; // Flag to indicate if a write is in progress
} TerminalData;

TerminalData terminal_data[USLOSS_TERM_UNITS];

// Helper Functions

void clear_working_buffer(TerminalData* data)
{
    memset(data->working_buffer, 0, MAXLINE + 1);
    data->working_buffer_index = 0;
}

void enable_terminal_reads(int unit)
{
    int control = 0; // Make sure 'send char' is off (not sending any data)
    control |= 0x2; // Turn on read interrupt

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)control);
}

void enable_terminal_writes(int unit)
{
    int control = 0; // Make sure 'send char' is off (not sending any data)
    control |= 0x2; // Turn on read interrupt
    control |= 0x4; // Turn on write interrupt

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)control);
}

void disable_terminal_writes(int unit, int is_reading)
{
    int control = 0; // Make sure 'send char' is off (not sending any data)

    if(is_reading)
        control |= 0x2; // Turn on read interrupt

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)control);
}

void write_terminal_character(int unit, char c)
{
    int control = 0x1; // Turn on 'send char'
    control |= 0x2; // Turn on read interrupt
    control |= 0x4; // Turn on write interrupt
    control |= (c << 8); // Send char

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)control);
}

void disable_terminal(int unit)
{
    int control = 0;
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)control);
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

    enable_terminal_reads(unit);

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
        else if(read_status == USLOSS_DEV_READY) {} // Do nothing for reads
        else if(read_status == USLOSS_DEV_BUSY)
        {
            kernSemP(term->read_semaphore_id);

            char c = USLOSS_TERM_STAT_CHAR(status);

            term->working_buffer[term->working_buffer_index++] = c;

            // End of line reached
            if(c == 0 || c == '\n' || term->working_buffer_index == MAXLINE)
            {
                term->working_buffer[term->working_buffer_index] = '\0';

//                char temp[MAXLINE];
//                memset(temp, 0, MAXLINE);
//
//                strcpy(temp, term->working_buffer);
//                clear_working_buffer(term);
//                term->is_reading = 0;

                // Send the line to the mailbox (if mailbox is full, it gets deleted)
                MboxCondSend(term->read_mbox_id, term->working_buffer, strlen(term->working_buffer));

                clear_working_buffer(term);
                term->is_reading = 0;
            }

            kernSemV(term->read_semaphore_id);
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
            // Active write operation
            if(term->is_writing)
            {
                char c = term->write_buffer[term->write_buffer_index++];
                write_terminal_character(unit, c);

                // Reached end of input
                if(term->write_buffer[term->write_buffer_index] == '\0')
                {
                    term->is_writing = 0;
                    memset(term->write_buffer, 0, MAXLINE + 1);
                    term->write_buffer_index = 0;

                    kernSemV(term->daemon_mutex);
                }
            }

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
    char* buffer = (char *)args->arg1; // User buffer
    int bufSize = (int)(long)args->arg2; // Buffer size

    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buffer == NULL || bufSize <= 0)
    {
        args->arg2 = (void *)0; // No characters read
        args->arg4 = (void *)-1; // Invalid arguments
        return;
    }

    TerminalData* term = &terminal_data[unit];

    term->deliver_size = bufSize;
    term->is_reading = 1;

    char temp[MAXLINE + 1];
    memset(temp, 0, MAXLINE + 1);
    // Reads the first available line, or blocks until the daemon process sends a line
    int length = MboxRecv(term->read_mbox_id, temp, MAXLINE);

    strncpy(buffer, temp, bufSize);

    if(bufSize < length)
        length = bufSize;

    //buffer[bufSize] = '\0'; // Null terminate the buffer
    args->arg2 = (void *)(long) length; // Characters read
    args->arg4 = (void *)0; // Success
}

void term_write_handler(USLOSS_Sysargs* args)
{
    int unit = (int)(long)args->arg3; // Terminal number
    char* buffer = (char *)args->arg1; // User buffer
    int bufSize = (int)(long)args->arg2; // Buffer size

    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buffer == NULL || bufSize <= 0)
    {
        args->arg4 = (void *)-1; // Invalid arguments
        return;
    }

    TerminalData* term = &terminal_data[unit];

    // Grab semaphore lock
    kernSemP(term->write_semaphore_id);

    // Copy the buffer to the terminal
    memset(term->write_buffer, 0, MAXLINE + 1);
    strncpy(term->write_buffer, buffer, bufSize + 1);
    term->write_buffer_index = 0;

    // Enable writes in the Control Register and enable the flag for the daemon
    enable_terminal_writes(unit);
    term->is_writing = 1;

    // Block until the daemon finishes writing
    kernSemP(term->daemon_mutex);

    args->arg2 = (void *)(long)bufSize; // Characters written
    args->arg4 = (void *)0; // Success

    // Release semaphore lock
    kernSemV(term->write_semaphore_id);
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
        TerminalData* term = &terminal_data[i];

        // Create Terminal Read Mailbox
        int mbox_id = MboxCreate(10, MAXLINE + 1);
        term->read_mbox_id = mbox_id;

        // Create Terminal Read Semaphores
        kernSemCreate(1, &term->read_semaphore_id);

        // Empty Working Buffers
        clear_working_buffer(term);

        // Create Terminal Write Semaphores
        kernSemCreate(1, &term->write_semaphore_id);
        kernSemCreate(0, &term->daemon_mutex);

        // Create Terminal Write Mailbox
        mbox_id = MboxCreate(1, 1);
        term->write_mbox_id = mbox_id;

        // Disable the terminal itself to start
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void*)(long)0);
    }
}

void phase4_start_service_processes()
{
    spork("Sleep Daemon", sleep_daemon_process, NULL, USLOSS_MIN_STACK, 1);

    spork("Terminal 0 Daemon Process", terminal_daemon_process, (void*)0, USLOSS_MIN_STACK, 1);
    spork("Terminal 1 Daemon Process", terminal_daemon_process, (void*)1, USLOSS_MIN_STACK, 1);
    spork("Terminal 2 Daemon Process", terminal_daemon_process, (void*)2, USLOSS_MIN_STACK, 1);
    spork("Terminal 3 Daemon Process", terminal_daemon_process, (void*)3, USLOSS_MIN_STACK, 1);
}