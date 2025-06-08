#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define CAR_COUNT 25
#define MINIBUS_COUNT 15
#define TRUCK_COUNT 10
#define TOTAL_VEHICLES 50
#define FERRY_CAPACITY 50
#define TOLL_PER_SIDE 2
#define MAX_TRIPS 100
#define SIDE_A 0
#define SIDE_B 1

typedef struct {
    int id;
    int type; // 0: Car, 1: Minibus, 2: Truck
    int capacity; // 1, 2, 4
    int port; // SIDE_A or SIDE_B
    int start_port; // Initial starting point (X)
    int boarded; // 0, 1, 2
    int a_trip_no, b_trip_no; // Trip numbers for A->B and B->A
    int returned; // 1 if vehicle completed round-trip
    time_t wait_start_a, wait_end_a; // Side-A wait times
    time_t wait_start_b, wait_end_b; // Side-B wait times
    time_t ferry_start_a, ferry_end_a; // A->B ferry times
    time_t ferry_start_b, ferry_end_b; // B->A ferry times
    time_t trip_start, trip_end; // Full round-trip times
} Vehicle;

typedef struct {
    int trip_id;
    int direction; // 0: A->B, 1: B->A
    double duration;
    int vehicle_ids[20];
    int vehicle_count;
    int capacity_used;
} Trip;

// Global variables
Vehicle vehicles[TOTAL_VEHICLES];
Trip trip_log[MAX_TRIPS];
int trip_count = 0;
int ferry_capacity = 0;
int ferry_side = SIDE_A;
int total_returned = 0;
int current_trip_id = 0;
int is_first_return = 1;
int final_trip_done = 0;
int boarded_ids[20];
int boarded_count = 0;
double wait_time_a[TOTAL_VEHICLES], wait_time_b[TOTAL_VEHICLES];
double ferry_time_a[TOTAL_VEHICLES], ferry_time_b[TOTAL_VEHICLES];
double round_trip_time[TOTAL_VEHICLES];
time_t sim_start_time;

// Synchronization primitives
pthread_mutex_t boarding_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t return_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t toll_sem[4]; // 0-1: Side-A tolls, 2-3: Side-B tolls

// Helper functions
const char* get_type_name(int type) {
    switch (type) {
        case 0: return "\033[32m🟢 Car\033[0m";
        case 1: return "\033[34m🔵 Minibus\033[0m";
        case 2: return "\033[33m🟡 Truck\033[0m";
        default: return "Unknown";
    }
}

int can_fill_remaining(int remaining_capacity) {
    int dp[FERRY_CAPACITY + 1];
    memset(dp, 0, sizeof(dp));
    dp[0] = 1;
    
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == ferry_side && vehicles[i].boarded < 2 && !vehicles[i].returned) {
            int cap = vehicles[i].capacity;
            for (int j = FERRY_CAPACITY; j >= cap; j--) {
                if (dp[j - cap]) dp[j] = 1;
            }
        }
    }
    return dp[remaining_capacity];
}

void log_trip(int direction, double duration, int *ids, int count, int capacity) {
    pthread_mutex_lock(&log_mutex);
    Trip *t = &trip_log[trip_count++];
    t->trip_id = current_trip_id;
    t->direction = direction;
    t->duration = duration;
    t->vehicle_count = count;
    t->capacity_used = capacity;
    memcpy(t->vehicle_ids, ids, sizeof(int) * count);
    pthread_mutex_unlock(&log_mutex);
}

