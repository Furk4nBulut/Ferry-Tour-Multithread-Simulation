#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <limits.h>

#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_RESET "\x1b[0m"
#define LOCK_TIMEOUT 5 // seconds for mutex timeout
#define MAX_TRIPS 100
#define SIDE_A 0
#define SIDE_B 1
#define TOLL_PER_SIDE 2

// Global configuration variables (set via config file)
int CAR_COUNT = 12;
int MINIBUS_COUNT = 10;
int TRUCK_COUNT = 8;
int FERRY_CAPACITY = 20;
int TOTAL_VEHICLES; // Will be set after loading config

typedef struct {
    int id;
    int type; // 0: car, 1: minibus, 2: truck
    int capacity;
    int port; // current side
    int boarded; // 0, 1, 2
    int a_trip_no, b_trip_no;
    time_t wait_start, wait_end;
    time_t ferry_start, ferry_end;
    int returned;
    double wait_time;
    int priority;
} Vehicle;

typedef struct {
    int trip_id;
    int direction; // 0: A->B, 1: B->A
    double duration;
    int vehicle_ids[20];
    int vehicle_count;
} Trip;

// Declare pointers for dynamic allocation
Vehicle *vehicles;
Trip trip_log[MAX_TRIPS];
double *time_elapsed_a, *time_elapsed_b, *time_elapsed_ferry;
int trip_count = 0;

pthread_mutex_t boarding_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t return_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

sem_t toll_sem[4]; // 0-1: Side-A, 2-3: Side-B

int ferry_capacity = 0;
int ferry_side = SIDE_A;
int total_returned = 0;
int current_trip_id = 0;
int is_first_return = 1;
int final_trip_done = 0;
FILE *log_file;

// Function to load configuration from file
void load_config(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("Error opening config file\n");
        exit(1);
    }
    fscanf(fp, "CAR_COUNT=%d\nMINIBUS_COUNT=%d\nTRUCK_COUNT=%d\nFERRY_CAPACITY=%d\n",
           &CAR_COUNT, &MINIBUS_COUNT, &TRUCK_COUNT, &FERRY_CAPACITY);
    fclose(fp);
}

// Initialize log file
void init_log() {
    pthread_mutex_lock(&log_mutex);
    log_file = fopen("trip_log.txt", "w");
    if (!log_file) {
        printf("Error opening log file\n");
        exit(1);
    }
    pthread_mutex_unlock(&log_mutex);
}

// Log events to file
void log_event(const char* event) {
    pthread_mutex_lock(&log_mutex);
    fprintf(log_file, "[%ld] %s\n", time(NULL), event);
    fflush(log_file);
    pthread_mutex_unlock(&log_mutex);
}

// Get vehicle type name
const char* get_type_name(int type) {
    if (type == 0) return "Car";
    if (type == 1) return "Minibus";
    return "Truck";
}

// Check if remaining capacity can be filled
int can_fill_remaining(int remaining_capacity, int side) {
    int dp[FERRY_CAPACITY + 1];
    memset(dp, 0, sizeof(dp));
    dp[0] = 1;

    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == side && vehicles[i].boarded == (side == SIDE_A ? 0 : 1)) {
            for (int j = FERRY_CAPACITY; j >= vehicles[i].capacity; j--)
                dp[j] |= dp[j - vehicles[i].capacity];
        }
    }
    return dp[remaining_capacity];
}

// Try to lock mutex with timeout
int try_lock(pthread_mutex_t *mutex) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += LOCK_TIMEOUT;
    return pthread_mutex_timedlock(mutex, &ts);
}

// Select toll with shortest queue
int get_shortest_toll(int side) {
    int start = (side == SIDE_A) ? 0 : 2;
    int min_wait = INT_MAX, best_toll = start;
    for (int i = start; i < start + TOLL_PER_SIDE; i++) {
        int val;
        sem_getvalue(&toll_sem[i], &val);
        if (val > 0) return i;
        if (val < min_wait) {
            min_wait = val;
            best_toll = i;
        }
    }
    return best_toll;
}

