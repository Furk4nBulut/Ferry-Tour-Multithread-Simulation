#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <ncurses.h>
#include <math.h>

// cJSON kütüphanesi (gömülü)
typedef struct cJSON {
    struct cJSON *next, *prev, *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

#define cJSON_Object 4
#define cJSON_Array 5
#define cJSON_String 6
#define cJSON_Number 7

// cJSON fonksiyon prototipleri
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateNumber(double num);
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
void cJSON_AddItemToArray(cJSON *array, cJSON *item);
void cJSON_AddNumberToObject(cJSON *object, const char *string, double num);
void cJSON_AddStringToObject(cJSON *object, const char *string, const char *value);
void cJSON_Delete(cJSON *c);
char *cJSON_Print(cJSON *item);

// cJSON fonksiyon tanımları
cJSON *cJSON_CreateObject(void) { cJSON *item = (cJSON*)calloc(1, sizeof(cJSON)); item->type = cJSON_Object; return item; }
cJSON *cJSON_CreateArray(void) { cJSON *item = (cJSON*)calloc(1, sizeof(cJSON)); item->type = cJSON_Array; return item; }
cJSON *cJSON_CreateString(const char *string) { cJSON *item = (cJSON*)calloc(1, sizeof(cJSON)); item->type = cJSON_String; item->valuestring = strdup(string); return item; }
cJSON *cJSON_CreateNumber(double num) { cJSON *item = (cJSON*)calloc(1, sizeof(cJSON)); item->type = cJSON_Number; item->valuedouble = num; item->valueint = (int)num; return item; }
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) { item->string = strdup(string); item->next = object->child; if (object->child) object->child->prev = item; object->child = item; }
void cJSON_AddItemToArray(cJSON *array, cJSON *item) { cJSON *c = array->child; if (!c) { array->child = item; } else { while (c->next) c = c->next; c->next = item; item->prev = c; } }
void cJSON_AddNumberToObject(cJSON *object, const char *string, double num) { cJSON *item = cJSON_CreateNumber(num); cJSON_AddItemToObject(object, string, item); }
void cJSON_AddStringToObject(cJSON *object, const char *string, const char *value) { cJSON *item = cJSON_CreateString(value); cJSON_AddItemToObject(object, string, item); }
void cJSON_Delete(cJSON *c) { cJSON *next; while (c) { next = c->next; if (c->child) cJSON_Delete(c->child); if (c->valuestring) free(c->valuestring); if (c->string) free(c->string); free(c); c = next; } }
char *cJSON_Print(cJSON *item) {
    char *buf = (char*)malloc(1024); buf[0] = '\0'; int len = 1024;
    if (item->type == cJSON_Object) {
        sprintf(buf, "{"); cJSON *c = item->child;
        while (c) {
            char *child = cJSON_Print(c); char *key = c->string;
            len += strlen(key) + strlen(child) + 5;
            buf = (char*)realloc(buf, len);
            strcat(buf, "\""); strcat(buf, key); strcat(buf, "\":"); strcat(buf, child);
            if (c->next) strcat(buf, ",");
            free(child); c = c->next;
        }
        strcat(buf, "}");
    } else if (item->type == cJSON_Array) {
        sprintf(buf, "["); cJSON *c = item->child;
        while (c) {
            char *child = cJSON_Print(c);
            len += strlen(child) + 2;
            buf = (char*)realloc(buf, len);
            strcat(buf, child);
            if (c->next) strcat(buf, ",");
            free(child); c = c->next;
        }
        strcat(buf, "]");
    } else if (item->type == cJSON_String) {
        len += strlen(item->valuestring) + 3;
        buf = (char*)realloc(buf, len);
        sprintf(buf, "\"%s\"", item->valuestring);
    } else if (item->type == cJSON_Number) {
        char tmp[32]; sprintf(tmp, "%.2f", item->valuedouble);
        len += strlen(tmp) + 1;
        buf = (char*)realloc(buf, len);
        strcpy(buf, tmp);
    }
    return buf;
}

