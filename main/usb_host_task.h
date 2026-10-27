#pragma once

// Ставит USB Host Library, крутит её события в своей задаче и логирует
// каждое новое устройство (VID:PID, класс). Возвращается, когда хост готов.
void usb_host_start(void);
