#include "scheduler.h"
#include <errno.h> // Ensure this is included
#include <math.h>



int algorithm;          
int quantum;   
int arr_msgq_id;
int comp_msgq_id;          
FILE* logFile;         
void* readyQueue;        
PCB* PCB_table_head = NULL;
PCB* PCB_table_tail = NULL;
int process_count = 0;   
int static_process_count=0;
int first_time = 0;
int first_arrival_time;
PCB* running_process = NULL; 
int current_time = -1;  
int actual_running_time = 0; 
int time_slice = 0;      
int terminated = 0;  
int KEY = 300;   

int process_not_arrived = 1; // Flag to indicate if there is a processes that haven't arrived

int TA_Array[MAX_PROCESSES];
double WTA_Array[MAX_PROCESSES];
double waiting = 0;

void initialize(int alg, int q)
{
    algorithm = alg;
    quantum = q;

    signal(SIGINT, (void (*)(int))cleanup);


    for (int i = 0; i < MAX_PROCESSES; i++) TA_Array[i] = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) WTA_Array[i] = -1;
    
    printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
    printf(algorithm == HPF ? "Using HPF algorithm\n" :
           algorithm == SRTN ? "Using SRTN algorithm\n" :
           algorithm == RR ? "Using RR algorithm\n" : "Unknown algorithm\n");
    switch(algorithm) {
        case HPF:
            readyQueue = createMinHeap(MAX_PROCESSES, compare_priority);
            printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
            printf("Scheduler started with Highest Priority First algorithm\n");
            break;
        case SRTN:
            printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
            printf("Scheduler started with Shortest Remaining Time Next algorithm\n");
            readyQueue = createMinHeap(MAX_PROCESSES, compare_remaining_time);
            break;
        case RR:
            readyQueue = createQueue();
            printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
            printf("Scheduler started with Round Robin algorithm, quantum = %d\n", quantum);
            break;

    }

    // Create log file
    logFile = fopen("scheduler.log", "w");
    if (!logFile)
    {
        perror("Failed to open log file");
        exit(1);
    }
    if (!logFile)
    {
        perror("Failed to open log file");
        exit(1);
    }
    fprintf(logFile, "#At time x process y state arr w total z remain y wait k\n");    

    // Open arrival message queue
    key_t arr_key = ftok("keyfile", 'a');
    arr_msgq_id = msgget(arr_key, 0666);
    if (arr_msgq_id == -1)
    {
        perror("Error accessing arrival message queue");
        return ;
    }

    // Open completion message queue
    key_t comp_key = ftok("keyfile", 'c');
    comp_msgq_id = msgget(comp_key, 0666);
    if (comp_msgq_id == -1)
    {
        perror("Error accessing completion message queue");
        return ;
    }

    sync_clk();


    printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");

    printf("Scheduler initialized successfully\n");
}

// Main scheduler loop
void run_scheduler()
{
    while (!terminated)
    {
        int new_time = get_clk();
        if (new_time > current_time)
        {
            current_time = new_time;

            // Check if process finished
            if (running_process && running_process->remaining_time >= 0)
            {
                update_process_times();
            }
            if (running_process && running_process->remaining_time <= 0)
            {
                handle_finished_process();
            }

            check_arrivals();
            
            // Select next process if needed
            check_context_switch();
            
        }
    }
    log_performance_stats();
}

void check_context_switch()
{
    PCB *next_process = select_next_process();

    if (next_process != running_process)
    {
        if (running_process && running_process->remaining_time > 0)
        {
            stop_process(running_process);
        }
        running_process = next_process;
        if (running_process)
        {
            start_process(running_process);
        }
    }
}

// Check for newly arrived processes from the message queue

