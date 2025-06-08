#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define ARABA_SAYISI 12
#define MINIBUS_SAYISI 10
#define KAMYON_SAYISI 8
#define FERIBOT_KAPASITESI 20
#define TARAF_A 0 // Başlangıç tarafı
#define TARAF_B 1 // Karşı taraf
#define GISHE_SAYISI 2 // Her yakada 2 gişe
#define MAX_BEKLEME_SURESI 5 // Feribotun maksimum bekleme süresi (saniye)

typedef struct {
    int kimlik;
    int tur;        // 0: Araba, 1: Minibüs, 2: Kamyon
    int liman;      // 0: Başlangıç limanı, 1: Karşı liman
    int gise;       // 0-1: Başlangıç limanı gişeleri, 2-3: Karşı liman gişeleri
    int kapasite;   // 1: Araba, 2: Minibüs, 3: Kamyon
    int bindi;      // Kaç kez feribota bindi
} Arac;

Arac araclar[ARABA_SAYISI + MINIBUS_SAYISI + KAMYON_SAYISI];
pthread_mutex_t feribot_kilit = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gise_kilit[4] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
                                PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
pthread_cond_t feribot_dolu = PTHREAD_COND_INITIALIZER;
int feribot_kapasite = 0;
int feribot_taraf = 0; // Rastgele başlangıç için main'de atanacak
int feribot_hazir = 0;
int araclar_bitti = 0;
int toplam_arac = ARABA_SAYISI + MINIBUS_SAYISI + KAMYON_SAYISI;

const char* arac_turu_al(int tur) {
    static const char* turler[] = {"Araba", "Minibüs", "Kamyon"};
    return turler[tur];
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

void* arac_thread(void* arg) {
    Arac* a = (Arac*)arg;

    while (a->bindi < 2) { // Her araç iki kez binecek (gidiş-dönüş)
        // Gişeden geç
        int gise_no = (a->liman == TARAF_A) ? rand() % GISHE_SAYISI : (2 + rand() % GISHE_SAYISI);
        a->gise = gise_no;
        pthread_mutex_lock(&gise_kilit[gise_no]);
        printf("🚗 %s %d %s limanındaki %d numaralı gişeden geçiyor.\n",
               arac_turu_al(a->tur), a->kimlik, a->liman == TARAF_A ? "Başlangıç" : "Karşı", gise_no % 2);
        usleep(100000); // Gişe geçiş süresi
        pthread_mutex_unlock(&gise_kilit[gise_no]);

        // Bekleme alanına gir
        pthread_mutex_lock(&feribot_kilit);
        printf("🚗 %s %d %s limanındaki bekleme alanına girdi.\n",
               arac_turu_al(a->tur), a->kimlik, a->liman == TARAF_A ? "Başlangıç" : "Karşı");
        pthread_mutex_unlock(&feribot_kilit);

        // Feribota bin
        pthread_mutex_lock(&feribot_kilit);
        while (!(feribot_taraf == a->liman && feribot_kapasite + a->kapasite <= FERIBOT_KAPASITESI) && !araclar_bitti) {
            pthread_mutex_unlock(&feribot_kilit);
            usleep(100000);
            pthread_mutex_lock(&feribot_kilit);
        }

        if (araclar_bitti) {
            pthread_mutex_unlock(&feribot_kilit);
            break;
        }

        feribot_kapasite += a->kapasite;
        a->bindi++;
        printf("🚗 %s %d %s limanından feribota bindi. (Kapasite: %d/%d)\n",
               arac_turu_al(a->tur), a->kimlik, a->liman == TARAF_A ? "Başlangıç" : "Karşı",
               feribot_kapasite, FERIBOT_KAPASITESI);

        if (feribot_kapasite >= FERIBOT_KAPASITESI) {
            feribot_hazir = 1;
            pthread_cond_signal(&feribot_dolu);
        }
        pthread_mutex_unlock(&feribot_kilit);

        // Karşıya geçmeyi bekle
        while (feribot_taraf == a->liman) usleep(1000);

        // Karşı limana ulaştı
        a->liman = !a->liman;
        usleep((1 + rand() % 3) * 1000000); // Karşı limanda bekleme
    }

    return NULL;
}

void* feribot_thread(void* arg) {
    time_t son_kalkis = time(NULL);
    while (!araclar_bitti || feribot_kapasite > 0) {
        pthread_mutex_lock(&feribot_kilit);

        // Kalkma koşulları: doluysa veya süre dolduysa
        time_t simdi = time(NULL);
        if (!feribot_hazir && (simdi - son_kalkis < MAX_BEKLEME_SURESI) && !araclar_bitti) {
            pthread_mutex_unlock(&feribot_kilit);
            usleep(100000);
            continue;
        }

        if (feribot_kapasite > 0) {
            printf("\n⛴️ Feribot %s'dan %s'a yola çıkıyor... (%d/%d)\n",
                   feribot_taraf == TARAF_A ? "Başlangıç" : "Karşı",
                   feribot_taraf == TARAF_A ? "Karşı" : "Başlangıç",
                   feribot_kapasite, FERIBOT_KAPASITESI);
            print_progress_bar(feribot_kapasite, FERIBOT_KAPASITESI);

            // Geçiş süresi
            double gecis = 5 + rand() % 5;
            usleep(gecis * 1000000);

            // Karşıya ulaştı
            feribot_taraf = !feribot_taraf;
            feribot_kapasite = 0;
            feribot_hazir = 0;
            son_kalkis = time(NULL);

            printf("🛬 Feribot %s'a ulaştı.\n", feribot_taraf == TARAF_A ? "Başlangıç" : "Karşı");
        }
        pthread_mutex_unlock(&feribot_kilit);
        usleep(100000);
    }

    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t threads[ARABA_SAYISI + MINIBUS_SAYISI + KAMYON_SAYISI];
    pthread_t ferry;

    // Feribotun başlangıç tarafını rastgele seç
    feribot_taraf = rand() % 2;

    // Araçları oluştur
    int index = 0;
    for (int i = 0; i < ARABA_SAYISI; i++)
        araclar[index++] = (Arac){index, 0, rand() % 2, 0, 1, 0}; // Araba
    for (int i = 0; i < MINIBUS_SAYISI; i++)
        araclar[index++] = (Arac){index, 1, rand() % 2, 0, 2, 0}; // Minibüs
    for (int i = 0; i < KAMYON_SAYISI; i++)
        araclar[index++] = (Arac){index, 2, rand() % 2, 0, 3, 0}; // Kamyon

    // Thread'leri başlat
    pthread_create(&ferry, NULL, feribot_thread, NULL);
    for (int i = 0; i < toplam_arac; i++) {
        pthread_create(&threads[i], NULL, arac_thread, &araclar[i]);
    }

    // Thread'lerin bitmesini bekle
    for (int i = 0; i < toplam_arac; i++) {
        pthread_join(threads[i], NULL);
    }

    araclar_bitti = 1;
    pthread_join(ferry, NULL);

    printf("Simülasyon tamamlandı!\n");
    return 0;
}