#define CAR_COUNT 25
#define MINIBUS_COUNT 15
#define TRUCK_COUNT 10
#define TOTAL_VEHICLES 50
#define FERRY_CAPACITY 50
#define TOLL_PER_SIDE 2
#define MAX_TRIPS 100
#define SIDE_X 0
#define SIDE_Y 1

typedef struct {
    int id;
    int type; // 0: Car, 1: Minibus, 2: Truck
    int capacity; // 1, 2, 4
    int port; // SIDE_X or SIDE_Y
    int start_port; // Initial starting point (X or Y)
    int boarded; // 0, 1, 2
    int x_trip_no, y_trip_no; // Trip numbers for X->Y and Y->X
    int returned; // 1 if vehicle completed round-trip
    time_t wait_start_x, wait_end_x; // Side-X wait times
    time_t wait_start_y, wait_end_y; // Side-Y wait times
    time_t ferry_start_x, ferry_end_x; // X->Y ferry times
    time_t ferry_start_y, ferry_end_y; // Y->X ferry times
    time_t trip_start, trip_end; // Full round-trip times
} Vehicle;

typedef struct {
    int trip_id;
    int direction; // 0: X->Y, 1: Y->X
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
int ferry_side = SIDE_X;
int total_returned = 0;
int current_trip_id = 0;
int is_first_return = 1;
int final_trip_done = 0;
int boarded_ids[20];
int boarded_count = 0;
double wait_time_x[TOTAL_VEHICLES], wait_time_y[TOTAL_VEHICLES];
double ferry_time_x[TOTAL_VEHICLES], ferry_time_y[TOTAL_VEHICLES];
double round_trip_time[TOTAL_VEHICLES];
time_t sim_start_time;
WINDOW *main_win, *ferry_win, *stats_win, *log_win, *status_win;
int paused = 0; // Simülasyon duraklatma durumu
float sim_speed = 1.0; // Simülasyon hız katsayısı (1.0 = normal)

// Synchronization primitives
pthread_mutex_t boarding_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t return_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t toll_sem[4]; // 0-1: Side-X tolls, 2-3: Side-Y tolls
sem_t pause_sem; // Duraklatma için semafor

// Helper functions
const char* get_type_name(int type) {
    static char buf[32];
    switch (type) {
        case 0: strcpy(buf, "Car"); break;
        case 1: strcpy(buf, "Minibus"); break;
        case 2: strcpy(buf, "Truck"); break;
        default: strcpy(buf, "Unknown");
    }
    return buf;
}

const char* get_type_icon(int type) {
    switch (type) {
        case 0: return "🚗"; // Car
        case 1: return "🚐"; // Minibus
        case 2: return "🚚"; // Truck
        default: return "???";
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

void draw_ferry_animation(int position) {
    pthread_mutex_lock(&print_mutex);
    wclear(ferry_win);
    box(ferry_win, 0, 0);
    mvwprintw(ferry_win, 1, 2, "Ferry Status: %s", ferry_side == SIDE_X ? "At X" : "At Y");
    mvwprintw(ferry_win, 2, 2, "Direction: %s", ferry_side == SIDE_X ? "X -> Y" : "Y -> X");

    // Deniz (su) çizimi
    for (int i = 1; i < 7; i++) {
        mvwprintw(ferry_win, i, 10, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    }

    // Feribot çizimi
    wattron(ferry_win, COLOR_PAIR(4));
    mvwprintw(ferry_win, 3, 10 + position, "🚢");
    mvwprintw(ferry_win, 4, 10 + position, "=====");
    wattroff(ferry_win, COLOR_PAIR(4));

    // Kıyılar
    mvwprintw(ferry_win, 2, 8, "X");
    mvwprintw(ferry_win, 2, 68, "Y");

    wrefresh(ferry_win);
    pthread_mutex_unlock(&print_mutex);
}

void print_state(int wait_counter) {
    pthread_mutex_lock(&print_mutex);
    wclear(main_win);
    box(main_win, 0, 0);
    wattron(main_win, A_BOLD);
    mvwprintw(main_win, 1, 28, "🚢 Ferry Simulation 🚢");
    mvwprintw(main_win, 2, 30, "[Trip: %d/%d]", current_trip_id, MAX_TRIPS);
    wattroff(main_win, A_BOLD);

    // Side-X
    mvwprintw(main_win, 4, 2, "📍 Side-X:");
    int y = 5, count_x = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == SIDE_X && !vehicles[i].returned && vehicles[i].boarded < 2) {
            wattron(main_win, COLOR_PAIR(vehicles[i].type + 1) | A_BOLD);
            mvwprintw(main_win, y++, 4, "%s %s%d [Wait: %.1fs]",
                      get_type_icon(vehicles[i].type), get_type_name(vehicles[i].type),
                      vehicles[i].id, vehicles[i].wait_start_x ? difftime(time(NULL), vehicles[i].wait_start_x) : 0);
            wattroff(main_win, COLOR_PAIR(vehicles[i].type + 1) | A_BOLD);
            count_x++;
            if (y > 12) break;
        }
    }
    mvwprintw(main_win, 4, 15, "(%d vehicles)", count_x);

    // Side-Y
    mvwprintw(main_win, 4, 50, "📍 Side-Y:");
    y = 5; int count_y = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (vehicles[i].port == SIDE_Y && !vehicles[i].returned && vehicles[i].boarded < 2) {
            wattron(main_win, COLOR_PAIR(vehicles[i].type + 1) | A_BOLD);
            mvwprintw(main_win, y++, 52, "%s %s%d [Wait: %.1fs]",
                      get_type_icon(vehicles[i].type), get_type_name(vehicles[i].type),
                      vehicles[i].id, vehicles[i].wait_start_y ? difftime(time(NULL), vehicles[i].wait_start_y) : 0);
            wattroff(main_win, COLOR_PAIR(vehicles[i].type + 1) | A_BOLD);
            count_y++;
            if (y > 12) break;
        }
    }
    mvwprintw(main_win, 4, 63, "(%d vehicles)", count_y);

    // Start
    mvwprintw(main_win, 14, 2, "🏁 Start Points:");
    y = 15; int count_s = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        if (!vehicles[i].returned && vehicles[i].boarded < 2) {
            mvwprintw(main_win, y++, 4, "%c%-3d", vehicles[i].start_port == SIDE_X ? 'X' : 'Y', vehicles[i].id);
            count_s++;
            if (y > 20) break;
        }
    }

    // Stats window
    wclear(stats_win);
    box(stats_win, 0, 0);
    wattron(stats_win, A_BOLD);
    mvwprintw(stats_win, 1, 35, "📊 Statistics");
    wattroff(stats_win, A_BOLD);
    mvwprintw(stats_win, 2, 2, "Progress: %.1f%%", (total_returned / (float)TOTAL_VEHICLES) * 100);
    mvwprintw(stats_win, 3, 2, "Elapsed: %ld s", time(NULL) - sim_start_time);
    mvwprintw(stats_win, 4, 2, "Returned: %d/%d", total_returned, TOTAL_VEHICLES);
    mvwprintw(stats_win, 5, 2, "Load: %d/%d (%.1f%%)", ferry_capacity, FERRY_CAPACITY, (ferry_capacity / (float)FERRY_CAPACITY) * 100);
    mvwprintw(stats_win, 6, 2, "Wait: %.1fs", wait_counter * 0.25);
    mvwprintw(stats_win, 7, 2, "Speed: %.1fx", sim_speed);

    // Status window
    wclear(status_win);
    box(status_win, 0, 0);
    wattron(status_win, paused ? COLOR_PAIR(5) : COLOR_PAIR(6));
    mvwprintw(status_win, 1, 2, "Status: %s", paused ? "Paused" : "Running");
    wattroff(status_win, paused ? COLOR_PAIR(5) : COLOR_PAIR(6));
    mvwprintw(status_win, 2, 2, "Controls: [P] Pause/Resume | [+/-] Speed | [Q] Quit");

    wrefresh(main_win);
    wrefresh(stats_win);
    wrefresh(status_win);
    pthread_mutex_unlock(&print_mutex);
}

