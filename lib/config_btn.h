#ifndef BOOTSEL_BTN_H
#define BOOTSEL_BTN_H

#include "pico/bootrom.h"
#define botaoB 6
#define BOTAO_A 5

void irq_handler(uint gpio, uint32_t events) {
    if (gpio == BOTAO_A) {
        nivel_min = 2000;
        nivel_max = 3000;
        reset_pendente = true;
        printf("[RESET BUTTON A] Limites resetados: min=2000, max=3000\n");
    } else if (gpio == botaoB) {
        printf("[BOOTSEL BUTTON B] Entrando em modo boot\n");
        reset_usb_boot(0, 0);
    }
}

void bootsel_btn_callback() {
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, irq_handler);

    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, irq_handler);
}

#endif
