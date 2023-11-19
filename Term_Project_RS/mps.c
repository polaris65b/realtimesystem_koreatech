#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>
#include <getopt.h>
#include <time.h>

// Constants
#define MAX_PROCESSORS 10
#define MAX_PROCESSES 1000
#define DEFAULT_N 2
#define DEFAULT_SAP 'M'
#define DEFAULT_QS "RM"
#define DEFAULT_ALG "RR"
#define DEFAULT_Q 20
#define DEFAULT_INFILE "in.txt"
#define DEFAULT_OUTMODE 1
#define DEFAULT_OUTFILE "out.txt"
#define DEFAULT_T 200
#define DEFAULT_T1 10
#define DEFAULT_T2 1000
#define DEFAULT_L 100
#define DEFAULT_L1 10
#define DEFAULT_L2 500
#define DEFAULT_PC 10
#define MAX_SIMULATION_TIME 30.0

// Data structures
typedef struct burst {
    int pid;
    int burst_length;
    int arrival_time;
    int remaining_time;
    int finish_time;
    int turnaround_time;
    int cpu_id;
    struct burst *next;
} Burst;

typedef struct {
    Burst *head;
    Burst *tail;
} Queue;

typedef struct {
    pthread_mutex_t lock;
    Queue queue;
} MutexQueue;

// Global variables
int n_processors = DEFAULT_N;
char sap = DEFAULT_SAP;
char qs[3] = DEFAULT_QS;
char alg[4] = DEFAULT_ALG;
char infile[256] = DEFAULT_INFILE;
int outmode = DEFAULT_OUTMODE;
char outfile[256] = DEFAULT_OUTFILE;
int T = DEFAULT_T, T1 = DEFAULT_T1, T2 = DEFAULT_T2;
int L = DEFAULT_L, L1 = DEFAULT_L1, L2 = DEFAULT_L2;
int PC = DEFAULT_PC;
int Q = DEFAULT_Q;
bool random_flag = false;
MutexQueue mutex_queues[MAX_PROCESSORS];
pthread_t processor_threads[MAX_PROCESSORS];
Queue finished_bursts;
pthread_mutex_t finished_bursts_lock;
int completed_processes = 0;
pthread_mutex_t completed_processes_lock;

// Function prototypes
void parse_arguments(int argc, char *argv[]);
void initialize_simulation();
void read_input_file();
void create_random_bursts();
void *processor_thread(void *arg);
void enqueue(Queue *queue, Burst *burst);
Burst *dequeue(Queue *queue);
Burst *peek(Queue *queue);
Burst *find_shortest_job(Queue *queue);
int queue_length(Queue *queue);
bool is_queue_empty(Queue *queue);
void output_simulation_results();
double get_wall_clock_time();

double simulation_start_time;

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);
    initialize_simulation();
    if (random_flag) {
        create_random_bursts();
    } else {
        read_input_file();
    }

    simulation_start_time = get_wall_clock_time();

    // Create processor threads
    for (int i = 0; i < n_processors; i++) {
        pthread_create(&processor_threads[i], NULL, processor_thread, (void *)(long)i);
    }

    // Wait for all processor threads to finish
    for (int i = 0; i < n_processors; i++) {
        pthread_join(processor_threads[i], NULL);
    }

    output_simulation_results();

    return 0;
}

void *processor_thread(void *arg) {
    int cpu_id = (int)(long)arg;
    MutexQueue *mutex_queue = &mutex_queues[cpu_id];

    while (true) {
        Burst *burst = NULL;
        if (!is_queue_empty(&mutex_queue->queue)) {
            if (strcmp(alg, "RR") == 0) {
                burst = dequeue(&mutex_queue->queue);
            } else if (strcmp(alg, "SJF") == 0) {
                burst = find_shortest_job(&mutex_queue->queue);
            } else if (strcmp(alg, "FCFS") == 0) {
                burst = dequeue(&mutex_queue->queue);
            }
        }

        if (burst != NULL) {
            burst->cpu_id = cpu_id;
            // usleep(burst->remaining_time * 1000);
            struct timespec req, rem;
            req.tv_sec = burst->remaining_time / 1000;
            req.tv_nsec = (burst->remaining_time % 1000) * 1000000L;
            nanosleep(&req, &rem);
            burst->finish_time = get_wall_clock_time() - simulation_start_time;
            burst->turnaround_time = burst->finish_time - burst->arrival_time;
            enqueue(&finished_bursts, burst);
            completed_processes++;
        }

        if (get_wall_clock_time() - simulation_start_time > MAX_SIMULATION_TIME) {
            break;
        }
    }

    return NULL;
}