void check_arrivals()
{
    ProcessMessage msg;
    int processes_received = 0; // Track processes received this tick
    // Only check messages if we expect new processes
    if (process_not_arrived)
    {
        while (true)
        {
            // Blocking msgrcv to wait for a message
            ssize_t received = msgrcv(arr_msgq_id, &msg, sizeof(msg) - sizeof(long), 0, 0);
            if (received == -1)
            {
                perror("Error receiving message");
                return;
            }


            if (msg.process_id == -2) {
                printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
                printf("Received no more processes signal at time %d\n", current_time);
                process_not_arrived = false;
                if (running_process == NULL && Empty(readyQueue))
                {
                    terminated = true;
                }
                return; 
            }
            else if (msg.process_id == -1)
            {
                // No process this tick, only exit if no processes were received
                if (processes_received == 0)
                {
                    return;
                }
                // Otherwise, continue checking for more process messages
                continue;
            }

            // Handle new process
            PCB *new_process = (PCB *)malloc(sizeof(PCB));
            if (!new_process)
            {
                perror("Failed to allocate memory for PCB");
                continue;
            }
            new_process->id = msg.process_id;
            new_process->arrival_time = msg.arrival_time;
            new_process->runtime = msg.runtime;
            new_process->remaining_time = msg.runtime;
            new_process->priority = msg.priority;
            new_process->pid = msg.pid;       // Get the PID from the message
            new_process->shm_id = msg.shm_id; // Get the shared memory ID from the message
            new_process->wait_time = 0;
            new_process->start_time = -1;
            new_process->status = READY;
            // Attach to the shared memory
            int *shm_ptr = (int *)shmat(msg.shm_id, NULL, 0);
            if (new_process->shm_ptr == (int *)-1)
            {
                perror("Failed to attach to shared memory in scheduler");
                free(new_process);
                continue;
            }

            new_process->shm_ptr = shm_ptr; // Store the pointer to shared memory

            printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
            printf("Received new process %d at time %d\n", new_process->id, current_time);
            // log_process_state(new_process, "arrived");

            // Add to processes array
            static_process_count++;
            actual_running_time += new_process->runtime;
            PCB_add(new_process);

            // Add to ready queue
            switch (algorithm)
            {
            case HPF:
                insertMinHeap(readyQueue, new_process);
                break;
            case SRTN:
                insertMinHeap(readyQueue, new_process);
                break;
            case RR:
                enqueue(readyQueue, new_process);
                break;
            }

            printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
            printf("Process %d arrived at time %d\n", new_process->id, current_time);
            //log_process_state(new_process, "arrived");
            processes_received++;

            // Peek at the next message non-blocking
            received = msgrcv(arr_msgq_id, &msg, sizeof(msg) - sizeof(long), 0, 0);
            if (received == -1)
            {
                if (errno == ENOMSG)
                {
                    // No more messages, exit
                    return;
                }
                else
                {
                    perror("Error peeking at message");
                    return;
                }
            }
            // Re-send the peeked message
            if (msgsnd(arr_msgq_id, &msg, sizeof(msg) - sizeof(long), 0) == -1)
            {
                perror("Error re-sending message");
                return;
            }

            // If the next message is not a process message, exit
            if (msg.process_id <= 0)
            {
                return;
            }
            if (msg.arrival_time != current_time)
            {
                // If the next message is not for the current time, exit
                return;
            }
        }
    }

    // Update termination condition
    if (!process_not_arrived && running_process == NULL && Empty(readyQueue))
    {
        terminated = true;
    }
}

// Update times for the running process
void update_process_times()
{
    if (running_process && running_process->remaining_time > 0)
    {
        running_process->remaining_time--;
        *(running_process->shm_ptr) = running_process->remaining_time; // Update shared memory

        if (algorithm == RR)
        {
            time_slice++;
        }
    }
}

void handle_finished_process()
{
    if (running_process && running_process->remaining_time <= 0)
    {
        CompletionMessage msg;
        if (msgrcv(comp_msgq_id, &msg, sizeof(msg) - sizeof(long), 0, !IPC_NOWAIT) == -1)
        {
            perror("Error receiving completion message");
        }
        running_process->ending_time = current_time;
        printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
        printf("Process %d finished at time %d\n", running_process->id, current_time);
        log_process_state(running_process, "finished");

        PCB_remove(running_process);

        running_process = NULL;
        time_slice = 0;
    }
}

int compare_priority(void *a, void *b)
{
    PCB *p1 = (PCB *)a;
    PCB *p2 = (PCB *)b;

    // Lower priority number = higher priority
    if (p1->priority != p2->priority)
    {
        return p1->priority - p2->priority;
    }

    // If priorities are equal, use arrival time as tiebreaker
    return p1->arrival_time - p2->arrival_time;
}

