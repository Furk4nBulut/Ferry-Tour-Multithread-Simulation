#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define CAR_COUNT 12
#define MINIBUS_COUNT 10
#define TRUCK_COUNT 8
#define TOTAL_VEHICLES (CAR_COUNT + MINIBUS_COUNT + TRUCK_COUNT)
#define FERRY_CAPACITY 20
#define TOLL_PER_SIDE 2
#define MAX_TRIPS 100
#define SIDE_A 0
#define SIDE_B 1

typedef struct {
    int id;
    int type; // 0: Car, 1: Minibus, 2: Truck
    int capacity; // 1, 2, 3
    int port; // SIDE_A or SIDE_B
    int boarded; // 0, 1, 2
    int a_trip_no, b_trip_no; // Trip numbers for A->B and B->A
    int returned; // 1 if vehicle completed round-trip
    time_t wait_start_a, wait_end_a; // Side-A wait times
    time_t wait_start_b, wait_end_b; // Side-B wait times
    time_t ferry_start, ferry_end; // Ferry travel times
} Vehicle;

typedef struct {
    int trip_id;
    int direction; // 0: A->B, 1: B->A
    double duration;
    int vehicle_ids[20];
    int vehicle_count;
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
double wait_time_a[TOTAL_VEHICLES], wait_time_b[TOTAL_VEHICLES], ferry_time[TOTAL_VEHICLES];

// Synchronization primitives
pthread_mutex_t boarding_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t return_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t toll_sem[4]; // 0-1: Side-A tolls, 2-3: Side-B tolls

// Helper functions
const char* get_type_name(int type) {
    switch (type) {
        case 0: return "Car";
        case 1: return "Minibus";
        case 2: return "Truck";
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

void log_trip(int direction, double duration, int *ids, int count) {
    pthread_mutex_lock(&log_mutex);
    Trip *t = &trip_log[trip_count++];
    t->trip_id = current_trip_id;
    t->direction = direction;
    t->duration = duration;
    t->vehicle_count = count;
    memcpy(t->vehicle_ids, ids, sizeof(int) * count);
    pthread_mutex_unlock(&log_mutex);
}

void print_state() {
    pthread_mutex_lock(&print_mutex);
    printf("\033[H\033[J"); // Clear screen
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("🅰 Side-A         ⛴️ Ferry (%s)         Side-B 🅱\n",
           ferry_side == SIDE_A ? "→→→ A→B" : "←←← B→A");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // Side-A vehicles
    printf("Side-A: ");
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == SIDE_A && !vehicles[i].returned && vehicles[i].boarded < 2) {
            printf("%s%d ", get_type_name(vehicles[i].type), vehicles[i].id);
        }
    }
    printf("\n");

    // Ferry vehicles
    printf("Ferry : ");
    for (int i = 0; i < boarded_count; i++) {
        for (int j = 0; j < TOTAL_VEHICLES; j++) {
            if (vehicles[j].id == boarded_ids[i]) {
                printf("%s%d ", get_type_name(vehicles[j].type), vehicles[j].id);
                break;
            }
        }
    }
    printf("\n");

    // Side-B vehicles
    printf("Side-B: ");
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == SIDE_B && !vehicles[i].returned && vehicles[i].boarded < 2) {
            printf("%s%d ", get_type_name(vehicles[i].type), vehicles[i].id);
        }
    }
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Returned: %d/%d | Ferry Load: %d/%d\n", total_returned, TOTAL_VEHICLES, ferry_capacity, FERRY_CAPACITY);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    pthread_mutex_unlock(&print_mutex);
}

void show_final_statistics() {
    double total_wait_a = 0, total_wait_b = 0, total_ferry = 0;
    double total_duration = 0;
    int total_capacity_used = 0;

    printf("\n─────────────────────────────────────────────\n");
    printf("Simulation Complete! Final Statistics:\n");
    printf("─────────────────────────────────────────────\n");
    printf("Vehicle ID | Type    | Wait A | Wait B | Ferry Time\n");
    printf("----------------------------------------------------\n");

    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        printf("%-10d | %-7s | %.1fs  | %.1fs  | %.1fs\n",
               vehicles[i].id, get_type_name(vehicles[i].type),
               wait_time_a[i], wait_time_b[i], ferry_time[i]);
        total_wait_a += wait_time_a[i];
        total_wait_b += wait_time_b[i];
        total_ferry += ferry_time[i];
    }

    for (int i = 0; i < trip_count; i++) {
        total_duration += trip_log[i].duration;
        for (int j = 0; j < trip_log[i].vehicle_count; j++) {
            for (int k = 0; k < TOTAL_VEHICLES; k++) {
                if (vehicles[k].id == trip_log[i].vehicle_ids[j]) {
                    total_capacity_used += vehicles[k].capacity;
                }
            }
        }
    }

    printf("----------------------------------------------------\n");
    printf("Average Wait A: %.2fs | Wait B: %.2fs | Ferry: %.2fs\n",
           total_wait_a / TOTAL_VEHICLES, total_wait_b / TOTAL_VEHICLES, total_ferry / TOTAL_VEHICLES);
    printf("Total Simulation Time: %.2fs\n", total_duration);
    printf("Ferry Utilization: %.2f%%\n", (total_capacity_used / (double)(trip_count * FERRY_CAPACITY)) * 100);

    // Starvation analysis
    double max_wait = 0, min_wait = 1e9;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        double total_wait = wait_time_a[i] + wait_time_b[i];
        if (total_wait > max_wait) max_wait = total_wait;
        if (total_wait < min_wait) min_wait = total_wait;
    }
    printf("Starvation Risk: %s (Max Wait: %.2fs, Min Wait: %.2fs)\n",
           max_wait / min_wait > 3 ? "High" : "Low", max_wait, min_wait);
    printf("─────────────────────────────────────────────\n");
}