void write_log_file() {
    cJSON *root = cJSON_CreateObject();
    cJSON *trips = cJSON_CreateArray();
    cJSON *vehicles_array = cJSON_CreateArray();

    cJSON_AddNumberToObject(root, "total_vehicles", TOTAL_VEHICLES);
    cJSON_AddNumberToObject(root, "ferry_capacity", FERRY_CAPACITY);
    cJSON_AddNumberToObject(root, "total_trips", trip_count);
    cJSON_AddNumberToObject(root, "duration", (double)(time(NULL) - sim_start_time));

    for (int i = 0; i < trip_count; i++) {
        cJSON *trip = cJSON_CreateObject();
        Trip *t = &trip_log[i];
        cJSON_AddNumberToObject(trip, "id", t->trip_id);
        cJSON_AddStringToObject(trip, "direction", t->direction == SIDE_X ? "X->Y" : "Y->X");
        cJSON_AddNumberToObject(trip, "duration", t->duration);
        cJSON_AddNumberToObject(trip, "capacity_used", t->capacity_used);
        cJSON_AddNumberToObject(trip, "capacity_percent", (t->capacity_used / (float)FERRY_CAPACITY) * 100);
        cJSON *vehicles_json = cJSON_CreateArray();
        for (int j = 0; j < t->vehicle_count; j++) {
            for (int k = 0; k < TOTAL_VEHICLES; k++) {
                if (vehicles[k].id == t->vehicle_ids[j]) {
                    char buf[32];
                    sprintf(buf, "%s%d", get_type_name(vehicles[k].type), t->vehicle_ids[j]);
                    cJSON_AddItemToArray(vehicles_json, cJSON_CreateString(buf));
                    break;
                }
            }
        }
        cJSON_AddItemToObject(trip, "vehicles", vehicles_json);
        cJSON_AddItemToArray(trips, trip);
    }
    cJSON_AddItemToObject(root, "trips", trips);

    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        cJSON *veh = cJSON_CreateObject();
        cJSON_AddNumberToObject(veh, "id", vehicles[i].id);
        cJSON_AddStringToObject(veh, "type", get_type_name(vehicles[i].type));
        cJSON_AddStringToObject(veh, "start", vehicles[i].start_port == SIDE_X ? "X" : "Y");
        cJSON_AddNumberToObject(veh, "wait_x", wait_time_x[i]);
        cJSON_AddNumberToObject(veh, "wait_y", wait_time_y[i]);
        cJSON_AddNumberToObject(veh, "ferry_x", ferry_time_x[i]);
        cJSON_AddNumberToObject(veh, "ferry_y", ferry_time_y[i]);
        cJSON_AddNumberToObject(veh, "round_trip", round_trip_time[i]);
        cJSON_AddItemToArray(vehicles_array, veh);
    }
    cJSON_AddItemToObject(root, "vehicles", vehicles_array);

    char *json_str = cJSON_Print(root);
    FILE *fp = fopen("ferry_log.json", "w");
    if (fp) {
        fprintf(fp, "%s\n", json_str);
        fclose(fp);
    }
    free(json_str);
    cJSON_Delete(root);
}

