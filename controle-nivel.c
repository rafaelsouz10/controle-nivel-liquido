#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "animacoes_led.pio.h" // Anima��es LEDs PIO
#include "math.h"
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
#define BOTAO_A 5

// Configura��o da matriz de LEDs
#define NUM_PIXELS 25          // N�mero de LEDs na matriz
#define matriz_leds 7          // Pino de sa�da para matriz

// Vari�veis globais
PIO pio;                      // Controlador PIO
uint sm;                      // State Machine do PIO

// Wi-Fi
#define WIFI_SSID "Paixao 2"
#define WIFI_PASS "25931959"

// Vari�veis globais do sistema
volatile int nivel_min = 2000;
volatile int nivel_max = 3000;
volatile uint16_t nivel_atual = 0;
volatile bool bomba_ligada = false;

uint32_t cor_verde = 0xFF000000;
uint32_t cor_vermelho = 0x00FF0000;

// P�gina HTML servida pelo servidor
const char HTML_BODY[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Controle da Bomba</title>"
"<style>"
"body{font-family:sans-serif;text-align:center;padding:10px;}"
"h1,h2{margin:10px;}"
".barra{width:60%;height:20px;background:#ddd;margin:10px auto;border-radius:10px;overflow:hidden;}"
".preenchimento{height:100%;background:#2196F3;transition:width 0.3s;}"
".botao{font-size:20px;padding:10px 30px;margin:10px;border:none;border-radius:8px;color:white;}"
".reset{background:#FF9800;}"
"input{padding:5px;margin:5px;width:80px;}"
"#confirm{color:green;font-weight:bold;margin-top:10px;}"
"</style>"
"<script>"
"function inicializar(){"
"fetch('/estado').then(r=>r.json()).then(data=>{"
"document.getElementById('min').value = data.min;"
"document.getElementById('max').value = data.max;"
"atualizar(data);"
"});"
"}"
"function atualizar(data){"
"if(!data){"
"fetch('/estado').then(r=>r.json()).then(d=>{"
"document.getElementById('bomba').innerText=d.bomba?'Ligada':'Desligada';"
"document.getElementById('nivel_valor').innerText=d.nivel;"
"document.getElementById('barra_nivel').style.width=(d.nivel/4095*100)+'%';"
"});"
"}else{"
"document.getElementById('bomba').innerText=data.bomba?'Ligada':'Desligada';"
"document.getElementById('nivel_valor').innerText=data.nivel;"
"document.getElementById('barra_nivel').style.width=(data.nivel/4095*100)+'%';"
"}"
"}"
"setInterval(function(){atualizar();},1000);"
"function configurar(){"
"var min = document.getElementById('min').value;"
"var max = document.getElementById('max').value;"
"fetch('/config?min='+min+'&max='+max).then(r=>r.json()).then(data=>{"
"document.getElementById('confirm').innerText = "
"'Configuração atualizada: min=' + data.min + ', max=' + data.max;"
"});"
"}"
"function resetar(){"
"fetch('/reset').then(r=>r.text()).then(data=>{"
"document.getElementById('confirm').innerText = data;"
"document.getElementById('min').value = 2000;"
"document.getElementById('max').value = 3000;"
"});"
"}"
"window.onload = inicializar;"
"</script></head><body>"
"<h1>CEPEDI TIC37</h1>"
"<h2>EMBARCATECH</h2>"
"<p>Bomba: <span id='bomba'>--</span></p>"
"<div class='barra'><div id='barra_nivel' class='preenchimento' style='width:0'></div></div>"
"<p>Nivel: <span id='nivel_valor'>--</span></p>"
"<div style='margin-top:20px;'>"
"<input type='number' id='min' placeholder='Mínimo'>"
"<input type='number' id='max' placeholder='Máximo'>"
"<button class='botao' style='background:#2196F3;' onclick='configurar()'>Configurar</button>"
"</div>"
"<button class='botao reset' onclick='resetar()'>Resetar Limites</button>"
"<p id='confirm'></p>"
"</body></html>";

// Estrutura para manter o estado da resposta HTTP
struct http_state {
    char response[4096];
    size_t len;
    size_t sent;
};

// Callback botão A
void reset_callback(uint gpio, uint32_t events) {
    nivel_min = 2000;
    nivel_max = 3000;
    printf("[RESET BUTTON] Limites resetados: min=2000, max=3000\n");
}

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

// Recebe e trata as requisi��es
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
    else if (strstr(req, "GET /config?")) {
        char *min_str = strstr(req, "min=");
        char *max_str = strstr(req, "max=");
        if (min_str) nivel_min = atoi(min_str + 4);
        if (max_str) nivel_max = atoi(max_str + 4);

        char config_json[64];
        int config_len = snprintf(config_json, sizeof(config_json),
            "{\"min\":%d,\"max\":%d}", nivel_min, nivel_max);

        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            config_len, config_json);
    } 
    else if (strstr(req, "GET /reset")) {
        nivel_min = 2000;
        nivel_max = 3000;
        const char *resp = "Limites resetados para padrão (2000 / 3000)";
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(resp), resp);
        printf("[RESET] Limites resetados: min=2000, max=3000\n");
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