// Vehicle thread function
void* vehicle_func(void* arg) {
    Vehicle *v = (Vehicle*)arg;
    char buffer[100];
    v->priority = 0;

    while (v->boarded < 2) {
        if (v->boarded == 1 && v->port == SIDE_B) {
            while (current_trip_id < v->b_trip_no + 2)
                usleep(20000); // Wait for two ferry cycles
        }

        int toll_index = get_shortest_toll(v->port);
        v->wait_start = time(NULL);
        sprintf(buffer, "%s %d attempting toll at Side-%c", get_type_name(v->type), v->id, v->port == SIDE_A ? 'A' : 'B');
        log_event(buffer);

        sem_wait(&toll_sem[toll_index]);
        if (try_lock(&print_mutex) == 0) {
            printf("🚗 %s %d passed toll at Side-%c\n", get_type_name(v->type), v->id, v->port == SIDE_A ? 'A' : 'B');
            pthread_mutex_unlock(&print_mutex);
        }
        sem_post(&toll_sem[toll_index]);

        sleep(1); // Wait in square
        time_t square_start = time(NULL);

        int boarded = 0;
        while (!boarded) {
            if (try_lock(&boarding_mutex) == 0) {
                v->wait_time = difftime(time(NULL), v->wait_start);
                if (v->wait_time > 10) v->priority++; // Escalate priority

                if (ferry_side == v->port && ferry_capacity + v->capacity <= FERRY_CAPACITY &&
                    (!is_first_return || v->port == SIDE_A)) {
                    int can_board = 1;
                    for (int i = 0; i < TOTAL_VEHICLES; i++)
                        if (vehicles[i].port == ferry_side && vehicles[i].priority > v->priority &&
                            vehicles[i].capacity <= FERRY_CAPACITY - ferry_capacity)
                            can_board = 0;
                    if (can_board) {
                        ferry_capacity += v->capacity;
                        v->ferry_start = time(NULL);
                        v->boarded++;
                        boarded = 1;
                        if (v->port == SIDE_A) {
                            time_elapsed_a[v->id - 1] = difftime(v->ferry_start, v->wait_start);
                            v->a_trip_no = current_trip_id;
                        } else {
                            time_elapsed_b[v->id - 1] = difftime(v->ferry_start, square_start);
                            v->b_trip_no = current_trip_id;
                        }
                        sprintf(buffer, "%s %d boarded ferry at Side-%c (Capacity: %d)",
                                get_type_name(v->type), v->id, v->port == SIDE_A ? 'A' : 'B', ferry_capacity);
                        log_event(buffer);
                        if (try_lock(&print_mutex) == 0) {
                            printf("✅ %s %d boarded ferry at Side-%c (Capacity: %d)\n",
                                   get_type_name(v->type), v->id, v->port == SIDE_A ? 'A' : 'B', ferry_capacity);
                            pthread_mutex_unlock(&print_mutex);
                        }
                    }
                }
                pthread_mutex_unlock(&boarding_mutex);
            } else {
                printf("Warning: Timeout on boarding_mutex for %s %d\n", get_type_name(v->type), v->id);
            }
            if (!boarded) usleep(200000);
        }

        while (ferry_side == v->port)
            usleep(100000);

        v->ferry_end = time(NULL);
        time_elapsed_ferry[v->id - 1] += difftime(v->ferry_end, v->ferry_start);
        v->port = 1 - v->port;

        if (v->boarded == 2) {
            if (try_lock(&return_mutex) == 0) {
                v->returned = 1;
                total_returned++;
                sprintf(buffer, "%s %d returned to Side-A", get_type_name(v->type), v->id);
                log_event(buffer);
                pthread_mutex_unlock(&return_mutex);
            }
        }

        sleep(2 + rand() % 3);
    }
    return NULL;
}

