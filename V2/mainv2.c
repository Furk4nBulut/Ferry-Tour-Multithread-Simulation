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
    int type; // 0: car, 1: minibus, 2: truck
    int capacity;
    int port; // current side
    int boarded; // 0, 1, 2
    int a_trip_no, b_trip_no;
    time_t wait_start, wait_end;
    time_t ferry_start, ferry_end;
    int returned;
} Vehicle;

typedef struct {
    int trip_id;
    int direction; // 0: A->B, 1: B->A
    double duration;
    int vehicle_ids[20];
    int vehicle_count;
} Trip;

Vehicle vehicles[TOTAL_VEHICLES];
Trip trip_log[MAX_TRIPS];
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

void log_trip(int direction, double duration, int *ids, int count) {
    pthread_mutex_lock(&log_mutex);
    Trip *t = &trip_log[trip_count++];
    t->trip_id = current_trip_id++;
    t->direction = direction;
    t->duration = duration;
    t->vehicle_count = count;
    memcpy(t->vehicle_ids, ids, sizeof(int) * count);
    pthread_mutex_unlock(&log_mutex);
}

const char* get_type_name(int type) {
    if (type == 0) return "Car";
    if (type == 1) return "Minibus";
    return "Truck";
}

void* vehicle_func(void* arg) {
    Vehicle *v = (Vehicle*)arg;
    while (v->boarded < 2) {
        int toll_index = (v->port == SIDE_A) ? rand() % 2 : 2 + (rand() % 2);
        sem_wait(&toll_sem[toll_index]);

        pthread_mutex_lock(&print_mutex);
        printf("🚗 %s %d passed toll at Side-%c\n", get_type_name(v->type), v->id,
               v->port == SIDE_A ? 'A' : 'B');
        pthread_mutex_unlock(&print_mutex);

        sem_post(&toll_sem[toll_index]);

        sleep(1); // Wait in square

        int boarded = 0;
        while (!boarded) {
            pthread_mutex_lock(&boarding_mutex);
            if (ferry_side == v->port && ferry_capacity + v->capacity <= FERRY_CAPACITY &&
                (!is_first_return || v->port == SIDE_A)) {
                ferry_capacity += v->capacity;
                v->ferry_start = time(NULL);
                v->boarded++;
                boarded = 1;
                pthread_mutex_lock(&print_mutex);
                printf("✅ %s %d boarded ferry at Side-%c (Capacity: %d)\n",
                       get_type_name(v->type), v->id, v->port == SIDE_A ? 'A' : 'B', ferry_capacity);
                pthread_mutex_unlock(&print_mutex);
            }
            pthread_mutex_unlock(&boarding_mutex);
            if (!boarded) usleep(200000);
        }

        while (ferry_side == v->port)
            usleep(100000);

        v->ferry_end = time(NULL);
        v->port = 1 - v->port;

        if (v->boarded == 2) {
            pthread_mutex_lock(&return_mutex);
            v->returned = 1;
            total_returned++;
            pthread_mutex_unlock(&return_mutex);
        }

        sleep(2 + rand() % 3);
    }
    return NULL;
}

void* ferry_func(void* arg) {
    while (1) {
        int boarded_ids[20];
        int count = 0;
        time_t start_time = time(NULL);

        while (1) {
            sleep(1);
            pthread_mutex_lock(&boarding_mutex);
            if (ferry_capacity >= FERRY_CAPACITY || difftime(time(NULL), start_time) >= 5)
                break;
            pthread_mutex_unlock(&boarding_mutex);
        }

        pthread_mutex_lock(&boarding_mutex);
        // log boarded vehicles
        for (int i = 0; i < TOTAL_VEHICLES; i++) {
            if (vehicles[i].boarded > 0 &&
                ((ferry_side == SIDE_A && vehicles[i].a_trip_no == 0) ||
                 (ferry_side == SIDE_B && vehicles[i].b_trip_no == 0))) {
                boarded_ids[count++] = vehicles[i].id;
                if (ferry_side == SIDE_A)
                    vehicles[i].a_trip_no = current_trip_id;
                else
                    vehicles[i].b_trip_no = current_trip_id;
            }
        }

        printf("\n⛴️ Ferry departing from Side-%c to Side-%c with %d units\n",
               ferry_side == SIDE_A ? 'A' : 'B',
               ferry_side == SIDE_A ? 'B' : 'A',
               ferry_capacity);

        double duration = 2 + rand() % 4;
        pthread_mutex_unlock(&boarding_mutex);
        sleep(duration);

        pthread_mutex_lock(&boarding_mutex);
        ferry_side = 1 - ferry_side;
        ferry_capacity = 0;
        if (is_first_return && ferry_side == SIDE_A)
            is_first_return = 0;
        log_trip(1 - ferry_side, duration, boarded_ids, count);
        pthread_mutex_unlock(&boarding_mutex);

        pthread_mutex_lock(&return_mutex);
        if (total_returned >= TOTAL_VEHICLES && ferry_side == SIDE_A) {
            final_trip_done = 1;
            pthread_mutex_unlock(&return_mutex);
            break;
        }
        pthread_mutex_unlock(&return_mutex);
    }

    printf("\n📦 Trip Summary:\n");
    for (int i = 0; i < trip_count; i++) {
        Trip *t = &trip_log[i];
        printf("Trip %d [%s]: %.2f sec | Vehicles: ", t->trip_id,
               t->direction == 0 ? "A→B" : "B→A", t->duration);
        for (int j = 0; j < t->vehicle_count; j++)
            printf("%d ", t->vehicle_ids[j]);
        printf("\n");
    }
    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t vehicle_threads[TOTAL_VEHICLES];
    pthread_t ferry;

    // Init semaphores
    for (int i = 0; i < 4; i++)
        sem_init(&toll_sem[i], 0, 1);

    // Create vehicles
    int id = 1;
    for (int i = 0; i < CAR_COUNT; i++)
        vehicles[i] = (Vehicle){id++, 0, 1, SIDE_A, 0, -1, -1, 0, 0, 0, 0};
    for (int i = 0; i < MINIBUS_COUNT; i++)
        vehicles[CAR_COUNT + i] = (Vehicle){id++, 1, 2, SIDE_A, 0, -1, -1, 0, 0, 0, 0};
    for (int i = 0; i < TRUCK_COUNT; i++)
        vehicles[CAR_COUNT + MINIBUS_COUNT + i] = (Vehicle){id++, 2, 3, SIDE_A, 0, -1, -1, 0, 0, 0, 0};

    // Create threads
    pthread_create(&ferry, NULL, ferry_func, NULL);
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_create(&vehicle_threads[i], NULL, vehicle_func, &vehicles[i]);

    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_join(vehicle_threads[i], NULL);

    pthread_join(ferry, NULL);

    printf("\n✅ Simulation complete. All vehicles returned.\n");
    return 0;
}
