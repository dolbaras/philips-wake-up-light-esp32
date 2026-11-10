#!/usr/bin/env bash
# Прогон всех хост-тестов ядра прошивки (без ESP32).
set -e
cd "$(dirname "$0")/.."
run(){ echo "== $1"; g++ -std=c++17 -w "${@:2}" -o /tmp/wul_t && /tmp/wul_t; }
run frame        -I src/wul test/native/test_frame.cpp src/wul/wul_frame.cpp
run scheduler    -I src/scheduler test/native/test_scheduler.cpp src/scheduler/scheduler.cpp
run snooze       -I src/scheduler test/native/test_snooze.cpp
run state        -I src -I src/api -I src/scheduler test/native/test_state.cpp src/api/app_state.cpp src/scheduler/scheduler.cpp
run integration  -I src/wul test/native/test_integration.cpp src/wul/wul_frame.cpp src/wul/wul_client.cpp
run iap          -I src/iap test/native/test_iap.cpp src/iap/iap_link.cpp
run engine       -I src/core -I src/wul test/native/test_engine.cpp src/core/engine.cpp src/core/model.cpp src/wul/wul_frame.cpp
run display      -I src/wul test/native/test_display.cpp src/wul/display_fx.cpp
run eq           -I src/core test/native/test_eq.cpp
echo "ВСЕ ТЕСТЫ ПРОШЛИ"