void show_final_statistics() {
    endwin(); // Ncurses'ü kapat
    double total_wait_x = 0, total_wait_y = 0, total_ferry_x = 0, total_ferry_y = 0;
    double car_wait = 0, minibus_wait = 0, truck_wait = 0;
    double start_x_wait = 0, start_y_wait = 0;
    double total_round_trip = 0, max_round_trip = 0, min_round_trip = 1e9;
    double round_trip_variance = 0;
    int car_count = 0, minibus_count = 0, truck_count = 0;
    int start_x_count = 0, start_y_count = 0;
    double total_duration = 0, total_capacity_used = 0;
    double max_wait = 0, min_wait = 1e9;
    int empty_trips = 0;

    printf("\n┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ Final Statistics ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳\n");
    printf("┃ Simulation Complete! Total Duration: %ld s | Total Trips: %d                  ┃\n",
           time(NULL) - sim_start_time, trip_count);
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ ID | Type      | Start | Wait X | Wait Y | Ferry X | Ferry Y | Round Trip   ┃\n");
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");

    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        double total_wait = wait_time_x[i] + wait_time_y[i];
        printf("┃ %2d | %-9s | %-5c | %6.1fs | %6.1fs | %7.1fs | %7.1fs | %8.1fs ┃\n",
               vehicles[i].id, get_type_name(vehicles[i].type),
               vehicles[i].start_port == SIDE_X ? 'X' : 'Y',
               wait_time_x[i], wait_time_y[i], ferry_time_x[i], ferry_time_y[i], round_trip_time[i]);
        total_wait_x += wait_time_x[i];
        total_wait_y += wait_time_y[i];
        total_ferry_x += ferry_time_x[i];
        total_ferry_y += ferry_time_y[i];
        total_round_trip += round_trip_time[i];
        round_trip_variance += pow(round_trip_time[i] - (total_round_trip / TOTAL_VEHICLES), 2);
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
        if (vehicles[i].start_port == SIDE_X) {
            start_x_wait += total_wait;
            start_x_count++;
        } else {
            start_y_wait += total_wait;
            start_y_count++;
        }
        if (total_wait > max_wait) max_wait = total_wait;
        if (total_wait < min_wait && total_wait > 0) min_wait = total_wait;
    }

    for (int i = 0; i < trip_count; i++) {
        total_duration += trip_log[i].duration;
        total_capacity_used += trip_log[i].capacity_used;
        if (trip_log[i].vehicle_count == 0) empty_trips++;
    }

    round_trip_variance /= TOTAL_VEHICLES;

    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ Avg Wait X: %.2fs | Avg Wait Y: %.2fs | Avg Ferry X: %.2fs | Avg Ferry Y: %.2fs ┃\n",
           total_wait_x / TOTAL_VEHICLES, total_wait_y / TOTAL_VEHICLES,
           total_ferry_x / TOTAL_VEHICLES, total_ferry_y / TOTAL_VEHICLES);
    printf("┃ Avg Wait by Type: Car: %.2fs | Minibus: %.2fs | Truck: %.2fs                  ┃\n",
           car_count ? car_wait / car_count : 0,
           minibus_count ? minibus_wait / minibus_count : 0,
           truck_count ? truck_wait / truck_count : 0);
    printf("┃ Avg Wait by Start: X: %.2fs | Y: %.2fs                                      ┃\n",
           start_x_count ? start_x_wait / start_x_count : 0,
           start_y_count ? start_y_wait / start_y_count : 0);
    printf("┃ Avg Round Trip: %.2fs | Max: %.2fs | Min: %.2fs | Std Dev: %.2fs            ┃\n",
           total_round_trip / TOTAL_VEHICLES, max_round_trip, min_round_trip, sqrt(round_trip_variance));
    printf("┃ Ferry Utilization: %.2f%% | Empty Trips: %d (%.2f%%) | X->Y vs Y->X: %.2f%% ┃\n",
           (total_capacity_used / (trip_count * FERRY_CAPACITY)) * 100,
           empty_trips, (empty_trips / (float)trip_count) * 100,
           (total_ferry_x / (total_ferry_y ? total_ferry_y : 1)) * 100);
    printf("┃ Starvation Risk: %s (Max Wait: %.2fs, Min: %.2fs, Ratio: %.2f)         ┃\n",
           max_wait / (min_wait ? min_wait : 1) > 3 ? "High" : "Low",
           max_wait, min_wait, max_wait / (min_wait ? min_wait : 1));
    printf("┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┻\n");

    write_log_file();
}

