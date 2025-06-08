#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

// Constants
#define NUM_CARS 12
#define NUM_MINIBUSES 10
#define NUM_TRUCKS 8
#define FERRY_CAPACITY 20
#define NUM_TOLLS_PER_SIDE 2
#define SIDE_A 0
#define SIDE_B 1

// Vehicle structure
typedef struct {
    int id;           // Unique vehicle ID
    int type;         // 0: Car, 1: Minibus, 2: Truck
    int port;         // Current side (0: Side A, 1: Side B)
    int toll;         // Assigned toll (0 or 1 for Side A, 2 or 3 for Side B)
    int capacity;     // Capacity units (1 for car, 2 for minibus, 3 for truck)
    int original_port;// Original starting side for return check
} Vehicle;

// Global variables
Vehicle vehicles[NUM_CARS + NUM_MINIBUSES + NUM_TRUCKS];
sem_t toll_sem[4];          // Semaphores for 4 tolls
sem_t square_sem[2];        // Semaphores for squares on each side
sem_t ferry_load_sem;       // Semaphore for ferry loading
sem_t ferry_unload_sem;     // Semaphore for ferry unloading
pthread_mutex_t ferry_mutex;// Mutex for ferry state
int ferry_capacity_used = 0;// Current capacity used on ferry
int ferry_side = SIDE_A;    // Current side of ferry (0: Side A, 1: Side B)
int ferry_passengers = 0;   // Number of vehicles on ferry
int all_returned = 0;       // Flag to check if all vehicles returned
int event_counter = 0;      // Sequence number for events
double sim_time = 0.0;      // Simulated time in seconds

// Random number generators
int getrand(int max) {
    return rand() % max;
}

double getrand_wait(double min, double max) {
    return min + ((double)rand() / RAND_MAX) * (max - min);
}

// Get vehicle type as string
const char* get_vehicle_type(int type) {
    switch (type) {
        case 0: return "Car";
        case 1: return "Minibus";
        case 2: return "Truck";
        default: return "Unknown";
    }
}

// Get side as string
const char* get_side_name(int side) {
    return (side == SIDE_A) ? "Bozca Island" : "Istanbul Karakoy";
}

// Update simulated time
void update_sim_time(double seconds) {
    sim_time += seconds;
}