// L�gica de acionamento autom�tico da bomba
void atualizar_bomba(int nivel) {
    if (nivel < nivel_min && !bomba_ligada) {
        gpio_put(RELAY_PIN, 1);
        bomba_ligada = true;
    } else if (nivel > nivel_max && bomba_ligada) {
        gpio_put(RELAY_PIN, 0);
        bomba_ligada = false;
    }
}

void Ligar_matriz_leds() {
    // Calcula o percentual atual baseado no n�vel
    uint32_t cor_nivel_RGB = 0;
    int percentual_nivel = (int)(((float)(nivel_atual - nivel_min) / (nivel_max - nivel_min)) * 100);

    // Determina quantas linhas devem ficar acesas (cada linha = 20%)
    int linhas_acesas = ceil((float)percentual_nivel / 20); // linhas arredondando para cima
    if (linhas_acesas > 5) linhas_acesas = 5;  // Garante que n�o passe de 5 linhas

    if (linhas_acesas <= 2){
        cor_nivel_RGB = cor_vermelho;

    }else{
        cor_nivel_RGB = cor_verde;
    }

    for (int i = 0; i < NUM_PIXELS; i++) {
        uint32_t valor_led = 0;
        int linha = i / 5; // Considerando matriz 5x5 (5 pixels por linha)

        if (linha < linhas_acesas) {
            // Define a cor que quiser para indicar "n�vel cheio"
            valor_led = cor_nivel_RGB;  // Por exemplo, uma cor fixa tipo verde
        } else {
            valor_led = 0x000000;  // LED apagado
        }

        pio_sm_put_blocking(pio, sm, valor_led);
    }
}


// Fun��o principal
int main() {
    stdio_init_all();
    display_init();

    bootsel_btn_callback();

    adc_init();
    adc_gpio_init(PINO_POTENCIOMETRO);

    gpio_init(RELAY_PIN);
    gpio_set_dir(RELAY_PIN, GPIO_OUT);
    gpio_put(RELAY_PIN, 0);

    gpio_init(LED_BOMBA);
    gpio_set_dir(LED_BOMBA, GPIO_OUT);
    gpio_put(LED_BOMBA, 0);

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, reset_callback);

    // Inicialização PIO para matriz de LEDs
    pio = pio0;
    uint offset = pio_add_program(pio, &animacoes_led_program);
    sm = pio_claim_unused_sm(pio, true);
    animacoes_led_program_init(pio, sm, offset, matriz_leds);

    if (cyw43_arch_init()) return 1;
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 5000)) {
        ssd1306_fill(&ssd, 0);
        ssd1306_draw_string(&ssd, "Falha Wi-Fi", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

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

        adc_select_input(2);  // GPIO 28 = ADC2
        nivel_atual = adc_read();
        atualizar_bomba(nivel_atual);
        gpio_put(LED_BOMBA, bomba_ligada);

        char str_nivel[10], str_bomba[10];
        snprintf(str_nivel, sizeof(str_nivel), "%d", nivel_atual);
        snprintf(str_bomba, sizeof(str_bomba), "%s", bomba_ligada ? "ON" : "OFF");

        Ligar_matriz_leds();

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