void* vehicle_func(void* arg) {
    Vehicle *v = (Vehicle*)arg;

    while (v->boarded < 2) {
        // Toll booth access
        int toll_index = (v->port == SIDE_A) ? rand() % TOLL_PER_SIDE : TOLL_PER_SIDE + (rand() % TOLL_PER_SIDE);
        sem_wait(&toll_sem[toll_index]);
        
        if (v->port == SIDE_A) v->wait_start_a = time(NULL);
        else v->wait_start_b = time(NULL);

        pthread_mutex_lock(&print_mutex);
        printf("🚗 %s %d passed toll at Side-%c\n", get_type_name(v->type), v->id,
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
                } else {
                    v->wait_end_b = time(NULL);
                    wait_time_b[v->id - 1] = difftime(v->wait_end_b, v->wait_start_b);
                }
                ferry_capacity += v->capacity;
                v->ferry_start = time(NULL);
                v->boarded++;
                boarded_ids[boarded_count++] = v->id;
                if (v->port == SIDE_A) v->a_trip_no = current_trip_id;
                else v->b_trip_no = current_trip_id;
                boarded = 1;
                pthread_mutex_lock(&print_mutex);
                printf("✅ %s %d boarded ferry at Side-%c (Capacity: %d)\n",
                       get_type_name(v->type), v->id, v->port == SIDE_A ? 'A' : 'B', ferry_capacity);
                pthread_mutex_unlock(&print_mutex);
            }
            pthread_mutex_unlock(&boarding_mutex);
            if (!boarded) usleep(200000);
        }

        if (final_trip_done) break;

        // Wait for ferry to arrive
        while (ferry_side == v->port && !final_trip_done) usleep(100000);

        v->ferry_end = time(NULL);
        ferry_time[v->id - 1] += difftime(v->ferry_end, v->ferry_start);
        v->port = 1 - v->port;

        if (v->boarded == 2) {
            pthread_mutex_lock(&return_mutex);
            v->returned = 1;
            total_returned++;
            pthread_mutex_lock(&print_mutex);
            printf("🏁 %s %d completed round-trip\n", get_type_name(v->type), v->id);
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
            if (ferry_capacity >= FERRY_CAPACITY ||
                !can_fill_remaining(FERRY_CAPACITY - ferry_capacity) ||
                wait_counter >= 20 ||
                (is_first_return && ferry_side == SIDE_B) ||
                (total_returned < TOTAL_VEHICLES && ferry_side == SIDE_A && !can_fill_remaining(FERRY_CAPACITY))) {
                should_depart = 1;
            }
            wait_counter++;
            pthread_mutex_unlock(&boarding_mutex);
            if (!should_depart) usleep(250000);
        }

        pthread_mutex_lock(&boarding_mutex);
        printf("\n⛴️ Ferry departing from Side-%c to Side-%c with %d units\n",
               ferry_side == SIDE_A ? 'A' : 'B',
               ferry_side == SIDE_A ? 'B' : 'A',
               ferry_capacity);

        double duration = 2 + (rand() % 8);
        log_trip(ferry_side, duration, boarded_ids, boarded_count);
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
    printf("\n📦 Trip Summary:\n");
    for (int i = 0; i < trip_count; i++) {
        Trip *t = &trip_log[i];
        printf("Trip %d [%s]: %.2fs | Vehicles: ", t->trip_id,
               t->direction == SIDE_A ? "A→B" : "B→A", t->duration);
        for (int j = 0; j < t->vehicle_count; j++) {
            for (int k = 0; k < TOTAL_VEHICLES; k++) {
                if (vehicles[k].id == t->vehicle_ids[j]) {
                    printf("%s%d ", get_type_name(vehicles[k].type), t->vehicle_ids[j]);
                    break;
                }
            }
        }
        printf("\n");
    }
    return NULL;
}

void* print_state_thread(void* arg) {
    while (!final_trip_done) {
        print_state();
        usleep(500000); // Update every 0.5 seconds
    }
    print_state(); // Final state
    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t vehicle_threads[TOTAL_VEHICLES];
    pthread_t ferry_thread, printer_thread;

    // Initialize semaphores
    for (int i = 0; i < 4; i++) sem_init(&toll_sem[i], 0, 1);

    // Initialize vehicles
    int id = 1;
    for (int i = 0; i < CAR_COUNT; i++)
        vehicles[i] = (Vehicle){id++, 0, 1, SIDE_A, 0, -1, -1, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < MINIBUS_COUNT; i++)
        vehicles[CAR_COUNT + i] = (Vehicle){id++, 1, 2, SIDE_A, 0, -1, -1, 0, 0, 0, 0, 0};
    for (int i = 0; i < TRUCK_COUNT; i++)
        vehicles[CAR_COUNT + MINIBUS_COUNT + i] = (Vehicle){id++, 2, 3, SIDE_A, 0, -1, -1, 0, 0, 0, 0, 0};

    // Initialize time arrays
    memset(wait_time_a, 0, sizeof(wait_time_a));
    memset(wait_time_b, 0, sizeof(wait_time_b));
    memset(ferry_time, 0, sizeof(ferry_time));

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

    printf("\n✅ Simulation completed.\n");
    return 0;
}