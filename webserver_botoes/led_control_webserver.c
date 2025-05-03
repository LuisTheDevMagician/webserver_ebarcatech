#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "ws2818b.pio.h"
#include "neopixel.c"

#define WIFI_SSID "NOME DA REDE WIFI"
#define WIFI_PASSWORD "SENHA DA REDE WIFI"
#define LED_BLUE_PIN 12
#define LED_MATRIX_PIN 7
#define LED_COUNT 25
#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6

static volatile bool button_checking_enabled = false;
static volatile bool button_a_state = false;
static volatile bool button_b_state = false;

bool check_buttons() {
    if (!button_checking_enabled) return false;

    bool new_button_a_state = !gpio_get(BUTTON_A_PIN); // Pull-up
    bool new_button_b_state = !gpio_get(BUTTON_B_PIN); // Pull-up

    if (new_button_a_state != button_a_state || new_button_b_state != button_b_state) {
        button_a_state = new_button_a_state;
        button_b_state = new_button_b_state;
        printf("Botão A: %s, Botão B: %s\n",
               button_a_state ? "Pressionado" : "Livre",
               button_b_state ? "Pressionado" : "Livre");
        return true;
    }
    return false;
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    if (strncmp(p->payload, "GET", 3) != 0) {
        pbuf_free(p);
        return ERR_OK;
    }

    char *request = (char *)p->payload;
    printf("Request: %.*s\n", p->len, request);

    if (strstr(request, "GET /blue_on")) {
        gpio_put(LED_BLUE_PIN, 1);
        npSetLED(2, 200, 200, 200); 
        npSetLED(7, 200, 200, 200);
        npSetLED(10, 200, 200, 200);
        npSetLED(12, 200, 200, 200);
        npSetLED(14, 200, 200, 200);
        npSetLED(16, 200, 200, 200);
        npSetLED(17, 200, 200, 200);
        npSetLED(18, 200, 200, 200);
        npSetLED(22, 200, 200, 200);
        npWrite();
    }
    else if (strstr(request, "GET /blue_off")) {
        gpio_put(LED_BLUE_PIN, 0);
        npClear();
        npWrite();
    }
    else if (strstr(request, "GET /check_buttons_on")) {
        button_checking_enabled = true;
    }
    else if (strstr(request, "GET /check_buttons_off")) {
        button_checking_enabled = false;
    }

    check_buttons();

    // Temperatura
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float temperature = 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;

    // HTML com charset e refresh
    const char *response_fmt =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<meta http-equiv='refresh' content='1'>\n"
        "<title>Controle de Máquina de Solda Subaquática</title>\n"
        "<style>\n"
        "body { font-family: Arial; margin: 20px; }\n"
        "button { padding: 10px 15px; margin: 5px; }\n"
        ".pressed { color: green; }\n"
        ".released { color: red; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<h1>Controle de Máquina de Solda Subaquática</h1>\n"
        "<form action='/blue_on'><button>Ligar Lanterna</button></form>\n"
        "<form action='/blue_off'><button>Desligar Lanterna</button></form>\n"
        "<form action='/check_buttons_on'><button>Ativar Verificação</button></form>\n"
        "<form action='/check_buttons_off'><button>Desativar Verificação</button></form>\n"
        "<p>Status da Lanterna: <strong>%s</strong></p>\n"
        "<p>Status da Verificação: %s</p>\n"
        "<p>Botão A: <span class='%s'>%s</span></p>\n"
        "<p>Botão B: <span class='%s'>%s</span></p>\n"
        "<p>Temperatura da Ponta de Solda: %.2f °C</p>\n"
        "</body>\n"
        "</html>\n";

    char *html_response = malloc(1024);
    if (!html_response) {
        pbuf_free(p);
        return ERR_MEM;
    }

    snprintf(html_response, 1024, response_fmt,
             gpio_get(LED_BLUE_PIN) ? "Ligada" : "Desligada",
             button_checking_enabled ? "Ativa" : "Inativa",
             button_a_state ? "pressed" : "released",
             button_a_state ? "Pressionado" : "Livre",
             button_b_state ? "pressed" : "released",
             button_b_state ? "Pressionado" : "Livre",
             temperature);

    tcp_write(tpcb, html_response, strlen(html_response), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    free(html_response);
    pbuf_free(p);

    return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

int main() {
    stdio_init_all();

    // Inicializa LEDs
    npInit(LED_MATRIX_PIN, LED_COUNT);
    npClear();
    npWrite();

    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_put(LED_BLUE_PIN, 0);

    // Botões
    gpio_init(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);
    gpio_pull_up(BUTTON_B_PIN);

    // Wi-Fi
    if (cyw43_arch_init()) {
        printf("Erro ao inicializar Wi-Fi\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Falha na conexão Wi-Fi\n");
        return 1;
    }
    printf("Conectado! IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar PCB\n");
        return 1;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, 80);
    if (err != ERR_OK) {
        printf("Erro ao bind: %d\n", err);
        return 1;
    }

    pcb = tcp_listen(pcb);
    if (!pcb) {
        printf("Erro ao listen\n");
        return 1;
    }

    tcp_accept(pcb, tcp_server_accept);

    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true) {
        cyw43_arch_poll();
        check_buttons();
        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}
