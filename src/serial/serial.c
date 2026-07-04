#include "serial/serial.h"

#define REGISTER_DATA 0U
#define REGISTER_INTERRUPT_ENABLE 1U
#define REGISTER_DIVISOR_LOW 0U
#define REGISTER_DIVISOR_HIGH 1U
#define REGISTER_FIFO_CONTROL 2U
#define REGISTER_LINE_CONTROL 3U
#define REGISTER_MODEM_CONTROL 4U
#define REGISTER_LINE_STATUS 5U

#define LINE_CONTROL_DIVISOR_LATCH 0x80U
#define LINE_CONTROL_8N1 0x03U
#define FIFO_ENABLE_AND_CLEAR 0xC7U
#define MODEM_READY 0x0BU
#define MODEM_LOOPBACK 0x1EU
#define LINE_STATUS_TRANSMIT_EMPTY 0x20U

#define DIVISOR_115200 1U
#define LOOPBACK_PROBE 0xAEU

static uint16_t serial_port;

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

bool serial_init(uint16_t port) {
    out8(port + REGISTER_INTERRUPT_ENABLE, 0);
    out8(port + REGISTER_LINE_CONTROL, LINE_CONTROL_DIVISOR_LATCH);
    out8(port + REGISTER_DIVISOR_LOW, DIVISOR_115200);
    out8(port + REGISTER_DIVISOR_HIGH, 0);
    out8(port + REGISTER_LINE_CONTROL, LINE_CONTROL_8N1);
    out8(port + REGISTER_FIFO_CONTROL, FIFO_ENABLE_AND_CLEAR);

    /* Loop the line back and see whether the byte returns. A port that is not
       there reads as 0xFF, and writing to it forever would hang the loader. */
    out8(port + REGISTER_MODEM_CONTROL, MODEM_LOOPBACK);
    out8(port + REGISTER_DATA, LOOPBACK_PROBE);
    if (in8(port + REGISTER_DATA) != LOOPBACK_PROBE) return false;

    out8(port + REGISTER_MODEM_CONTROL, MODEM_READY);
    serial_port = port;
    return true;
}

static void serial_put(char value) {
    if (!serial_port) return;
    while ((in8(serial_port + REGISTER_LINE_STATUS) & LINE_STATUS_TRANSMIT_EMPTY) == 0)
        ;
    out8(serial_port + REGISTER_DATA, (uint8_t)value);
}

void serial_write(const char *text, size_t length) {
    for (size_t index = 0; index < length; index++) {
        if (text[index] == '\n') serial_put('\r');
        serial_put(text[index]);
    }
}

void serial_write_cstr(const char *text) {
    for (size_t index = 0; text[index] != '\0'; index++) {
        if (text[index] == '\n') serial_put('\r');
        serial_put(text[index]);
    }
}