void parse_arguments(int argc, char *argv[]) {
    int opt;

    static struct option long_options[] = {
        {"n_processors", required_argument, 0, 'n'},
        {"assignment_policy", required_argument, 0, 'a'},
        {"scheduling_algorithm", required_argument, 0, 's'},
        {"infile", required_argument, 0, 'i'},
        {"outmode", required_argument, 0, 'm'},
        {"outfile", required_argument, 0, 'o'},
        {"random", required_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "n:a:s:i:m:o:r:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n':
                n_processors = atoi(optarg);
                break;
            case 'a':
                sscanf(optarg, "%c %2s", &sap, qs);
                qs[sizeof(qs) - 1] = '\0';
                break;
            case 's':
                sscanf(optarg, "%3s %d", alg, &Q);
                alg[sizeof(alg) - 1] = '\0';
                break;
            case 'i':
                strncpy(infile, optarg, sizeof(infile) - 1);
                infile[sizeof(infile) - 1] = '\0';
                break;
            case 'm':
                outmode = atoi(optarg);
                break;
            case 'o':
                strncpy(outfile, optarg, sizeof(outfile) - 1);
                outfile[sizeof(outfile) - 1] = '\0';
                break;
            case 'r':
                sscanf(optarg, "%d %d %d %d %d %d %d", &T, &T1, &T2, &L, &L1, &L2, &PC);
                random_flag = true;
                break;
            default:
                printf("Usage: %s [-n N] [-a SAP QS] [-s ALG Q] [-i INFILE] [-m OUTMODE] [-o OUTFILE] [-r T T1 T2 L L1 L2 PC]\n", argv[0]);
                exit(1);
        }
    }
}

void initialize_simulation() {
    for (int i = 0; i < MAX_PROCESSORS; i++) {
        pthread_mutex_init(&mutex_queues[i].lock, NULL);
        mutex_queues[i].queue.head = NULL;
        mutex_queues[i].queue.tail = NULL;
    }
    pthread_mutex_init(&completed_processes_lock, NULL);
    pthread_mutex_init(&finished_bursts_lock, NULL);
    finished_bursts.head = NULL;
    finished_bursts.tail = NULL;
}

void read_input_file() {
    FILE *file = fopen(infile, "r");
    if (file == NULL) {
        perror("Error opening input file");
        exit(1);
    }

    int pid, burst_length, arrival_time;
    while (fscanf(file, "%d %d %d", &pid, &burst_length, &arrival_time) == 3) {
        Burst *burst = (Burst *)malloc(sizeof(Burst));
        burst->pid = pid;
        burst->burst_length = burst_length;
        burst->arrival_time = arrival_time;
        burst->remaining_time = burst_length;
        burst->finish_time = 0;
        burst->turnaround_time = 0;
        burst->cpu_id = -1;
        burst->next = NULL;

        if (sap == 'S') {
            enqueue(&mutex_queues[0].queue, burst);
        } else if (sap == 'M') {
            int target_queue;
            if (strcmp(qs, "RM") == 0) {
                target_queue = pid % n_processors;
            } else if (strcmp(qs, "LM") == 0) {
                target_queue = 0;
                int min_queue_length = queue_length(&mutex_queues[0].queue);
                for (int i = 1; i < n_processors; i++) {
                    int current_queue_length = queue_length(&mutex_queues[i].queue);
                    if (current_queue_length < min_queue_length) {
                        min_queue_length = current_queue_length;
                        target_queue = i;
                    }
                }
            }
            enqueue(&mutex_queues[target_queue].queue, burst);
        }
    }

    fclose(file);
}

