# uartlink: проверка провода синт <-> панель

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
# хост-тест сверки
cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build
```

Перезаписывает прошивку обеих плат. Синт потом вернуть: `idf.py -p /dev/ttyACM0 flash` из корня
(раздел тарелок не трогается). Панель: бэкап `~/Documents/projects/esp/panel-backup/panel_full_16MB.bin`,
`esptool.py -p /dev/ttyUSB0 -b 460800 write_flash 0 panel_full_16MB.bin`.
