# Ferry Tour Simulation

A multithreaded vehicle transportation system implemented in C using POSIX threads to simulate a ferry transporting vehicles between two city sides. This project serves as an educational tool to understand concurrency, synchronization, and optimization concepts in operating systems.
---
### 📄 Simulation Paper

[📥 View the full simulation paper (PDF)](Fery-Tour-Simulation-Paper.pdf)
## Table of Contents

- Overview
- Features
- References
- Türkçe

---

## Overview

The **Ferry Tour Simulation** models a ferry service connecting two city sides (Side-A and Side-B) with 30 vehicles (12 cars, 10 minibuses, 8 trucks) of varying capacities (1, 2, and 4 units, respectively). The ferry, with a 20-unit capacity, transports vehicles across a river, with vehicles navigating through four tolls (two per side) to complete round-trips. Implemented in C with POSIX threads, the system uses mutexes and semaphores for thread safety, dynamic programming for optimizing ferry departures, and real-time visualization for monitoring.

The project aims to:

- Demonstrate multithreading and synchronization techniques.
- Optimize ferry operations for efficiency.
- Provide educational insights into operating system concepts.
- Analyze performance and identify areas for improvement.

Tested on a Linux Mint 22.1 system with an Intel i7-11800H CPU, the simulation completed 12 trips in 88 seconds but faced challenges like logging inaccuracies and a 33.3% empty trip rate.

---

## Features

- **Multithreading**: 30 vehicle threads, 1 ferry thread, and 1 visualization thread operate concurrently.
- **Synchronization**: Mutexes and semaphores ensure thread safety for tolls, ferry boarding, and logging.
- **Optimization**: Dynamic programming minimizes idle time by checking capacity feasibility.
- **Real-Time Visualization**: Console updates every 0.5 seconds with vehicle locations, ferry status, and progress.
- **Logging**: Trip details and vehicle metrics (wait times, ferry times, round-trip times) saved to `ferry_log.txt`.

---

## References

1. A. Silberschatz, P. B. Galvin, and G. Gagne, *Operating System Concepts*, 10th ed. Hoboken, NJ, USA: Wiley, 2018.
2. B. W. Kernighan and D. M. Ritchie, *The C Programming Language*, 2nd ed. Englewood Cliffs, NJ, USA: Prentice Hall, 1988.
3. W. Stallings, *Operating Systems: Internals and Design Principles*, 9th ed. Upper Saddle River, NJ, USA: Pearson, 2018.

---

## Türkçe

# Feribot Tur Simülasyonu

C programlama dili ve POSIX thread'leri kullanılarak geliştirilen, çok iş parçacıklı bir araç taşıma sistemi simülasyonu. Bu proje, işletim sistemlerinde eşzamanlılık, senkronizasyon ve optimizasyon kavramlarını öğretmek için bir eğitim aracı olarak tasarlanmıştır.

---

## İçindekiler

- Genel Bakış
- Özellikler
- Kaynaklar

---

## Genel Bakış

**Feribot Tur Simülasyonu**, iki şehir tarafını (Taraf-A ve Taraf-B) bağlayan bir feribot hizmetini modellemektedir. 30 araç (12 otomobil: 1 birim, 10 minibüs: 2 birim, 8 kamyon: 4 birim) ile çalışır ve feribot 20 birim kapasiteye sahiptir. Araçlar, her biri iki gişe bulunan dört gişeden (her tarafta iki gişe) geçerek gidiş-dönüş yolculuklarını tamamlar. C dili ve POSIX thread'leri ile geliştirilen sistem, mutex ve semaforlar ile iş parçacığı güvenliğini sağlar, dinamik programlama ile feribot kalkışlarını optimize eder ve gerçek zamanlı görselleştirme ile izleme sunar.

Projenin amaçları:

- Çok iş parçacıklı programlama ve senkronizasyon tekniklerini göstermek.
- Feribot operasyonlarını verimlilik açısından optimize etmek.
- İşletim sistemleri kavramlarına dair eğitimsel bilgiler sunmak.
- Performans analizi yaparak iyileştirme alanlarını belirlemek.

Linux Mint 22.1 sisteminde (Intel i7-11800H CPU) test edilen simülasyon, 88 saniyede 12 tur tamamladı ancak günlük kaydı hataları ve %33,3 boş tur oranı gibi sorunlarla karşılaştı.

---

## Özellikler

- **Çok İş Parçacığı**: 30 araç, 1 feribot ve 1 görselleştirme iş parçacığı eşzamanlı çalışır.
- **Senkronizasyon**: Mutex ve semaforlar, gişeler, feribot yükleme ve günlük kaydı için iş parçacığı güvenliğini sağlar.
- **Optimizasyon**: Dinamik programlama, kapasite fizibilitesini kontrol ederek boşta kalma süresini azaltır.
- **Gerçek Zamanlı Görselleştirme**: Konsol, her 0.5 saniyede bir araç konumları, feribot durumu ve ilerleme ile güncellenir.
- **Günlük Kaydı**: Tur detayları ve araç metrikleri (`ferry_log.txt` dosyasına kaydedilir).

---


## Kaynaklar

1. A. Silberschatz, P. B. Galvin ve G. Gagne, *Operating System Concepts*, 10. baskı. Hoboken, NJ, ABD: Wiley, 2018.
2. B. W. Kernighan ve D. M. Ritchie, *The C Programming Language*, 2. baskı. Englewood Cliffs, NJ, ABD: Prentice Hall, 1988.

W. Stallings, *Operating Systems: Internals and Design Principles*, 9. baskı. Upper Saddle River, NJ, ABD: Pearson, 2018.