void* vehicle_func(void* arg) {
    Vehicle *v = (Vehicle*)arg;
    v->trip_start = time(NULL);

    while (v->boarded < 2) {
        sem_wait(&pause_sem); // Duraklatma kontrolü
        sem_post(&pause_sem);

        // Toll booth access
        int toll_index = (v->port == SIDE_X) ? rand() % TOLL_PER_SIDE : TOLL_PER_SIDE + (rand() % TOLL_PER_SIDE);
        sem_wait(&toll_sem[toll_index]);

        if (v->port == SIDE_X) v->wait_start_x = time(NULL);
        else v->wait_start_y = time(NULL);

        pthread_mutex_lock(&print_mutex);
        wattron(log_win, COLOR_PAIR(v->type + 1));
        wprintw(log_win, "[>] %s%d passed toll at Side-%c\n", get_type_name(v->type), v->id,
                v->port == SIDE_X ? 'X' : 'Y');
        wattroff(log_win, COLOR_PAIR(v->type + 1));
        wrefresh(log_win);
        pthread_mutex_unlock(&print_mutex);
        usleep(100000 / sim_speed); // Simulate toll payment
        sem_post(&toll_sem[toll_index]);

        // Wait for boarding
        int boarded = 0;
        while (!boarded && !final_trip_done) {
            sem_wait(&pause_sem); // Duraklatma kontrolü
            sem_post(&pause_sem);

            pthread_mutex_lock(&boarding_mutex);
            if (ferry_side == v->port && ferry_capacity + v->capacity <= FERRY_CAPACITY &&
                (!is_first_return || v->port == SIDE_X) &&
                (v->port == SIDE_Y ? current_trip_id >= v->y_trip_no + 2 : 1)) {
                if (v->port == SIDE_X) {
                    v->wait_end_x = time(NULL);
                    wait_time_x[v->id - 1] = difftime(v->wait_end_x, v->wait_start_x);
                    v->ferry_start_x = time(NULL);
                } else {
                    v->wait_end_y = time(NULL);
                    wait_time_y[v->id - 1] = difftime(v->wait_end_y, v->wait_start_y);
                    v->ferry_start_y = time(NULL);
                }
                ferry_capacity += v->capacity;
                v->boarded++;
                boarded_ids[boarded_count++] = v->id;
                if (v->port == SIDE_X) v->x_trip_no = current_trip_id;
                else v->y_trip_no = current_trip_id;
                boarded = 1;
                pthread_mutex_lock(&print_mutex);
                wattron(log_win, COLOR_PAIR(v->type + 1));
                wprintw(log_win, "[F] %s%d boarded ferry at Side-%c (Load: %d)\n",
                        get_type_name(v->type), v->id, v->port == SIDE_X ? 'X' : 'Y', ferry_capacity);
                wattroff(log_win, COLOR_PAIR(v->type + 1));
                wrefresh(log_win);
                pthread_mutex_unlock(&print_mutex);
            }
            pthread_mutex_unlock(&boarding_mutex);
            if (!boarded) usleep(200000 / sim_speed);
        }

        if (final_trip_done) break;

        // Wait for ferry to arrive
        while (ferry_side == v->port && !final_trip_done) {
            sem_wait(&pause_sem); // Duraklatma kontrolü
            sem_post(&pause_sem);
            usleep(100000 / sim_speed);
        }

        if (v->port == SIDE_X) {
            v->ferry_end_y = time(NULL);
            ferry_time_y[v->id - 1] = difftime(v->ferry_end_y, v->ferry_start_y);
        } else {
            v->ferry_end_x = time(NULL);
            ferry_time_x[v->id - 1] = difftime(v->ferry_end_x, v->ferry_start_x);
        }
        v->port = 1 - v->port;

        // Random wait time at destination (1-5 seconds)
        if (v->boarded == 1) usleep((1 + rand() % 5) * 1000000 / sim_speed);

        if (v->boarded == 2) {
            pthread_mutex_lock(&return_mutex);
            v->returned = 1;
            v->trip_end = time(NULL);
            round_trip_time[v->id - 1] = difftime(v->trip_end, v->trip_start);
            total_returned++;
            pthread_mutex_lock(&print_mutex);
            wattron(log_win, COLOR_PAIR(v->type + 1));
            wprintw(log_win, "[R] %s%d completed round-trip in %.1fs\n",
                    get_type_name(v->type), v->id, round_trip_time[v->id - 1]);
            wattroff(log_win, COLOR_PAIR(v->type + 1));
            wrefresh(log_win);
            pthread_mutex_unlock(&print_mutex);
            pthread_mutex_unlock(&return_mutex);
        }
    }
    return NULL;
}