// Ferry thread function
void* ferry_func(void* arg) {
    char buffer[100];
    while (1) {
        int boarded_ids[20];
        int count = 0;
        time_t start_time = time(NULL);

        while (1) {
            sleep(1);
            if (try_lock(&boarding_mutex) == 0) {
                if (ferry_capacity >= FERRY_CAPACITY ||
                    !can_fill_remaining(FERRY_CAPACITY - ferry_capacity, ferry_side) ||
                    difftime(time(NULL), start_time) >= 5) {
                    break;
                }
                pthread_mutex_unlock(&boarding_mutex);
            }
        }

        // Log boarded vehicles
        for (int i = 0; i < TOTAL_VEHICLES; i++) {
            if (vehicles[i].boarded > 0 &&
                ((ferry_side == SIDE_A && vehicles[i].a_trip_no == current_trip_id) ||
                 (ferry_side == SIDE_B && vehicles[i].b_trip_no == current_trip_id))) {
                boarded_ids[count++] = vehicles[i].id;
            }
        }

        sprintf(buffer, "Ferry departing from Side-%c with %d units, reason: %s",
                ferry_side == SIDE_A ? 'A' : 'B', ferry_capacity,
                ferry_capacity >= FERRY_CAPACITY ? "Full" :
                !can_fill_remaining(FERRY_CAPACITY - ferry_capacity, ferry_side) ? "Fragmentation" : "Timeout");
        log_event(buffer);
        if (try_lock(&print_mutex) == 0) {
            printf("\n⛴️ Ferry departing from Side-%c to Side-%c with %d units\n",
                   ferry_side == SIDE_A ? 'A' : 'B',
                   ferry_side == SIDE_A ? 'B' : 'A',
                   ferry_capacity);
            pthread_mutex_unlock(&print_mutex);
        }

        double duration = 2 + rand() % 4;
        sleep(duration);

        if (try_lock(&boarding_mutex) == 0) {
            ferry_side = 1 - ferry_side;
            ferry_capacity = 0;
            if (is_first_return && ferry_side == SIDE_A)
                is_first_return = 0;

            // Log trip
            pthread_mutex_lock(&log_mutex);
            Trip *t = &trip_log[trip_count++];
            t->trip_id = current_trip_id++;
            t->direction = 1 - ferry_side;
            t->duration = duration;
            t->vehicle_count = count;
            memcpy(t->vehicle_ids, boarded_ids, sizeof(int) * count);
            pthread_mutex_unlock(&log_mutex);

            pthread_mutex_unlock(&boarding_mutex);
        }

        if (try_lock(&return_mutex) == 0) {
            if (total_returned >= TOTAL_VEHICLES && ferry_side == SIDE_A) {
                final_trip_done = 1;
                pthread_mutex_unlock(&return_mutex);
                break;
            }
            pthread_mutex_unlock(&return_mutex);
        }
    }
    return NULL;
}

// Printer thread for real-time visualization
void* printer_func(void* arg) {
    while (!final_trip_done) {
        if (try_lock(&print_mutex) == 0) {
            printf("\n=== System State ===\n");
            printf("Returned: %d | Ferry Capacity: %d\n", total_returned, ferry_capacity);

            printf("Side-A:\n");
            printf("CARS: ");
            for (int i = 0; i < CAR_COUNT; i++)
                if (vehicles[i].port == SIDE_A && vehicles[i].boarded < 2)
                    printf("%sC%d%s ", ANSI_COLOR_MAGENTA, vehicles[i].id, ANSI_COLOR_RESET);
            printf("\nMINIBUSES: ");
            for (int i = CAR_COUNT; i < CAR_COUNT + MINIBUS_COUNT; i++)
                if (vehicles[i].port == SIDE_A && vehicles[i].boarded < 2)
                    printf("%sMB%d%s ", ANSI_COLOR_BLUE, vehicles[i].id, ANSI_COLOR_RESET);
            printf("\nTRUCKS: ");
            for (int i = CAR_COUNT + MINIBUS_COUNT; i < TOTAL_VEHICLES; i++)
                if (vehicles[i].port == SIDE_A && vehicles[i].boarded < 2)
                    printf("%sTR%d%s ", ANSI_COLOR_YELLOW, vehicles[i].id, ANSI_COLOR_RESET);
            printf("\n");

            printf("Ferry (%s):\n", ferry_side == SIDE_A ? "→→ MOVING TO SIDE-B" : "←← RETURNING TO SIDE-A");
            for (int i = 0; i < TOTAL_VEHICLES; i++)
                if (vehicles[i].boarded > 0 && vehicles[i].port == ferry_side &&
                    ((ferry_side == SIDE_A && vehicles[i].a_trip_no == current_trip_id) ||
                     (ferry_side == SIDE_B && vehicles[i].b_trip_no == current_trip_id)))
                    printf("%s%s%d%s ",
                           vehicles[i].type == 0 ? ANSI_COLOR_MAGENTA :
                           vehicles[i].type == 1 ? ANSI_COLOR_BLUE : ANSI_COLOR_YELLOW,
                           get_type_name(vehicles[i].type), vehicles[i].id, ANSI_COLOR_RESET);
            printf("\n");

            printf("Side-B:\n");
            printf("CARS: ");
            for (int i = 0; i < CAR_COUNT; i++)
                if (vehicles[i].port == SIDE_B && vehicles[i].boarded < 2)
                    printf("%sC%d%s ", ANSI_COLOR_MAGENTA, vehicles[i].id, ANSI_COLOR_RESET);
            printf("\nMINIBUSES: ");
            for (int i = CAR_COUNT; i < CAR_COUNT + MINIBUS_COUNT; i++)
                if (vehicles[i].port == SIDE_B && vehicles[i].boarded < 2)
                    printf("%sMB%d%s ", ANSI_COLOR_BLUE, vehicles[i].id, ANSI_COLOR_RESET);
            printf("\nTRUCKS: ");
            for (int i = CAR_COUNT + MINIBUS_COUNT; i < TOTAL_VEHICLES; i++)
                if (vehicles[i].port == SIDE_B && vehicles[i].boarded < 2)
                    printf("%sTR%d%s ", ANSI_COLOR_YELLOW, vehicles[i].id, ANSI_COLOR_RESET);
            printf("\n");

            pthread_mutex_unlock(&print_mutex);
        }
        usleep(500000); // 0.5 seconds
    }
    return NULL;
}

