#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bootsel_btn.h"
#include "lib/config_display.h"

#define LED_BOMBA 12
#define RELAY_PIN 8
#define PINO_POTENCIOMETRO 28  // ADC2

#define WIFI_SSID "AP_"
#define WIFI_PASS "congresso@"

volatile int nivel_min = 1000;
volatile int nivel_max = 3000;
volatile uint16_t nivel_atual = 0;
volatile bool bomba_ligada = false;

const char HTML_BODY[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Controle de Nível</title>"
"<style>body{font-family:sans-serif;text-align:center;}"
"input{font-size:20px;}button{font-size:20px;margin-top:10px;}</style>"
"<script>"
"function atualizar(){"
"fetch('/estado').then(r=>r.json()).then(data=>{"
"document.getElementById('nivel').innerText=data.nivel;"
"document.getElementById('bomba').innerText=data.bomba?'Ligada':'Desligada';"
"document.getElementById('min').innerText=data.min;"
"document.getElementById('max').innerText=data.max;"
"});}"
"setInterval(atualizar,1000);"
"function enviar(){"
"const min=document.getElementById('min_in').value;"
"const max=document.getElementById('max_in').value;"
"fetch('/config?min='+min+'&max='+max).then(_=>alert('Configurado'));}"
"</script></head><body>"
"<h1>Controle de Nível</h1>"
"<p>Nível atual: <span id='nivel'>--</span></p>"
"<p>Bomba: <span id='bomba'>--</span></p>"
"<p>Limite Mínimo: <span id='min'>--</span></p>"
"<p>Limite Máximo: <span id='max'>--</span></p>"
"<p><input id='min_in' placeholder='Novo Mínimo'>"
"<input id='max_in' placeholder='Novo Máximo'></p>"
"<button onclick='enviar()'>Configurar</button>"
"</body></html>";

struct http_state {
    char response[1024];
    size_t len;
    size_t sent;
};

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len) {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }
    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs) {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    if (strstr(req, "GET /config?")) {
        char *min_s = strstr(req, "min=");
        char *max_s = strstr(req, "max=");
        if (min_s) nivel_min = atoi(min_s + 4);
        if (max_s) nivel_max = atoi(max_s + 4);
        const char *resp = "OK";
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(resp), resp);
    } else if (strstr(req, "GET /estado")) {
        char json[128];
        snprintf(json, sizeof(json),
            "{\"nivel\":%d,\"bomba\":%d,\"min\":%d,\"max\":%d}",
            nivel_atual, bomba_ligada, nivel_min, nivel_max);
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(json), json);
    } else {
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) return;
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
}

void atualizar_bomba(int nivel) {
    if (nivel < nivel_min && !bomba_ligada) {
        gpio_put(RELAY_PIN, 1);
        bomba_ligada = true;
    } else if (nivel > nivel_max && bomba_ligada) {
        gpio_put(RELAY_PIN, 0);
        bomba_ligada = false;
    }
}

int main() {
    stdio_init_all();
    display_init();

    adc_init();
    adc_gpio_init(PINO_POTENCIOMETRO);

    gpio_init(RELAY_PIN);
    gpio_set_dir(RELAY_PIN, GPIO_OUT);
    gpio_put(RELAY_PIN, 0);

    gpio_init(LED_BOMBA);
    gpio_set_dir(LED_BOMBA, GPIO_OUT);
    gpio_put(LED_BOMBA, 0);

    if (cyw43_arch_init()) return 1;
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 5000)) {
        ssd1306_fill(&ssd, 0);
        ssd1306_draw_string(&ssd, "Falha Wi-Fi", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    // Obtem o IP e monta string
    uint32_t ip_addr = cyw43_state.netif[0].ip_addr.addr;
    uint8_t *ip = (uint8_t *)&ip_addr;
    char ip_display[24];
    snprintf(ip_display, sizeof(ip_display), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    // Exibe mensagem inicial
    ssd1306_fill(&ssd, 0);
    ssd1306_draw_string(&ssd, "Wi-Fi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_display, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();

    while (1) {
        cyw43_arch_poll();

        adc_select_input(2);
        nivel_atual = adc_read();
        atualizar_bomba(nivel_atual);
        gpio_put(LED_BOMBA, bomba_ligada);

        char str_nivel[10], str_bomba[10];
        snprintf(str_nivel, sizeof(str_nivel), "%d", nivel_atual);
        snprintf(str_bomba, sizeof(str_bomba), "%s", bomba_ligada ? "ON" : "OFF");

        ssd1306_fill(&ssd, 0);

        // Moldura e divisões
        ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
        ssd1306_line(&ssd, 3, 25, 123, 25, true);
        ssd1306_line(&ssd, 3, 37, 123, 37, true);

        // Cabeçalho
        ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6);
        ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);

        // IP
        ssd1306_draw_string(&ssd, ip_display, 4, 28);

        // Nível
        ssd1306_draw_string(&ssd, "Nivel:", 8, 40);
        ssd1306_draw_string(&ssd, str_nivel, 50, 40);

        // Bomba
        ssd1306_draw_string(&ssd, "Bomba:", 8, 50);
        ssd1306_draw_string(&ssd, str_bomba, 60, 50);

        ssd1306_send_data(&ssd);
        sleep_ms(500);
    }
    cyw43_arch_deinit();
    return 0;
}
