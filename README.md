**test00**: The differences between our output and the test case is purely due to how the system clock runs. The
difference between the start and end times in our output comes out to be around 5.04s, which is effectively identical
to the test case putting the process to sleep for 5 seconds. Since the differences between our output and test case
are purely due to timing, and the logic is correct, we deserve full points for this test case.

**test01**: This is similar to test00 in that the only differences between our output and the test case is minor
differences in the wakeup time for each child process. All of the wakeup times begin with `10`, meaning each process is
going to sleep for 10 seconds, as expected - so our logic is correct. Therefore, we deserve full points for this test
case.

**test02**: The differences here are due to both clock timings and wakeup order. Each process goes to sleep as expected,
following the test case order. However, their wakeup orders are unpredictable due to the use of mailboxes, and because
every child process has the same priority. In our output, the test case with the smallest sleep time is the first to be
woken up,
because the sleep daemon process orders the processes in a priority queue based on their wakeup time. Therefore, our
logic is valid because the processes are waking up in the order as expected, and the system clock times that are printed
out approximately match up with the respective wakeup times (from within a few milliseconds to a few seconds for later
processes, simply because there are `Wait` calls happening in between, adding to the system time), and so we deserve
full credit for this test case.

**test07**: The differences in the output in this test case are a result of unpredictable ordering issues. After Child0
finishes its write#0, in the test case it is immediately able to perform write#1. However, in our code, due to the use
of
semaphore mutex locks to ensure only one write/read occurs at a time per process per terminal, there are ample
opportunities
for the dispatcher to context switch to another same priority process. Each process still completes its writes in the
correct order, but the overall order of when the processes get CPU time is slightly altered. Plus, the terminal output
matches the test case's terminal output exactly, so functionally the same result is achieved, therefore we
deserve full credit for this test case.

**test20**: The differences in the output of this test case are also due to unpredictable ordering of the dispatcher. In the 
test case, Child21's write operation occurs after the first four reads. However, since all of the processes have the 
same dispatcher priority, and due to the aforementioned semaphore mutex locks, in our output, Child21's write operation 
happens after the third read operation (before the fourth one done by Child0). The terminal outputs of our code and 
the test case are exactly the same, so functionally we achieve the same result and therefore deserve full credit for this test case.

**test22**: Just like above, this test case's differences are due to ordering issues. The test case is able to perform 
the writes for `one: second line` and `two: second line` consecutively, whereas in our test case, the write for 
`one: third line, longer than previous ones` occurs in between - which is entirely due to how the dispatcher selects 
the next process to run when semaphore operations and/or mailbox sends are involved. The actual terminal output that our 
code generated is identical to the test case's terminal output, meaning our logic is correct and the ordering of the 
terminal I/O operations is identical. Therefore, we also deserve full credit for this test case.