// Print performance summary and Chart.js visualization
void calculate_averages(double *avg_car_wait, double *avg_minibus_wait, double *avg_truck_wait) {
    double car_sum = 0, minibus_sum = 0, truck_sum = 0;
    for (int i = 0; i < CAR_COUNT; i++)
        car_sum += time_elapsed_a[i] + time_elapsed_b[i];
    for (int i = CAR_COUNT; i < CAR_COUNT + MINIBUS_COUNT; i++)
        minibus_sum += time_elapsed_a[i] + time_elapsed_b[i];
    for (int i = CAR_COUNT + MINIBUS_COUNT; i < TOTAL_VEHICLES; i++)
        truck_sum += time_elapsed_a[i] + time_elapsed_b[i];
    *avg_car_wait = CAR_COUNT > 0 ? car_sum / CAR_COUNT : 0;
    *avg_minibus_wait = MINIBUS_COUNT > 0 ? minibus_sum / MINIBUS_COUNT : 0;
    *avg_truck_wait = TRUCK_COUNT > 0 ? truck_sum / TRUCK_COUNT : 0;
}

void print_performance_summary() {
    printf("\n📊 Trip Summary:\n");
    for (int i = 0; i < trip_count; i++) {
        Trip *t = &trip_log[i];
        printf("Trip %d [%s]: %.2f sec | Vehicles: ", t->trip_id,
               t->direction == 0 ? "A→B" : "B→A", t->duration);
        for (int j = 0; j < t->vehicle_count; j++)
            printf("%s%d ", get_type_name(vehicles[t->vehicle_ids[j] - 1].type), t->vehicle_ids[j]);
        printf("\n");
    }

    printf("\n📊 Performance Summary:\n");
    printf("| ID | Type    | Side-A Wait (s) | Side-B Wait (s) | Ferry Time (s) |\n");
    printf("|----|---------|-----------------|-----------------|----------------|\n");
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        printf("| %2d | %-7s | %15.2f | %15.2f | %14.2f |\n",
               vehicles[i].id, get_type_name(vehicles[i].type),
               time_elapsed_a[i], time_elapsed_b[i], time_elapsed_ferry[i]);
    }

    double avg_car_wait, avg_minibus_wait, avg_truck_wait;
    calculate_averages(&avg_car_wait, &avg_minibus_wait, &avg_truck_wait);

    printf("\n📊 Average Wait Times Chart:\n");
    printf("```chartjs\n");
    printf("{\n");
    printf("  \"type\": \"bar\",\n");
    printf("  \"data\": {\n");
    printf("    \"labels\": [\"Cars\", \"Minibuses\", \"Trucks\"],\n");
    printf("    \"datasets\": [{\n");
    printf("      \"label\": \"Average Wait Time (s)\",\n");
    printf("      \"data\": [%.2f, %.2f, %.2f],\n", avg_car_wait, avg_minibus_wait, avg_truck_wait);
    printf("      \"backgroundColor\": [\"#FF00FF\", \"#0000FF\", \"#FFFF00\"],\n");
    printf("      \"borderColor\": [\"#FF00FF\", \"#0000FF\", \"#FFFF00\"],\n");
    printf("      \"borderWidth\": 1\n");
    printf("    }]\n");
    printf("  },\n");
    printf("  \"options\": {\n");
    printf("    \"scales\": {\n");
    printf("      \"y\": {\n");
    printf("        \"beginAtZero\": true,\n");
    printf("        \"title\": { \"display\": true, \"text\": \"Wait Time (s)\" }\n");
    printf("      }\n");
    printf("    }\n");
    printf("  }\n");
    printf("}\n");
    printf("```\n");
}

