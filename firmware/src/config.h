// Конфигурация сборки. Сеть Wi-Fi здесь не задаётся: модуль хранит её в NVS, а новую плату
// настраивают мастером на точке WakeUpLight-XXXX. Пустой WIFI_SSID — мастер с первого запуска.
#pragma once
#define WIFI_SSID   ""
#define WIFI_PASS   ""
#define NTP_SERVER  "pool.ntp.org"
#define BLE_NAME    "WakeUpLight-ESP32"
#define A2DP_NAME   "WakeUpLight"       // имя Bluetooth-колонки для стрима с телефона
#define TZ_INFO     "EET-2EEST,M3.5.0/3,M10.5.0/4"   // Europe/Kyiv (POSIX TZ для ESP32)
