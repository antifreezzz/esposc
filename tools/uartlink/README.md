# uartlink: проверка провода синт <-> панель

Связь с панелью теперь в основной прошивке (`main/panel_uart.c`, `panel/`, 1 Мбод); этот стенд - только для
проверки провода.

Синт (DevKit) шлёт `ping N\n` 100 раз в секунду по UART1, панель Guition возвращает байты как есть.
Синт раз в 5 с пишет в лог `ok / bad / lost` и задержку туда-обратно.

Провода (3 жилы, питание у каждой платы своё, 5V не соединять):

| DevKit | | панель, H1 левый столбец |
|---|---|---|
| J1-10 IO17 (TX) | → | RXD IO40 |
| J1-11 IO18 (RX) | ← | TXD IO2 |
| GND | — | GND |

```sh
. ~/esp/esp-idf-v6.1/export.sh
# DevKit, только COM
idf.py -B build-synth -DLINK_ROLE=synth -DSDKCONFIG=build-synth/sdkconfig -p /dev/ttyACM0 flash
# панель (перед этим: systemctl --user stop esp32-display.service)
idf.py -B build-panel -DLINK_ROLE=panel -DSDKCONFIG=build-panel/sdkconfig -p /dev/ttyUSB0 flash
# скорость: -DLINK_BAUD=1000000 обеим сторонам
# прозвонка без UART (свой -B, например build-synth-diag): -DLINK_DIAG=1 - TX держит 1, RX читает с подтяжкой
# к земле; -DLINK_DIAG=2 - наоборот. 1 на RX - провод от другой платы дошёл. После прозвонки прошить эхо обратно.
# хост-тест сверки
cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build
```

Замер 2026-10-31 на паяных проводах, 30 с: 115200, 1 и 2 Мбод - ни одной ошибки после старта
(bad 1 и lost 3 в первые 5 с - платы перезагружаются при открытии портов). Туда-обратно: 3.4 мс, 0.53 мс и 0.33 мс.

Перезаписывает прошивку обеих плат. Синт потом вернуть: `idf.py -p /dev/ttyACM0 flash` из корня
(раздел тарелок не трогается). Панель: бэкап `~/Documents/projects/esp/panel-backup/panel_full_16MB.bin`,
`esptool.py -p /dev/ttyUSB0 -b 460800 write_flash 0 panel_full_16MB.bin`.