int compare_remaining_time(void *a, void *b)
{
    PCB *p1 = (PCB *)a;
    PCB *p2 = (PCB *)b;

    // Lower remaining time = higher priority
    if (p1->remaining_time != p2->remaining_time)
    {
        return p1->remaining_time - p2->remaining_time;
    }

    // If remaining times are equal, use arrival time as tiebreaker
    return p1->arrival_time - p2->arrival_time;
}

// Select the next process to run based on the algorithm
PCB *select_next_process()
{

    // If using RR and quantum expired, put process back in queue

    if (algorithm == RR && running_process && time_slice >= quantum) {
        printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
        printf("Quantum expired for Process %d (remaining time: %d)\n", running_process->id, running_process->remaining_time);

        // Only enqueue if process still has remaining time, which should be the case anyways
        if (running_process->remaining_time > 0)
        {
            enqueue(readyQueue, running_process);
        }

        PCB *next = NULL;
        // running_process = NULL;
        time_slice = 0;

        // Get next process from queue if available
        if (!isEmpty(readyQueue))
        {
            next = (PCB *)dequeue(readyQueue);
        }

        return next;
    }

    if (running_process)
    {
        switch (algorithm)
        {
        case HPF:
            // HPF is non-preemptive, continue with current process
            return running_process;

        case SRTN:
            if (!isEmpty(readyQueue))
            {
                PCB *top = getMin(readyQueue);
                if (top && top->remaining_time < running_process->remaining_time)
                {
                    // Preempt current process
                    insertMinHeap(readyQueue, running_process);
                    return (PCB *)extractMin(readyQueue);
                }
            }
            return running_process;

        case RR:
            return running_process;

        default:
            return running_process;
        }
    }

    // No running process, get one from the ready queue
    switch (algorithm)
    {
    case HPF:
    case SRTN:
        if (!isEmpty(readyQueue))
        {
            return (PCB *)extractMin(readyQueue);
        }
        break;

    case RR:
        if (!isEmpty(readyQueue))
        {
            return (PCB *)dequeue(readyQueue);
        }
        break;
    }

    // No process to run
    return NULL;
}

void start_process(PCB *process)
{
    if (!process)
        return;

    process->status = RUNNING;

    if (process->start_time == -1)
    {
        // First time starting this process
        process->start_time = current_time;
        process->wait_time = current_time - process->arrival_time;

        // Update shared memory with current remaining time
        *(process->shm_ptr) = process->remaining_time;

        
        printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
        printf("Starting process %d at time %d\n", process->id, current_time);
        if (first_time == 0) {
            first_arrival_time = current_time;
            first_time = 1;
        }

        // No need to fork as the process is already running
        log_process_state(process, "started");
    }
    else
    {
        // Resume the process
        *(process->shm_ptr) = process->remaining_time;
        printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
        printf("Resuming process %d at time %d\n", process->id, current_time);
        process->status = RUNNING;
        log_process_state(process, "resumed");
    }

    kill(process->pid, SIGCONT);

    time_slice = 0;
}

// Stop a process
void stop_process(PCB *process)
{
    if (!process)
        return;

    if (process->status == RUNNING && process->remaining_time > 0)
    {
        // Only stop if process is running and not finished
        printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
        printf("Stopping process %d (remaining time: %d)\n", process->id, process->remaining_time);
        printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
        printf("first arrival time %d\n", first_arrival_time);

        kill(process->pid, SIGSTOP);
        // current_shm_ptr = NULL; // Reset shared memory pointer
        process->status = READY;
        log_process_state(process, "stopped");
    }
}

// Log process state changes

void log_process_state(PCB* process, char* state) {
    if (!process || !logFile) return;
    process->wait_time = current_time - process->arrival_time - (process->runtime - process->remaining_time);
    if(strcmp(state,"finished")!=0)
    {
        fprintf(logFile, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
            current_time, process->id, state, process->arrival_time,
            process->runtime, process->remaining_time, process->wait_time); 
    }
    else
    {
        int TA = process->ending_time - process->arrival_time;
        if (process->runtime > 0)
        {
            double WTA = (double)TA / process->runtime;
            WTA = round(WTA * 100) / 100;
            fprintf(logFile, "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA %.2f\n",
                    current_time, process->id, state, process->arrival_time,
                    process->runtime, process->remaining_time, process->wait_time, TA, WTA);
            WTA_Array[process->id - 1] = WTA;
        }
        else
        {
            fprintf(logFile, "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA Could not be calculated\n",
                    current_time, process->id, state, process->arrival_time,
                    process->runtime, process->remaining_time, process->wait_time, TA);
        }
        TA_Array[process->id - 1] = TA;
    }
    fflush(logFile);
}

