# esposc

Синтезатор на ESP32-S3. Этап 1: кислотный паттерн через USB-аудиосвисток.

## Железо

- ESP32-S3-DevKitC-1 N8R2.
- Разъём **COM** (CH343) -> ПК: прошивка и лог (`/dev/ttyACM0`).
- Разъём **USB** (родной OTG) -> хаб Canyon CNS-TDS04DG (вход Type-C хаба).
  - В PD-вход Type-C хаба - зарядка 5 В: питает хаб, свисток и плату.
  - Свисток (UAC 1.0) в USB-A хаба через переходник A->C.
- ⚠️ Разъём **USB** платы никогда не втыкать в ПК, только через COM.

## Сборка

```sh
. ~/esp/esp-idf-v6.1/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Ядро синта на ПК

```sh
cmake -S host -B host/build && cmake --build host/build
ctest --test-dir host/build
host/build/acid_render out.wav 30   # превью паттерна
```