int main() {
    srand(time(NULL));
    load_config("V2/config.txt");

    // Set TOTAL_VEHICLES after loading config
    TOTAL_VEHICLES = CAR_COUNT + MINIBUS_COUNT + TRUCK_COUNT;

    // Dynamically allocate arrays
    vehicles = (Vehicle*)malloc(TOTAL_VEHICLES * sizeof(Vehicle));
    time_elapsed_a = (double*)malloc(TOTAL_VEHICLES * sizeof(double));
    time_elapsed_b = (double*)malloc(TOTAL_VEHICLES * sizeof(double));
    time_elapsed_ferry = (double*)malloc(TOTAL_VEHICLES * sizeof(double));

    if (!vehicles || !time_elapsed_a || !time_elapsed_b || !time_elapsed_ferry) {
        printf("Memory allocation failed\n");
        exit(1);
    }

    // Initialize timing arrays
    memset(time_elapsed_a, 0, TOTAL_VEHICLES * sizeof(double));
    memset(time_elapsed_b, 0, TOTAL_VEHICLES * sizeof(double));
    memset(time_elapsed_ferry, 0, TOTAL_VEHICLES * sizeof(double));

    init_log();

    pthread_t vehicle_threads[TOTAL_VEHICLES];
    pthread_t ferry_thread, printer_thread;

    // Initialize semaphores
    for (int i = 0; i < 4; i++)
        sem_init(&toll_sem[i], 0, 1);

    // Create vehicles
    int id = 1;
    for (int i = 0; i < CAR_COUNT; i++)
        vehicles[i] = (Vehicle){id++, 0, 1, SIDE_A, 0, -1, -1, 0, 0, 0, 0, 0};
    for (int i = 0; i < MINIBUS_COUNT; i++)
        vehicles[CAR_COUNT + i] = (Vehicle){id++, 1, 2, SIDE_A, 0, -1, -1, 0, 0, 0, 0};
    for (int i = 0; i < TRUCK_COUNT; i++)
        vehicles[CAR_COUNT + MINIBUS_COUNT + i] = (Vehicle){id++, 2, 3, SIDE_A, 0, -1, -1, 0, 0, 0, 0};

    // Create threads
    pthread_create(&ferry_thread, NULL, ferry_func, NULL);
    pthread_create(&printer_thread, NULL, printer_func, NULL);
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_create(&vehicle_threads[i], NULL, vehicle_func, &vehicles[i]);

    // Join threads
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_join(vehicle_threads[i], NULL);
    pthread_join(ferry_thread, NULL);
    pthread_join(printer_thread, NULL);

    print_performance_summary();
    printf("\n✅ Simulation complete. All vehicles returned.\n");

    // Cleanup
    for (int i = 0; i < 4; i++)
        sem_destroy(&toll_sem[i]);
    pthread_mutex_destroy(&boarding_mutex);
    pthread_mutex_destroy(&return_mutex);
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&print_mutex);
    fclose(log_file);

    // Free dynamically allocated memory
    free(vehicles);
    free(time_elapsed_a);
    free(time_elapsed_b);
    free(time_elapsed_ferry);

    return 0;
}