// Vehicle thread function
void* vehicle_thread(void* arg) {
    Vehicle* v = (Vehicle*)arg;
    char* v_type = (char*)get_vehicle_type(v->type);

    while (!all_returned) {
        // Pass through toll
        if (sem_wait(&toll_sem[v->toll]) != 0) {
            fprintf(stderr, "Error: sem_wait toll_sem failed\n");
            return NULL;
        }
        double toll_wait = getrand_wait(5.0, 10.0); // Increased for longer simulation
        printf("[Event %03d, Time %.2fs] %s %d passing toll %d on %s (wait: %.2fs)\n",
               ++event_counter, sim_time, v_type, v->id, v->toll, get_side_name(v->port), toll_wait);
        update_sim_time(toll_wait);
        usleep((useconds_t)(toll_wait * 1000000));
        if (sem_post(&toll_sem[v->toll]) != 0) {
            fprintf(stderr, "Error: sem_post toll_sem failed\n");
            return NULL;
        }

        // Enter square
        if (sem_wait(&square_sem[v->port]) != 0) {
            fprintf(stderr, "Error: sem_wait square_sem failed\n");
            return NULL;
        }
        double square_wait = getrand_wait(2.0, 5.0); // Increased
        printf("[Event %03d, Time %.2fs] %s %d entered waiting square on %s (wait: %.2fs)\n",
               ++event_counter, sim_time, v_type, v->id, get_side_name(v->port), square_wait);
        update_sim_time(square_wait);
        usleep((useconds_t)(square_wait * 1000000));
        if (sem_post(&square_sem[v->port]) != 0) {
            fprintf(stderr, "Error: sem_post square_sem failed\n");
            return NULL;
        }

        // Wait for ferry and load
        int loaded = 0;
        while (!loaded && !all_returned) {
            if (sem_wait(&ferry_load_sem) != 0) {
                fprintf(stderr, "Error: sem_wait ferry_load_sem failed\n");
                return NULL;
            }
            if (pthread_mutex_lock(&ferry_mutex) != 0) {
                fprintf(stderr, "Error: pthread_mutex_lock ferry_mutex failed\n");
                sem_post(&ferry_load_sem);
                return NULL;
            }

            // Check if ferry is on the correct side and has enough capacity
            if (ferry_side == v->port && ferry_capacity_used + v->capacity <= FERRY_CAPACITY) {
                double load_wait = getrand_wait(3.0, 6.0); // Increased
                ferry_capacity_used += v->capacity;
                ferry_passengers++;
                printf("[Event %03d, Time %.2fs] %s %d loaded onto ferry on %s (wait: %.2fs). "
                       "Ferry: %d/%d capacity used, %d vehicles onboard\n",
                       ++event_counter, sim_time, v_type, v->id, get_side_name(v->port), load_wait,
                       ferry_capacity_used, FERRY_CAPACITY, ferry_passengers);
                update_sim_time(load_wait);
                usleep((useconds_t)(load_wait * 1000000));
                loaded = 1;
                sem_post(&ferry_load_sem);
            } else {
                sem_post(&ferry_load_sem);
                pthread_mutex_unlock(&ferry_mutex);
                double retry_wait = getrand_wait(1.0, 3.0); // Increased
                update_sim_time(retry_wait);
                usleep((useconds_t)(retry_wait * 1000000));
                continue;
            }
            if (pthread_mutex_unlock(&ferry_mutex) != 0) {
                fprintf(stderr, "Error: pthread_mutex_unlock ferry_mutex failed\n");
                return NULL;
            }
        }

        // Wait for ferry to cross
        while (ferry_side == v->port && !all_returned) {
            usleep(100000); // Increased polling interval
        }

        // Unload from ferry
        if (sem_wait(&ferry_unload_sem) != 0) {
            fprintf(stderr, "Error: sem_wait ferry_unload_sem failed\n");
            return NULL;
        }
        if (pthread_mutex_lock(&ferry_mutex) != 0) {
            fprintf(stderr, "Error: pthread_mutex_lock ferry_mutex failed\n");
            sem_post(&ferry_unload_sem);
            return NULL;
        }
        double unload_wait = getrand_wait(3.0, 6.0); // Increased
        ferry_capacity_used -= v->capacity;
        ferry_passengers--;
        v->port = ferry_side;
        printf("[Event %03d, Time %.2fs] %s %d unloaded from ferry on %s (wait: %.2fs). "
               "Ferry: %d/%d capacity used, %d vehicles onboard\n",
               ++event_counter, sim_time, v_type, v->id, get_side_name(v->port), unload_wait,
               ferry_capacity_used, FERRY_CAPACITY, ferry_passengers);
        update_sim_time(unload_wait);
        usleep((useconds_t)(unload_wait * 1000000));
        if (pthread_mutex_unlock(&ferry_mutex) != 0) {
            fprintf(stderr, "Error: pthread_mutex_unlock ferry_mutex failed\n");
            return NULL;
        }
        if (sem_post(&ferry_unload_sem) != 0) {
            fprintf(stderr, "Error: sem_post ferry_unload_sem failed\n");
            return NULL;
        }

        // Check if vehicle returned to original side
        if (v->port == v->original_port) {
            printf("[Event %03d, Time %.2fs] %s %d returned to original side (%s)\n",
                   ++event_counter, sim_time, v_type, v->id, get_side_name(v->port));
            break;
        }

        // Wait before returning
        double return_wait = getrand_wait(20.0, 30.0); // Significantly increased
        printf("[Event %03d, Time %.2fs] %s %d waiting on %s before return (wait: %.2fs)\n",
               ++event_counter, sim_time, v_type, v->id, get_side_name(v->port), return_wait);
        update_sim_time(return_wait);
        usleep((useconds_t)(return_wait * 1000000));
    }

    return NULL;
}

// Ferry thread function
void* ferry_thread(void* arg) {
    while (!all_returned) {
        if (pthread_mutex_lock(&ferry_mutex) != 0) {
            fprintf(stderr, "Error: pthread_mutex_lock ferry_mutex failed\n");
            return NULL;
        }
        // Check if ferry has enough passengers or is full
        if (ferry_passengers > 0 && (ferry_capacity_used >= FERRY_CAPACITY || ferry_passengers >= 10)) {
            double cross_wait = getrand_wait(30.0, 40.0); // Increased for longer simulation
            printf("[Event %03d, Time %.2fs] Ferry departing from %s with %d vehicles, "
                   "%d/%d capacity used (Remaining: %d, Crossing time: %.2fs)\n",
                   ++event_counter, sim_time, get_side_name(ferry_side), ferry_passengers,
                   ferry_capacity_used, FERRY_CAPACITY, FERRY_CAPACITY - ferry_capacity_used, cross_wait);
            update_sim_time(cross_wait);
            usleep((useconds_t)(cross_wait * 1000000));
            ferry_side = (ferry_side == SIDE_A) ? SIDE_B : SIDE_A;
            printf("[Event %03d, Time %.2fs] Ferry arrived at %s with %d vehicles, "
                   "%d/%d capacity used (Remaining: %d)\n",
                   ++event_counter, sim_time, get_side_name(ferry_side), ferry_passengers,
                   ferry_capacity_used, FERRY_CAPACITY, FERRY_CAPACITY - ferry_capacity_used);
            ferry_passengers = 0; // Reset for loading on new side
        }
        if (pthread_mutex_unlock(&ferry_mutex) != 0) {
            fprintf(stderr, "Error: pthread_mutex_unlock ferry_mutex failed\n");
            return NULL;
        }
        usleep(500000); // Increased to avoid busy waiting
    }
    return NULL;
}

