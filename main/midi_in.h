#pragma once
#include "acid_midi.h"
#include "esp_err.h"

// Приём USB-MIDI с class-compliant устройства (BeatStep Pro) через USB-хост.
esp_err_t midi_in_start(void);
// 1, пока MIDI-устройство подключено и открыто.
int midi_in_connected(void);
// Забрать одно событие без ожидания: 1 если есть, 0 если очередь пуста.
int midi_in_receive(acid_midi_event_t *ev);