void print_state(int wait_counter) {
    pthread_mutex_lock(&print_mutex);
    printf("\033[H\033[J"); // Clear screen
    printf("\033[1m┏━━━━━━━━━━━━━━━━━━━━━━━━━ Ferry Simulation ━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    printf("┃ Progress: %3.1f%% | Time Elapsed: %ld s | Trip: %d/%d \033[0m┃\n",
           (total_returned / (float)TOTAL_VEHICLES) * 100, time(NULL) - sim_start_time,
           current_trip_id, MAX_TRIPS);
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ \033[31m🅰 Side-A\033[0m      \033[36m⛴️ Ferry (%s)\033[0m      \033[31mSide-B 🅱\033[0m ┃\n",
           ferry_side == SIDE_A ? "\033[32m→→→ A→B\033[0m" : "\033[32m←←← B→A\033[0m");
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");

    // Side-A vehicles
    printf("┃ Side-A: ");
    int count_a = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == SIDE_A && !vehicles[i].returned && vehicles[i].boarded < 2) {
            printf("⏳%s%d ", get_type_name(vehicles[i].type), vehicles[i].id);
            count_a++;
        }
    }
    for (int i = count_a; i < 8; i++) printf("          ");
    printf("┃\n");

    // Ferry vehicles
    printf("┃ Ferry : ");
    for (int i = 0; i < boarded_count; i++) {
        for (int j = 0; j < TOTAL_VEHICLES; j++) {
            if (vehicles[j].id == boarded_ids[i]) {
                printf("🚢%s%d ", get_type_name(vehicles[j].type), vehicles[j].id);
                break;
            }
        }
    }
    for (int i = boarded_count; i < 8; i++) printf("          ");
    printf("┃\n");

    // Side-B vehicles
    printf("┃ Side-B: ");
    int count_b = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == SIDE_B && !vehicles[i].returned && vehicles[i].boarded < 2) {
            printf("⏳%s%d ", get_type_name(vehicles[i].type), vehicles[i].id);
            count_b++;
        }
    }
    for (int i = count_b; i < 8; i++) printf("          ");
    printf("┃\n");

    // Starting points
    printf("┃ Start : ");
    int count_s = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (!vehicles[i].returned && vehicles[i].boarded < 2) {
            printf("%c%-9d ", vehicles[i].start_port == SIDE_A ? 'A' : 'B', vehicles[i].id);
            count_s++;
        }
    }
    for (int i = count_s; i < 8; i++) printf("          ");
    printf("┃\n");

    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ Returned: %2d/%-2d | Load: %2d/%-2d | Wait: %.1fs | Time: %3ld \033[0m┃\n",
           total_returned, TOTAL_VEHICLES, ferry_capacity, FERRY_CAPACITY,
           wait_counter * 0.25, time(NULL) % 1000);
    printf("\033[1m┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n\033[0m");
    pthread_mutex_unlock(&print_mutex);
}

void write_log_file() {
    FILE *fp = fopen("ferry_log.txt", "w");
    if (!fp) return;

    fprintf(fp, "=== Ferry Simulation Log ===\n");
    fprintf(fp, "Total Vehicles: %d | Ferry Capacity: %d | Total Trips: %d\n", TOTAL_VEHICLES, FERRY_CAPACITY, trip_count);
    fprintf(fp, "Simulation Duration: %ld seconds\n\n", time(NULL) - sim_start_time);

    fprintf(fp, "=== Trip Summary ===\n");
    for (int i = 0; i < trip_count; i++) {
        Trip *t = &trip_log[i];
        fprintf(fp, "Trip %d [%s]: %.2fs | Capacity: %d/%d (%.1f%%) | Vehicles: ",
                t->trip_id, t->direction == SIDE_A ? "A->B" : "B->A", t->duration,
                t->capacity_used, FERRY_CAPACITY, (t->capacity_used / (float)FERRY_CAPACITY) * 100);
        for (int j = 0; j < t->vehicle_count; j++) {
            for (int k = 0; k < TOTAL_VEHICLES; k++) {
                if (vehicles[k].id == t->vehicle_ids[j]) {
                    fprintf(fp, "%s%d ", get_type_name(vehicles[k].type) + 7, t->vehicle_ids[j]);
                    break;
                }
            }
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "\n=== Vehicle Statistics ===\n");
    fprintf(fp, "ID | Type      | Start | Wait A | Wait B | Ferry A | Ferry B | Round Trip\n");
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        fprintf(fp, "%2d | %-9s | %-5c | %6.1fs | %6.1fs | %7.1fs | %7.1fs | %8.1fs\n",
                vehicles[i].id, get_type_name(vehicles[i].type) + 7,
                vehicles[i].start_port == SIDE_A ? 'A' : 'B',
                wait_time_a[i], wait_time_b[i], ferry_time_a[i], ferry_time_b[i], round_trip_time[i]);
    }

    fclose(fp);
}

void show_final_statistics() {
    double total_wait_a = 0, total_wait_b = 0, total_ferry_a = 0, total_ferry_b = 0;
    double car_wait = 0, minibus_wait = 0, truck_wait = 0;
    double start_a_wait = 0, start_b_wait = 0;
    double total_round_trip = 0, max_round_trip = 0, min_round_trip = 1e9;
    int car_count = 0, minibus_count = 0, truck_count = 0;
    int start_a_count = 0, start_b_count = 0;
    double total_duration = 0, total_capacity_used = 0;
    double max_wait = 0, min_wait = 1e9;
    int empty_trips = 0;

    printf("\n\033[1m┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ Final Statistics ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    printf("┃ Simulation Complete! Total Duration: %ld s | Total Trips: %d \033[0m┃\n",
           time(NULL) - sim_start_time, trip_count);
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ ID | Type      | Start | Wait A | Wait B | Ferry A | Ferry B | Round Trip \033[0m┃\n");
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");

    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        double total_wait = wait_time_a[i] + wait_time_b[i];
        printf("┃ %2d | %-9s | %-5c | %6.1fs | %6.1fs | %7.1fs | %7.1fs | %8.1fs \033[0m┃\n",
               vehicles[i].id, get_type_name(vehicles[i].type) + 7,
               vehicles[i].start_port == SIDE_A ? 'A' : 'B',
               wait_time_a[i], wait_time_b[i], ferry_time_a[i], ferry_time_b[i], round_trip_time[i]);
        total_wait_a += wait_time_a[i];
        total_wait_b += wait_time_b[i];
        total_ferry_a += ferry_time_a[i];
        total_ferry_b += ferry_time_b[i];
        total_round_trip += round_trip_time[i];
        if (round_trip_time[i] > max_round_trip) max_round_trip = round_trip_time[i];
        if (round_trip_time[i] < min_round_trip && round_trip_time[i] > 0) min_round_trip = round_trip_time[i];
        if (vehicles[i].type == 0) {
            car_wait += total_wait;
            car_count++;
        } else if (vehicles[i].type == 1) {
            minibus_wait += total_wait;
            minibus_count++;
        } else {
            truck_wait += total_wait;
            truck_count++;
        }
        if (vehicles[i].start_port == SIDE_A) {
            start_a_wait += total_wait;
            start_a_count++;
        } else {
            start_b_wait += total_wait;
            start_b_count++;
        }
        if (total_wait > max_wait) max_wait = total_wait;
        if (total_wait < min_wait && total_wait > 0) min_wait = total_wait;
    }

    for (int i = 0; i < trip_count; i++) {
        total_duration += trip_log[i].duration;
        total_capacity_used += trip_log[i].capacity_used;
        if (trip_log[i].vehicle_count == 0) empty_trips++;
    }

    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ \033[1mAvg Wait A: %.2fs | Avg Wait B: %.2fs | Avg Ferry A: %.2fs | Avg Ferry B: %.2fs\033[0m ┃\n",
           total_wait_a / TOTAL_VEHICLES, total_wait_b / TOTAL_VEHICLES,
           total_ferry_a / TOTAL_VEHICLES, total_ferry_b / TOTAL_VEHICLES);
    printf("┃ \033[1mAvg Wait by Type: Car: %.2fs | Minibus: %.2fs | Truck: %.2fs\033[0m ┃\n",
           car_count ? car_wait / car_count : 0,
           minibus_count ? minibus_wait / minibus_count : 0,
           truck_count ? truck_wait / truck_count : 0);
    printf("┃ \033[1mAvg Wait by Start: A: %.2fs | B: %.2fs\033[0m ┃\n",
           start_a_count ? start_a_wait / start_a_count : 0,
           start_b_count ? start_b_wait / start_b_count : 0);
    printf("┃ \033[1mAvg Round Trip: %.2fs | Max: %.2fs | Min: %.2fs\033[0m ┃\n",
           total_round_trip / TOTAL_VEHICLES, max_round_trip, min_round_trip);
    printf("┃ \033[1mFerry Utilization: %.2f%% | Empty Trips: %d (%.2f%%)\033[0m ┃\n",
           (total_capacity_used / (trip_count * FERRY_CAPACITY)) * 100,
           empty_trips, (empty_trips / (float)trip_count) * 100);
    printf("┃ \033[1mStarvation Risk: %s (Max Wait: %.2fs, Min Wait: %.2fs, Ratio: %.2f)\033[0m ┃\n",
           max_wait / (min_wait ? min_wait : 1) > 3 ? "\033[31mHigh\033[0m" : "\033[32mLow\033[0m",
           max_wait, min_wait, max_wait / (min_wait ? min_wait : 1));
    printf("\033[1m┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n\033[0m");

    write_log_file();
}

void* vehicle_func(void* arg) {
    Vehicle *v = (Vehicle*)arg;
    v->trip_start = time(NULL);

    while (v->boarded < 2) {
        // Toll booth access
        int toll_index = (v->port == SIDE_A) ? rand() % TOLL_PER_SIDE : TOLL_PER_SIDE + (rand() % TOLL_PER_SIDE);
        sem_wait(&toll_sem[toll_index]);
        
        if (v->port == SIDE_A) v->wait_start_a = time(NULL);
        else v->wait_start_b = time(NULL);

        pthread_mutex_lock(&print_mutex);
        printf("🚗 %s %d passed toll at Side-%c\n", get_type_name(v->type) + 7, v->id,
               v->port == SIDE_A ? 'A' : 'B');
        pthread_mutex_unlock(&print_mutex);
        usleep(100000); // Simulate toll payment
        sem_post(&toll_sem[toll_index]);

        // Wait for boarding
        int boarded = 0;
        while (!boarded && !final_trip_done) {
            pthread_mutex_lock(&boarding_mutex);
            if (ferry_side == v->port && ferry_capacity + v->capacity <= FERRY_CAPACITY &&
                (!is_first_return || v->port == SIDE_A) &&
                (v->port == SIDE_B ? current_trip_id >= v->b_trip_no + 2 : 1)) {
                if (v->port == SIDE_A) {
                    v->wait_end_a = time(NULL);
                    wait_time_a[v->id - 1] = difftime(v->wait_end_a, v->wait_start_a);
                    v->ferry_start_a = time(NULL);
                } else {
                    v->wait_end_b = time(NULL);
                    wait_time_b[v->id - 1] = difftime(v->wait_end_b, v->wait_start_b);
                    v->ferry_start_b = time(NULL);
                }
                ferry_capacity += v->capacity;
                v->boarded++;
                boarded_ids[boarded_count++] = v->id;
                if (v->port == SIDE_A) v->a_trip_no = current_trip_id;
                else v->b_trip_no = current_trip_id;
                boarded = 1;
                pthread_mutex_lock(&print_mutex);
                printf("✅ %s %d boarded ferry at Side-%c (Capacity: %d)\n",
                       get_type_name(v->type) + 7, v->id, v->port == SIDE_A ? 'A' : 'B', ferry_capacity);
                pthread_mutex_unlock(&print_mutex);
            }
            pthread_mutex_unlock(&boarding_mutex);
            if (!boarded) usleep(200000);
        }

        if (final_trip_done) break;

        // Wait for ferry to arrive
        while (ferry_side == v->port && !final_trip_done) usleep(100000);

        if (v->port == SIDE_A) {
            v->ferry_end_b = time(NULL);
            ferry_time_b[v->id - 1] = difftime(v->ferry_end_b, v->ferry_start_b);
        } else {
            v->ferry_end_a = time(NULL);
            ferry_time_a[v->id - 1] = difftime(v->ferry_end_a, v->ferry_start_a);
        }
        v->port = 1 - v->port;

        // Random wait time at destination (1-5 seconds)
        if (v->boarded == 1) usleep((1 + rand() % 5) * 1000000);

        if (v->boarded == 2) {
            pthread_mutex_lock(&return_mutex);
            v->returned = 1;
            v->trip_end = time(NULL);
            round_trip_time[v->id - 1] = difftime(v->trip_end, v->trip_start);
            total_returned++;
            pthread_mutex_lock(&print_mutex);
            printf("🏁 %s %d completed round-trip in %.1fs\n",
                   get_type_name(v->type) + 7, v->id, round_trip_time[v->id - 1]);
            pthread_mutex_unlock(&print_mutex);
            pthread_mutex_unlock(&return_mutex);
        }
    }
    return NULL;
}

void* ferry_func(void* arg) {
    int wait_counter = 0;
    while (!final_trip_done) {
        time_t start_time = time(NULL);
        int should_depart = 0;

        while (!should_depart) {
            pthread_mutex_lock(&boarding_mutex);
            int vehicles_waiting = 0;
            for (int i = 0; i < TOTAL_VEHICLES; i++) {
                if (vehicles[i].port == ferry_side && !vehicles[i].returned && vehicles[i].boarded < 2 &&
                    (ferry_side != SIDE_B || current_trip_id >= vehicles[i].b_trip_no + 2)) {
                    vehicles_waiting = 1;
                    break;
                }
            }
            if (ferry_capacity >= FERRY_CAPACITY ||
                !vehicles_waiting ||
                wait_counter >= 10 ||
                (is_first_return && ferry_side == SIDE_B)) {
                should_depart = 1;
                if (!vehicles_waiting && ferry_capacity == 0 && !(is_first_return && ferry_side == SIDE_B)) {
                    printf("🚨 No vehicles waiting at Side-%c, ferry waiting...\n",
                           ferry_side == SIDE_A ? 'A' : 'B');
                }
            }
            wait_counter++;
            pthread_mutex_unlock(&boarding_mutex);
            if (!should_depart) usleep(250000);
        }

        pthread_mutex_lock(&boarding_mutex);
        if (ferry_capacity > 0 || (is_first_return && ferry_side == SIDE_B)) {
            printf("\n\033[36m⛴️ Ferry departing from Side-%c to Side-%c with %d units\033[0m\n",
                   ferry_side == SIDE_A ? 'A' : 'B',
                   ferry_side == SIDE_A ? 'B' : 'A',
                   ferry_capacity);
        }

        double duration = 2 + (rand() % 8);
        log_trip(ferry_side, duration, boarded_ids, boarded_count, ferry_capacity);
        current_trip_id++;
        usleep(duration * 1000000);

        ferry_side = 1 - ferry_side;
        ferry_capacity = 0;
        wait_counter = 0;
        boarded_count = 0;
        memset(boarded_ids, 0, sizeof(boarded_ids));

        if (is_first_return && ferry_side == SIDE_A) is_first_return = 0;

        pthread_mutex_lock(&return_mutex);
        if (total_returned >= TOTAL_VEHICLES && ferry_side == SIDE_A) {
            final_trip_done = 1;
        }
        pthread_mutex_unlock(&return_mutex);
        pthread_mutex_unlock(&boarding_mutex);
    }

    // Print trip summary
    printf("\n\033[1m┏━━━━━━━━━━━━━━━━━━━━━━━ Trip Summary ━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    for (int i = 0; i < trip_count; i++) {
        Trip *t = &trip_log[i];
        printf("┃ Trip %d [%s]: %.2fs | Capacity: %d/%d (%.1f%%) | Vehicles: ",
               t->trip_id, t->direction == SIDE_A ? "A->B" : "B->A", t->duration,
               t->capacity_used, FERRY_CAPACITY, (t->capacity_used / (float)FERRY_CAPACITY) * 100);
        for (int j = 0; j < t->vehicle_count; j++) {
            for (int k = 0; k < TOTAL_VEHICLES; k++) {
                if (vehicles[k].id == t->vehicle_ids[j]) {
                    printf("%s%d ", get_type_name(vehicles[k].type) + 7, t->vehicle_ids[j]);
                    break;
                }
            }
        }
        printf("%*s┃\n", 30 - t->vehicle_count * 8, "");
    }
    printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n\033[0m");
    return NULL;
}

void* print_state_thread(void* arg) {
    int wait_counter = 0;
    while (!final_trip_done) {
        print_state(wait_counter);
        wait_counter++;
        usleep(500000); // Update every 0.5 seconds
    }
    print_state(wait_counter); // Final state
    return NULL;
}

int main() {
    srand(time(NULL));
    sim_start_time = time(NULL);
    pthread_t vehicle_threads[TOTAL_VEHICLES];
    pthread_t ferry_thread, printer_thread;

    // Initialize semaphores
    for (int i = 0; i < 4; i++) sem_init(&toll_sem[i], 0, 1);

    // Initialize vehicles with random starting side
    int id = 1;
    for (int i = 0; i < CAR_COUNT; i++) {
        int start = rand() % 2;
        vehicles[i] = (Vehicle){id++, 0, 1, start, start, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0};
    }
    for (int i = 0; i < MINIBUS_COUNT; i++) {
        int start = rand() % 2;
        vehicles[CAR_COUNT + i] = (Vehicle){id++, 1, 2, start, start, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0};
    }
    for (int i = 0; i < TRUCK_COUNT; i++) {
        int start = rand() % 2;
        vehicles[CAR_COUNT + MINIBUS_COUNT + i] = (Vehicle){id++, 2, 4, start, start, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0};
    }

    // Initialize time arrays
    memset(wait_time_a, 0, sizeof(wait_time_a));
    memset(wait_time_b, 0, sizeof(wait_time_b));
    memset(ferry_time_a, 0, sizeof(ferry_time_a));
    memset(ferry_time_b, 0, sizeof(ferry_time_b));
    memset(round_trip_time, 0, sizeof(round_trip_time));

    // Create threads
    pthread_create(&ferry_thread, NULL, ferry_func, NULL);
    pthread_create(&printer_thread, NULL, print_state_thread, NULL);
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_create(&vehicle_threads[i], NULL, vehicle_func, &vehicles[i]);

    // Join threads
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_join(vehicle_threads[i], NULL);
    pthread_join(ferry_thread, NULL);
    pthread_join(printer_thread, NULL);

    // Print final statistics
    show_final_statistics();

    // Cleanup
    for (int i = 0; i < 4; i++) sem_destroy(&toll_sem[i]);
    pthread_mutex_destroy(&boarding_mutex);
    pthread_mutex_destroy(&return_mutex);
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&print_mutex);

    printf("\n\033[1m✅ Simulation completed. Log saved to ferry_log.txt.\033[0m\n");
    return 0;
}