// Check if all vehicles returned to original side
void* check_completion(void* arg) {
    while (1) {
        int returned = 1;
        for (int i = 0; i < NUM_CARS + NUM_MINIBUSES + NUM_TRUCKS; i++) {
            if (vehicles[i].port != vehicles[i].original_port) {
                returned = 0;
                break;
            }
        }
        if (returned) {
            all_returned = 1;
            printf("[Event %03d, Time %.2fs] All vehicles returned to their original sides. Simulation complete.\n",
                   ++event_counter, sim_time);
            break;
        }
        usleep(5000000); // Check every 5 seconds
    }
    return NULL;
}

int main() {
    srand(time(NULL));

    // Initialize semaphores and mutex
    for (int i = 0; i < 4; i++) {
        if (sem_init(&toll_sem[i], 0, 1) != 0) {
            fprintf(stderr, "Error: sem_init toll_sem[%d] failed\n", i);
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (sem_init(&square_sem[i], 0, 100) != 0) {
            fprintf(stderr, "Error: sem_init square_sem[%d] failed\n", i);
            exit(EXIT_FAILURE);
        }
    }
    if (sem_init(&ferry_load_sem, 0, 1) != 0) {
        fprintf(stderr, "Error: sem_init ferry_load_sem failed\n");
        exit(EXIT_FAILURE);
    }
    if (sem_init(&ferry_unload_sem, 0, 1) != 0) {
        fprintf(stderr, "Error: sem_init ferry_unload_sem failed\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&ferry_mutex, NULL) != 0) {
        fprintf(stderr, "Error: pthread_mutex_init ferry_mutex failed\n");
        exit(EXIT_FAILURE);
    }

    // Initialize vehicles
    int vehicle_count = 0;
    // Cars
    for (int i = 0; i < NUM_CARS; i++) {
        vehicles[vehicle_count].id = vehicle_count;
        vehicles[vehicle_count].type = 0;
        vehicles[vehicle_count].capacity = 1;
        vehicles[vehicle_count].port = getrand(2);
        vehicles[vehicle_count].original_port = vehicles[vehicle_count].port;
        vehicles[vehicle_count].toll = (vehicles[vehicle_count].port == SIDE_A) ? getrand(2) : 2 + getrand(2);
        vehicle_count++;
    }
    // Minibuses
    for (int i = 0; i < NUM_MINIBUSES; i++) {
        vehicles[vehicle_count].id = vehicle_count;
        vehicles[vehicle_count].type = 1;
        vehicles[vehicle_count].capacity = 2;
        vehicles[vehicle_count].port = getrand(2);
        vehicles[vehicle_count].original_port = vehicles[vehicle_count].port;
        vehicles[vehicle_count].toll = (vehicles[vehicle_count].port == SIDE_A) ? getrand(2) : 2 + getrand(2);
        vehicle_count++;
    }
    // Trucks
    for (int i = 0; i < NUM_TRUCKS; i++) {
        vehicles[vehicle_count].id = vehicle_count;
        vehicles[vehicle_count].type = 2;
        vehicles[vehicle_count].capacity = 3;
        vehicles[vehicle_count].port = getrand(2);
        vehicles[vehicle_count].original_port = vehicles[vehicle_count].port;
        vehicles[vehicle_count].toll = (vehicles[vehicle_count].port == SIDE_A) ? getrand(2) : 2 + getrand(2);
        vehicle_count++;
    }

    // Create vehicle threads
    pthread_t vehicle_threads[NUM_CARS + NUM_MINIBUSES + NUM_TRUCKS];
    for (int i = 0; i < NUM_CARS + NUM_MINIBUSES + NUM_TRUCKS; i++) {
        if (pthread_create(&vehicle_threads[i], NULL, vehicle_thread, &vehicles[i]) != 0) {
            fprintf(stderr, "Error: Could not create thread for vehicle %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    // Create ferry thread
    pthread_t ferry;
    if (pthread_create(&ferry, NULL, ferry_thread, NULL) != 0) {
        fprintf(stderr, "Error: Could not create ferry thread\n");
        exit(EXIT_FAILURE);
    }

    // Create completion check thread
    pthread_t completion;
    if (pthread_create(&completion, NULL, check_completion, NULL) != 0) {
        fprintf(stderr, "Error: Could not create completion thread\n");
        exit(EXIT_FAILURE);
    }

    // Join threads
    for (int i = 0; i < NUM_CARS + NUM_MINIBUSES + NUM_TRUCKS; i++) {
        if (pthread_join(vehicle_threads[i], NULL) != 0) {
            fprintf(stderr, "Error: pthread_join vehicle_thread %d failed\n", i);
        }
    }
    if (pthread_join(ferry, NULL) != 0) {
        fprintf(stderr, "Error: pthread_join ferry_thread failed\n");
    }
    if (pthread_join(completion, NULL) != 0) {
        fprintf(stderr, "Error: pthread_join completion_thread failed\n");
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        sem_destroy(&toll_sem[i]);
    }
    for (int i = 0; i < 2; i++) {
        sem_destroy(&square_sem[i]);
    }
    sem_destroy(&ferry_load_sem);
    sem_destroy(&ferry_unload_sem);
    pthread_mutex_destroy(&ferry_mutex);

    printf("Simulation ended successfully.\n");
    return 0;
}