void* ferry_func(void* arg) {
    int wait_counter = 0;
    while (!final_trip_done) {
        sem_wait(&pause_sem); // Duraklatma kontrolü
        sem_post(&pause_sem);

        time_t start_time = time(NULL);
        int should_depart = 0;

        while (!should_depart) {
            sem_wait(&pause_sem); // Duraklatma kontrolü
            sem_post(&pause_sem);

            pthread_mutex_lock(&boarding_mutex);
            int vehicles_waiting = 0;
            for (int i = 0; i < TOTAL_VEHICLES; i++) {
                if (vehicles[i].port == ferry_side && !vehicles[i].returned && vehicles[i].boarded < 2 &&
                    (ferry_side != SIDE_Y || current_trip_id >= vehicles[i].y_trip_no + 2)) {
                    vehicles_waiting = 1;
                    break;
                }
            }
            if (ferry_capacity >= FERRY_CAPACITY ||
                !vehicles_waiting ||
                wait_counter >= 10 ||
                (is_first_return && ferry_side == SIDE_Y)) {
                should_depart = 1;
                if (!vehicles_waiting && ferry_capacity == 0 && !(is_first_return && ferry_side == SIDE_Y)) {
                    pthread_mutex_lock(&print_mutex);
                    wprintw(log_win, "[!] No vehicles waiting at Side-%c\n", ferry_side == SIDE_X ? 'X' : 'Y');
                    wrefresh(log_win);
                    pthread_mutex_unlock(&print_mutex);
                }
            }
            wait_counter++;
            pthread_mutex_unlock(&boarding_mutex);
            if (!should_depart) usleep(250000 / sim_speed);
        }

        pthread_mutex_lock(&boarding_mutex);
        if (ferry_capacity > 0 || (is_first_return && ferry_side == SIDE_Y)) {
            pthread_mutex_lock(&print_mutex);
            wattron(log_win, COLOR_PAIR(4));
            wprintw(log_win, "[F] Ferry departing from Side-%c to Side-%c with %d units\n",
                    ferry_side == SIDE_X ? 'X' : 'Y', ferry_side == SIDE_X ? 'Y' : 'X', ferry_capacity);
            wattroff(log_win, COLOR_PAIR(4));
            wrefresh(log_win);
            pthread_mutex_unlock(&print_mutex);
        }

        double duration = (2 + (rand() % 8)) / sim_speed;
        log_trip(ferry_side, duration, boarded_ids, boarded_count, ferry_capacity);
        current_trip_id++;

        // Feribot animasyonu
        int steps = 50;
        for (int i = 0; i <= steps; i++) {
            sem_wait(&pause_sem); // Duraklatma kontrolü
            sem_post(&pause_sem);
            draw_ferry_animation(i * 50 / steps);
            usleep((duration * 1000000 / steps) / sim_speed);
        }

        ferry_side = 1 - ferry_side;
        ferry_capacity = 0;
        wait_counter = 0;
        boarded_count = 0;
        memset(boarded_ids, 0, sizeof(boarded_ids));

        if (is_first_return && ferry_side == SIDE_X) is_first_return = 0;

        pthread_mutex_lock(&return_mutex);
        if (total_returned >= TOTAL_VEHICLES && ferry_side == SIDE_X) {
            final_trip_done = 1;
        }
        pthread_mutex_unlock(&return_mutex);
        pthread_mutex_unlock(&boarding_mutex);
    }

    // Print trip summary
    pthread_mutex_lock(&print_mutex);
    wprintw(log_win, "\n=== Trip Summary ===\n");
    for (int i = 0; i < trip_count; i++) {
        Trip *t = &trip_log[i];
        wprintw(log_win, "Trip %d [%s]: %.2fs | Capacity: %d/%d (%.1f%%) | Vehicles: ",
                t->trip_id, t->direction == SIDE_X ? "X->Y" : "Y->X", t->duration,
                t->capacity_used, FERRY_CAPACITY, (t->capacity_used / (float)FERRY_CAPACITY) * 100);
        for (int j = 0; j < t->vehicle_count; j++) {
            for (int k = 0; k < TOTAL_VEHICLES; k++) {
                if (vehicles[k].id == t->vehicle_ids[j]) {
                    wprintw(log_win, "%s%d ", get_type_name(vehicles[k].type), t->vehicle_ids[j]);
                    break;
                }
            }
        }
        wprintw(log_win, "\n");
    }
    wrefresh(log_win);
    pthread_mutex_unlock(&print_mutex);

    return NULL;
}

