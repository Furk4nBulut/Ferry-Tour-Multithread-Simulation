#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define ARABA_SAYISI 12
#define MINIBUS_SAYISI 10
#define KAMYON_SAYISI 8
#define FERIBOT_KAPASITESI 20
#define TARAF_A 0 // Bozcaada
#define TARAF_B 1 // İstanbul

typedef struct {
    int kimlik;
    int tur;
    int liman;
    int kapasite;
    int bindi;
} Arac;

Arac araclar[ARABA_SAYISI + MINIBUS_SAYISI + KAMYON_SAYISI];
pthread_mutex_t feribot_kilit = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t feribot_dolu = PTHREAD_COND_INITIALIZER;
int feribot_kapasite = 0;
int feribot_taraf = TARAF_B;
int feribot_hazir = 0;
int toplam_arac = ARABA_SAYISI + MINIBUS_SAYISI + KAMYON_SAYISI;
int ilk_bozcaada = 1; // Sadece ilk dönüşte boş dönecek
double simulasyon_zamani = 0.0;

const char* arac_turu_al(int tur) {
    static const char* turler[] = {"Araba", "Minibüs", "Kamyon"};
    return turler[tur];
}

void simulasyon_zamani_guncelle(double saniye) {
    simulasyon_zamani += saniye;
}

void progress_bar(double sure) {
    printf("[Feribot] Yolda: [");
    fflush(stdout);
    int i;
    int toplam = 20;
    for (i = 0; i < toplam; i++) {
        usleep(sure * 1000000 / toplam);
        printf("#");
        fflush(stdout);
    }
    printf("]\n");
}

void* arac_thread(void* arg) {
    Arac* a = (Arac*)arg;

    while (1) {
        pthread_mutex_lock(&feribot_kilit);

        // Araç feribota binmeye uygun değilse bekle
        while (!(feribot_taraf == a->liman && feribot_kapasite + a->kapasite <= FERIBOT_KAPASITESI)) {
            pthread_mutex_unlock(&feribot_kilit);
            usleep(100000);  // 100ms bekleme
            pthread_mutex_lock(&feribot_kilit);
        }

        // Araç feribota bindi
        feribot_kapasite += a->kapasite;
        a->bindi++;
        printf("🚗 %s %d %s limanından feribota bindi. (Kapasite: %d/%d)\n",
               arac_turu_al(a->tur), a->kimlik, a->liman == TARAF_A ? "Bozcaada" : "İstanbul",
               feribot_kapasite, FERIBOT_KAPASITESI);

        if (feribot_kapasite >= FERIBOT_KAPASITESI) {
            feribot_hazir = 1;
            pthread_cond_signal(&feribot_dolu);  // Feribot doldu, diğer araçlar için sinyal gönder
        }

        pthread_mutex_unlock(&feribot_kilit);

        // Karşıya geçmeyi bekle
        while (feribot_taraf == a->liman) usleep(1000); // Liman değişimini bekle

        // Araç karşı limana geçti
        a->liman = !a->liman;

        usleep((1 + rand() % 3) * 1000000); // Limanda bekleme
    }

    return NULL;
}

void* feribot_thread(void* arg) {
    while (1) {
        pthread_mutex_lock(&feribot_kilit);

        // İlk Bozcaada seferi ve normal sefer kontrolü
        if (feribot_taraf == TARAF_B || !ilk_bozcaada) {
            while (!feribot_hazir)
                pthread_cond_wait(&feribot_dolu, &feribot_kilit);  // Feribot dolduğunda harekete geç
        } else {
            // Boş sefer
            printf("\n⚠️ Feribot Bozcaada'dan ilk kez boş dönüyor!\n");
            sleep(2);
        }

        printf("\n⛴️ Feribot %s'dan %s'a doğru yola çıkıyor... (%d/%d)\n",
               feribot_taraf == TARAF_B ? "İstanbul" : "Bozcaada",
               feribot_taraf == TARAF_B ? "Bozcaada" : "İstanbul",
               feribot_kapasite, FERIBOT_KAPASITESI);

        // Feribot geçiş süresi simülasyonu
        double gecis = 5 + rand() % 5;
        simulasyon_zamani_guncelle(gecis);
        pthread_mutex_unlock(&feribot_kilit);

        progress_bar(gecis);  // Yolda geçişi gösteren progress bar

        pthread_mutex_lock(&feribot_kilit);
        feribot_taraf = !feribot_taraf;  // Feribot karşı limana geçti
        feribot_kapasite = 0;  // Kapasite sıfırla
        feribot_hazir = 0;

        if (ilk_bozcaada && feribot_taraf == TARAF_B)
            ilk_bozcaada = 0;

        printf("🛬 Feribot %s'a ulaştı.\n\n", feribot_taraf == TARAF_B ? "İstanbul" : "Bozcaada");
        pthread_mutex_unlock(&feribot_kilit);
    }

    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t threads[toplam_arac];
    pthread_t ferry;

    int index = 0;
    // Araçları oluştur
    for (int i = 0; i < ARABA_SAYISI; i++)
        araclar[index++] = (Arac){index, 0, TARAF_B, 1, 0};  // Araba
    for (int i = 0; i < MINIBUS_SAYISI; i++)
        araclar[index++] = (Arac){index, 1, TARAF_B, 2, 0};  // Minibüs
    for (int i = 0; i < KAMYON_SAYISI; i++)
        araclar[index++] = (Arac){index, 2, TARAF_B, 3, 0};  // Kamyon

    pthread_create(&ferry, NULL, feribot_thread, NULL);  // Feribot thread'ini başlat

    // Araç thread'lerini başlat
    for (int i = 0; i < toplam_arac; i++)
        pthread_create(&threads[i], NULL, arac_thread, &araclar[i]);

    // Thread'lerin bitmesini bekle
    for (int i = 0; i < toplam_arac; i++)
        pthread_join(threads[i], NULL);

    pthread_join(ferry, NULL);  // Feribot thread'ini bitir
    return 0;
}