void log_performance_stats()
{
    FILE *perfLogFile = fopen("scheduler.perf", "w");
    if (!perfLogFile)
    {
        perror("Failed to open performance file");
        exit(1);
    }
    double CPU_utilization = (actual_running_time / (double)(current_time)) * 100;
    CPU_utilization = round(CPU_utilization * 100) / 100;
    fprintf(perfLogFile, "CPU utilization = %.2f %%\n", CPU_utilization);
    double WTA_sum = 0;
    for (int i = 0; i < static_process_count; i++)
    {
        if (WTA_Array[i] != -1)
            WTA_sum += WTA_Array[i];
    }
    double WTA_AVG = WTA_sum / static_process_count;

    WTA_AVG = round(WTA_AVG * 100) /100;
    fprintf(perfLogFile, "Avg WTA = %.2f\n", WTA_AVG);
    double ans = waiting / static_process_count;
    fprintf(perfLogFile, "Avg Waiting = %.2f\n", ans);
    double diffSquared = 0;
    for (int i = 0; i < static_process_count; i++)
    {
        diffSquared += pow(WTA_Array[i] - WTA_AVG, 2);
    }
    fprintf(perfLogFile, "Std WTA = %.2f\n", pow(diffSquared / static_process_count, 1.0 / 2));
}


void PCB_add(PCB *process)
{
    if (!process)
        return;

    if (PCB_table_head == NULL)
    {
        PCB_table_head = process;
        PCB_table_tail = process;
        process->next = NULL;
    }
    else
    {
        PCB_table_tail->next = process;
        PCB_table_tail = process;
        process->next = NULL;
    }

    process_count++;
}

void PCB_remove(PCB *process)
{
    if (!PCB_table_head || !process)
        return;
    waiting += process->wait_time;

    // Clean up shared memory resources BEFORE freeing the PCB
    if (process->shm_ptr != (int *)-1 && process->shm_ptr != NULL)
    {
        shmdt(process->shm_ptr); // Detach from shared memory
    }

    if (process->shm_id > 0)
    {
        shmctl(process->shm_id, IPC_RMID, NULL); // Mark shared memory for removal
    }

    // Continue with existing PCB removal logic
    if (PCB_table_head == process)
    {
        PCB_table_head = process->next;

        if (PCB_table_tail == process)
        {
            PCB_table_tail = NULL;
        }
    }
    else
    {
        PCB *current = PCB_table_head;
        while (current && current->next != process)
        {
            current = current->next;
        }

        if (current)
        {
            current->next = process->next;

            if (PCB_table_tail == process)
            {
                PCB_table_tail = current;
            }
        }
        else
        {
            return;
        }
    }

    free(process);
    process_count--;
}


void cleanup() {
    printf("\033[0;34m"); printf("[Scheduler] "); printf("\033[0m");
    printf("Cleaning up scheduler resources...\n");

    if (logFile)
    {
        fclose(logFile);
    }

    // Clean up running process if it exists
    if (running_process)
    {
        PCB_remove(running_process);
        running_process = NULL;
    }

    // Clean up all remaining PCBs
    PCB *current = PCB_table_head;
    while (current)
    {
        PCB *next = current->next;

        // Double-check shared memory cleanup
        if (current->shm_ptr != (int *)-1 && current->shm_ptr != NULL)
        {
            shmdt(current->shm_ptr);
        }
        if (current->shm_id > 0)
        {
            shmctl(current->shm_id, IPC_RMID, NULL);
        }

        free(current);
        current = next;
    }

    // Free ready queue
    switch (algorithm)
    {
    case HPF:
    case SRTN:
        if (readyQueue)
            destroyHeap(readyQueue);
        break;
    case RR:
        if (readyQueue)
            free(readyQueue);
        break;
    }

    destroy_clk(0);
    exit(0);
}


int Empty(void * RQ){

    if (algorithm == RR)
    {
        Queue *q = (Queue *)RQ;
        return q->size == 0;
    }
    else
    {
        MinHeap *mh = (MinHeap *)RQ;
        return mh->size == 0;
    }
}