void create_random_bursts() {
    srand(time(NULL));

    for (int i = 0; i < T; i++) {
        int pid = i;
        int burst_length = rand() % (T2 - T1 + 1) + T1;
        int arrival_time = rand() % (L2 - L1 + 1) + L1;
        Burst *burst = (Burst *)malloc(sizeof(Burst));
        burst->pid = pid;
        burst->burst_length = burst_length;
        burst->arrival_time = arrival_time;
        burst->remaining_time = burst_length;
        burst->finish_time = 0;
        burst->turnaround_time = 0;
        burst->cpu_id = -1;
        burst->next = NULL;

        if (sap == 'S') {
            enqueue(&mutex_queues[0].queue, burst);
        } else if (sap == 'M') {
            int target_queue;
            if (strcmp(qs, "RM") == 0) {
                target_queue = pid % n_processors;
            } else if (strcmp(qs, "LM") == 0) {
                target_queue = 0;
                int min_queue_length = queue_length(&mutex_queues[0].queue);
                for (int i = 1; i < n_processors; i++) {
                    int current_queue_length = queue_length(&mutex_queues[i].queue);
                    if (current_queue_length < min_queue_length) {
                        min_queue_length = current_queue_length;
                        target_queue = i;
                    }
                }
            }
            enqueue(&mutex_queues[target_queue].queue, burst);
        }
    }
}

void enqueue(Queue *queue, Burst *burst) {
    if (queue->tail == NULL) {
        queue->head = burst;
        queue->tail = burst;
    } else {
        queue->tail->next = burst;
        queue->tail = burst;
    }
    burst->next = NULL;
}

Burst *dequeue(Queue *queue) {
    Burst *burst = queue->head;
    if (burst != NULL) {
        queue->head = burst->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
    }
    return burst;
}

Burst *peek(Queue *queue) {
    return queue->head;
}

Burst *find_shortest_job(Queue *queue) {
    Burst *prev = NULL;
    Burst *shortest_prev = NULL;
    Burst *shortest = NULL;
    int min_remaining_time = INT_MAX;

    for (Burst *burst = queue->head; burst != NULL; prev = burst, burst = burst->next) {
        if (burst->remaining_time < min_remaining_time) {
            min_remaining_time = burst->remaining_time;
            shortest = burst;
            shortest_prev = prev;
        }
    }

    if (shortest_prev != NULL) {
        shortest_prev->next = shortest->next;
    } else {
        queue->head = shortest->next;
    }

    if (shortest == queue->tail) {
        queue->tail = shortest_prev;
    }

    return shortest;
}

double get_wall_clock_time() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec + tp.tv_usec * 1e-6;
}

void output_simulation_results() {
    FILE *output_file = NULL;
    if (outmode != 1) {
        output_file = fopen(outfile, "w");
        if (output_file == NULL) {
            perror("Error opening output file");
            exit(1);
        }
    }

    if (outmode == 1 || outmode == 3) {
        printf("Finished bursts:\n");
        printf("BurstID\tCPU_ID\tArrival_Time\tFinish_Time\tTurnaround_Time\n");
    }
    if (outmode == 2 || outmode == 3) {
        fprintf(output_file, "Finished bursts:\n");
        fprintf(output_file, "BurstID\tCPU_ID\tArrival_Time\tFinish_Time\tTurnaround_Time\n");
    }

    for (Burst *burst = finished_bursts.head; burst != NULL; burst = burst->next) {
        if (outmode == 1 || outmode == 3) {
            printf("%d\t%d\t%.2f\t%.2f\t%.2f\n", burst->pid, burst->cpu_id,
                burst->arrival_time, burst->finish_time, burst->turnaround_time);
        }
        if (outmode == 2 || outmode == 3) {
            fprintf(output_file, "%d\t%d\t%.2f\t%.2f\t%.2f\n", burst->pid, burst->cpu_id,
                burst->arrival_time, burst->finish_time, burst->turnaround_time);
        }
    }

    if (output_file != NULL) {
        fclose(output_file);
    }
}

int queue_length(Queue *queue) {
    int length = 0;
    Burst *current = queue->head;
    
    while (current != NULL) {
        length++;
        current = current->next;
    }
    
    return length;
}

bool is_queue_empty(Queue *queue) {
    return queue->head == NULL;
}
