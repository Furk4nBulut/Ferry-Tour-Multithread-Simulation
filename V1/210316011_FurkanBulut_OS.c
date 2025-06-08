#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define CAR_COUNT 12
#define MINIBUS_COUNT 10
#define TRUCK_COUNT 8
#define FERRY_CAPACITY 20
#define SIDE_A 0 // Starting side
#define SIDE_B 1 // Opposite side
#define TOLL_COUNT 2 // 2 tolls per side
#define MAX_WAITING_TIME 5 // Max waiting time for ferry (seconds)

typedef struct {
    int id;
    int type;        // 0: Car, 1: Minibus, 2: Truck
    int port;        // 0: Starting port, 1: Opposite port
    int toll;        // 0-1: Starting port tolls, 2-3: Opposite port tolls
    int capacity;    // 1: Car, 2: Minibus, 3: Truck
    int boarded;     // How many times boarded the ferry
} Vehicle;

Vehicle vehicles[CAR_COUNT + MINIBUS_COUNT + TRUCK_COUNT];
pthread_mutex_t ferry_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t toll_lock[4] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
                                PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
pthread_cond_t ferry_full = PTHREAD_COND_INITIALIZER;
int ferry_capacity = 0;
int ferry_side = 0; // Will be set randomly in main
int ferry_ready = 0;
int vehicles_finished = 0;
int total_vehicles = CAR_COUNT + MINIBUS_COUNT + TRUCK_COUNT;

const char* get_vehicle_type(int type) {
    static const char* types[] = {"Car", "Minibus", "Truck"};
    return types[type];
}

void print_progress_bar(int current, int total) {
    int progress = (current * 50) / total;
    printf("[");
    for (int i = 0; i < 50; i++) {
        if (i < progress) printf("#");
        else printf("-");
    }
    printf("] %d/%d\n", current, total);
}

void* vehicle_thread(void* arg) {
    Vehicle* v = (Vehicle*)arg;

    while (v->boarded < 2) { // Each vehicle will board twice (round trip)
        // Pass through toll
        int toll_no = (v->port == SIDE_A) ? rand() % TOLL_COUNT : (2 + rand() % TOLL_COUNT);
        v->toll = toll_no;
        pthread_mutex_lock(&toll_lock[toll_no]);
        printf("🚗 %s %d is passing through toll %d at %s port.\n",
               get_vehicle_type(v->type), v->id, toll_no % 2, v->port == SIDE_A ? "Starting" : "Opposite");
        usleep(100000); // Toll passing time
        pthread_mutex_unlock(&toll_lock[toll_no]);

        // Enter waiting area
        pthread_mutex_lock(&ferry_lock);
        printf("🚗 %s %d entered the waiting area at %s port.\n",
               get_vehicle_type(v->type), v->id, v->port == SIDE_A ? "Starting" : "Opposite");
        pthread_mutex_unlock(&ferry_lock);

        // Board the ferry
        pthread_mutex_lock(&ferry_lock);
        while (!(ferry_side == v->port && ferry_capacity + v->capacity <= FERRY_CAPACITY) && !vehicles_finished) {
            pthread_mutex_unlock(&ferry_lock);
            usleep(100000);
            pthread_mutex_lock(&ferry_lock);
        }

        if (vehicles_finished) {
            pthread_mutex_unlock(&ferry_lock);
            break;
        }

        ferry_capacity += v->capacity;
        v->boarded++;
        printf("🚗 %s %d boarded the ferry from %s port. (Capacity: %d/%d)\n",
               get_vehicle_type(v->type), v->id, v->port == SIDE_A ? "Starting" : "Opposite",
               ferry_capacity, FERRY_CAPACITY);

        if (ferry_capacity >= FERRY_CAPACITY) {
            ferry_ready = 1;
            pthread_cond_signal(&ferry_full);
        }
        pthread_mutex_unlock(&ferry_lock);

        // Wait to cross
        while (ferry_side == v->port) usleep(1000);

        // Arrived at the other port
        v->port = !v->port;
        usleep((1 + rand() % 3) * 1000000); // Wait at the other port
    }

    return NULL;
}

void* ferry_thread(void* arg) {
    time_t last_departure = time(NULL);
    while (!vehicles_finished || ferry_capacity > 0) {
        pthread_mutex_lock(&ferry_lock);

        // Departure conditions: if full or time is up
        time_t now = time(NULL);
        if (!ferry_ready && (now - last_departure < MAX_WAITING_TIME) && !vehicles_finished) {
            pthread_mutex_unlock(&ferry_lock);
            usleep(100000);
            continue;
        }

        if (ferry_capacity > 0) {
            printf("\n⛴️ Ferry is departing from %s to %s... (%d/%d)\n",
                   ferry_side == SIDE_A ? "Starting" : "Opposite",
                   ferry_side == SIDE_A ? "Opposite" : "Starting",
                   ferry_capacity, FERRY_CAPACITY);
            print_progress_bar(ferry_capacity, FERRY_CAPACITY);

            // Travel time
            double travel_time = 5 + rand() % 5;
            usleep(travel_time * 1000000);

            // Arrived at the other side
            ferry_side = !ferry_side;
            ferry_capacity = 0;
            ferry_ready = 0;
            last_departure = time(NULL);

            printf("🛬 Ferry arrived at %s.\n", ferry_side == SIDE_A ? "Starting" : "Opposite");
        }
        pthread_mutex_unlock(&ferry_lock);
        usleep(100000);
    }

    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t threads[CAR_COUNT + MINIBUS_COUNT + TRUCK_COUNT];
    pthread_t ferry;

    // Set random starting side for the ferry
    ferry_side = rand() % 2;

    // Create vehicles
    int index = 0;
    for (int i = 0; i < CAR_COUNT; i++)
        vehicles[index++] = (Vehicle){index, 0, rand() % 2, 0, 1, 0}; // Car
    for (int i = 0; i < MINIBUS_COUNT; i++)
        vehicles[index++] = (Vehicle){index, 1, rand() % 2, 0, 2, 0}; // Minibus
    for (int i = 0; i < TRUCK_COUNT; i++)
        vehicles[index++] = (Vehicle){index, 2, rand() % 2, 0, 3, 0}; // Truck

    // Start threads
    pthread_create(&ferry, NULL, ferry_thread, NULL);
    for (int i = 0; i < total_vehicles; i++) {
        pthread_create(&threads[i], NULL, vehicle_thread, &vehicles[i]);
    }

    // Wait for threads to finish
    for (int i = 0; i < total_vehicles; i++) {
        pthread_join(threads[i], NULL);
    }

    vehicles_finished = 1;
    pthread_join(ferry, NULL);

    printf("Simulation completed!\n");
    return 0;
}