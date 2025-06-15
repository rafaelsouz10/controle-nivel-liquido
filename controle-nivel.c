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

// GPIOs
#define LED_BOMBA 12
#define RELAY_PIN 8
#define PINO_POTENCIOMETRO 28  // ADC2

// Wi-Fi
#define WIFI_SSID "TP LINK"
#define WIFI_PASS "nilton123"

// Variáveis globais do sistema
volatile int nivel_min = 2000;
volatile int nivel_max = 3000;
volatile uint16_t nivel_atual = 0;
volatile bool bomba_ligada = false;

// Página HTML servida pelo servidor
const char HTML_BODY[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Controle da Bomba</title>"
"<style>"
"body{font-family:sans-serif;text-align:center;padding:10px;}"
"h1,h2{margin:10px;}"
".barra{width:60%;height:20px;background:#ddd;margin:10px auto;border-radius:10px;overflow:hidden;}"
".preenchimento{height:100%;background:#2196F3;transition:width 0.3s;}"
".botao{font-size:20px;padding:10px 30px;margin:10px;border:none;border-radius:8px;color:white;}"
".ligar{background:#4CAF50;}"
".desligar{background:#f44336;}"
"</style>"
"<script>"
"function atualizar(){"
"fetch('/estado').then(r=>r.json()).then(data=>{"
"document.getElementById('bomba').innerText=data.bomba?'Ligada':'Desligada';"
"document.getElementById('nivel_valor').innerText=data.nivel;"
"document.getElementById('barra_nivel').style.width=(data.nivel/4095*100)+'%';"
"});}setInterval(atualizar,1000);"
"function comando(cmd){fetch('/comando?acao='+cmd);}"
"</script></head><body>"
"<h1>CEPEDI TIC37</h1>"
"<h2>EMBARCATECH</h2>"
"<p>Bomba: <span id='bomba'>--</span></p>"
"<div class='barra'><div id='barra_nivel' class='preenchimento' style='width:0'></div></div>"
"<p>Nível: <span id='nivel_valor'>--</span></p>"
"<button class='botao ligar' onclick=\"comando('ligar')\">Ligar</button>"
"<button class='botao desligar' onclick=\"comando('desligar')\">Desligar</button>"
"</body></html>";

// Estrutura para manter o estado da resposta HTTP
struct http_state {
    char response[4096];
    size_t len;
    size_t sent;
};

// Callback quando parte da resposta foi enviada
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len) {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

// Recebe e trata as requisições
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

    if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html", 15) == 0) {
        // Serve o HTML principal
        int content_length = strlen(HTML_BODY);
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            content_length, HTML_BODY);
    } 
    else if (strstr(req, "GET /estado")) {
        char json[128];
        int json_len = snprintf(json, sizeof(json),
            "{\"nivel\":%d,\"bomba\":%d,\"min\":%d,\"max\":%d}",
            nivel_atual, bomba_ligada, nivel_min, nivel_max);
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            json_len, json);
    } 
    else if (strstr(req, "GET /comando?")) {
        if (strstr(req, "acao=ligar")) {
            gpio_put(RELAY_PIN, 1);
            bomba_ligada = true;
        } else if (strstr(req, "acao=desligar")) {
            gpio_put(RELAY_PIN, 0);
            bomba_ligada = false;
        }
        const char *resp = "OK";
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(resp), resp);
    } 
    else {
        const char *resp = "404 Not Found";
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(resp), resp);
    }

    // Debug do conteúdo
    printf("=== HTTP RESPONSE ===\n%s\n=== END RESPONSE ===\n", hs->response);

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}


// Configura nova conexão
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Inicializa servidor HTTP
static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) return;
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
}

// Lógica de acionamento automático da bomba
void atualizar_bomba(int nivel) {
    if (nivel < nivel_min && !bomba_ligada) {
        gpio_put(RELAY_PIN, 1);
        bomba_ligada = true;
    } else if (nivel > nivel_max && bomba_ligada) {
        gpio_put(RELAY_PIN, 0);
        bomba_ligada = false;
    }
}

// Função principal
int main() {
    stdio_init_all();
    display_init();

    // Configura ADC
    adc_init();
    adc_gpio_init(PINO_POTENCIOMETRO);

    // Configura pinos da bomba
    gpio_init(RELAY_PIN);
    gpio_set_dir(RELAY_PIN, GPIO_OUT);
    gpio_put(RELAY_PIN, 0);

    gpio_init(LED_BOMBA);
    gpio_set_dir(LED_BOMBA, GPIO_OUT);
    gpio_put(LED_BOMBA, 0);

    // Inicializa Wi-Fi
    if (cyw43_arch_init()) return 1;
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 5000)) {
        ssd1306_fill(&ssd, 0);
        ssd1306_draw_string(&ssd, "Falha Wi-Fi", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    // Exibe IP no display
    uint32_t ip_addr = cyw43_state.netif[0].ip_addr.addr;
    uint8_t *ip = (uint8_t *)&ip_addr;
    char ip_display[24];
    snprintf(ip_display, sizeof(ip_display), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    ssd1306_fill(&ssd, 0);
    ssd1306_draw_string(&ssd, "Wi-Fi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_display, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();

    while (1) {
        cyw43_arch_poll();

        // Leitura e atualização do nível
        adc_select_input(2);
        nivel_atual = adc_read();
        atualizar_bomba(nivel_atual);
        gpio_put(LED_BOMBA, bomba_ligada);

        // Atualiza display
        char str_nivel[10], str_bomba[10];
        snprintf(str_nivel, sizeof(str_nivel), "%d", nivel_atual);
        snprintf(str_bomba, sizeof(str_bomba), "%s", bomba_ligada ? "ON" : "OFF");

        ssd1306_fill(&ssd, 0);
        ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
        ssd1306_line(&ssd, 3, 25, 123, 25, true);
        ssd1306_line(&ssd, 3, 37, 123, 37, true);
        ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6);
        ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);
        ssd1306_draw_string(&ssd, ip_display, 4, 28);
        ssd1306_draw_string(&ssd, "Nivel:", 8, 40);
        ssd1306_draw_string(&ssd, str_nivel, 60, 40);
        ssd1306_draw_string(&ssd, "Bomba:", 8, 50);
        ssd1306_draw_string(&ssd, str_bomba, 60, 50);
        ssd1306_send_data(&ssd);

        sleep_ms(500);
    }

    cyw43_arch_deinit();
    return 0;
}