void* print_state_thread(void* arg) {
    int wait_counter = 0;
    while (!final_trip_done) {
        sem_wait(&pause_sem); // Duraklatma kontrolü
        sem_post(&pause_sem);
        print_state(wait_counter);
        wait_counter++;
        usleep(500000 / sim_speed); // Update every 0.5 seconds
        if (wait_counter >= 1000) wait_counter = 0; // Prevent overflow
    }
    print_state(wait_counter); // Final state
    return NULL;
}

void* input_thread_func(void* arg) {
    while (!final_trip_done) {
        int ch = getch();
        switch (ch) {
            case 'p': case 'P':
                sem_wait(&pause_sem);
                paused = !paused;
                if (!paused) sem_post(&pause_sem);
                break;
            case '+':
                if (sim_speed < 5.0) sim_speed += 0.5;
                break;
            case '-':
                if (sim_speed > 0.5) sim_speed -= 0.5;
                break;
            case 'q': case 'Q':
                final_trip_done = 1;
                if (paused) {
                    paused = 0;
                    sem_post(&pause_sem);
                }
                break;
        }
    }
    return NULL;
}

int main() {
    srand(time(NULL));
    sim_start_time = time(NULL);

    // Ncurses başlat
    initscr();
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK); // Car
    init_pair(2, COLOR_BLUE, COLOR_BLACK); // Minibus
    init_pair(3, COLOR_YELLOW, COLOR_BLACK); // Truck
    init_pair(4, COLOR_CYAN, COLOR_BLACK); // Ferry
    init_pair(5, COLOR_RED, COLOR_BLACK); // Paused
    init_pair(6, COLOR_GREEN, COLOR_BLACK); // Running
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE); // Klavye girişi için

    // Pencereleri oluştur
    main_win = newwin(22, 80, 0, 10);
    ferry_win = newwin(10, 80, 22, 10);
    stats_win = newwin(10, 80, 32, 10);
    log_win = newwin(15, 80, 42, 0);
    status_win = newwin(4, 80, 57, 10);
    scrollok(log_win, TRUE);

    pthread_t vehicle_threads[TOTAL_VEHICLES];
    pthread_t ferry_thread, printer_thread, input_thread;
    sem_init(&pause_sem, 0, 1);

    // Semaphore başlat
    for (int i = 0; i < 4; i++) sem_init(&toll_sem[i], 0, 1);

    // Araçları başlat
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

    // Zaman dizilerini sıfırla
    memset(wait_time_x, 0, sizeof(wait_time_x));
    memset(wait_time_y, 0, sizeof(wait_time_y));
    memset(ferry_time_x, 0, sizeof(ferry_time_x));
    memset(ferry_time_y, 0, sizeof(ferry_time_y));
    memset(round_trip_time, 0, sizeof(round_trip_time));

    // Threadleri oluştur
    pthread_create(&ferry_thread, NULL, ferry_func, NULL);
    pthread_create(&printer_thread, NULL, print_state_thread, NULL);
    pthread_create(&input_thread, NULL, input_thread_func, NULL);
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_create(&vehicle_threads[i], NULL, vehicle_func, &vehicles[i]);

    // Threadleri join et
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_join(vehicle_threads[i], NULL);
    pthread_join(ferry_thread, NULL);
    pthread_join(printer_thread, NULL);
    pthread_join(input_thread, NULL);

    // İstatistikleri göster
    show_final_statistics();

    // Temizlik
    delwin(main_win);
    delwin(ferry_win);
    delwin(stats_win);
    delwin(log_win);
    delwin(status_win);
    endwin();

    for (int i = 0; i < 4; i++) sem_destroy(&toll_sem[i]);
    sem_destroy(&pause_sem);
    pthread_mutex_destroy(&boarding_mutex);
    pthread_mutex_destroy(&return_mutex);
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&print_mutex);

    printf("\nSimulation completed. Log saved to ferry_log.json!\n");
    return 0;
}