#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

#define WIFI_SSID "Mugen"
#define WIFI_SENHA "MangekyouSharingan"
#define PINO_X_JOYSTICK 26
#define PINO_Y_JOYSTICK 27
#define PINO_BOTAO_JOYSTICK 22
#define CANAL_ADC_X 0
#define CANAL_ADC_Y 1

typedef struct {
    uint16_t eixo_x;
    uint16_t eixo_y;
    bool botao_pressionado;
    const char* direcao;
} EstadoJoystick;

static EstadoJoystick estado_joystick = {2048, 2048, false, "Centro"};

const char* obter_direcao(uint16_t x, uint16_t y) {
    const uint16_t area_morta = 400;
    const uint16_t centro = 2048;

    int16_t rel_x = (int16_t)x - centro;
    int16_t rel_y = (int16_t)y - centro;

    if (abs(rel_x) < area_morta && abs(rel_y) < area_morta) return "Centro";

    float angulo = atan2f(rel_x, -rel_y) * 180.0f / 3.14159265f;
    if (angulo < 0) angulo += 360.0f;

    // Apenas trocamos os nomes das direções para corresponder ao físico
    if (angulo >= 337.5f || angulo < 22.5f) return "Oeste";
    else if (angulo >= 22.5f && angulo < 67.5f) return "Noroeste";
    else if (angulo >= 67.5f && angulo < 112.5f) return "Norte";
    else if (angulo >= 112.5f && angulo < 157.5f) return "Nordeste";
    else if (angulo >= 157.5f && angulo < 202.5f) return "Leste";
    else if (angulo >= 202.5f && angulo < 247.5f) return "Sudeste";
    else if (angulo >= 247.5f && angulo < 292.5f) return "Sul";
    else return "Sudoeste";
}

void ler_joystick() {
    adc_select_input(CANAL_ADC_X);
    sleep_us(2);
    estado_joystick.eixo_x = adc_read();

    adc_select_input(CANAL_ADC_Y);
    sleep_us(2);
    estado_joystick.eixo_y = adc_read();

    estado_joystick.botao_pressionado = !gpio_get(PINO_BOTAO_JOYSTICK);
    estado_joystick.direcao = obter_direcao(estado_joystick.eixo_x, estado_joystick.eixo_y);
}

static err_t receber_tcp(void *arg, struct tcp_pcb *pcb_tcp, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(pcb_tcp);
        tcp_recv(pcb_tcp, NULL);
        return ERR_OK;
    }

    if (strncmp(p->payload, "GET", 3) != 0) {
        pbuf_free(p);
        return ERR_OK;
    }

    ler_joystick();

    const char *formato_resposta =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='0.5'>"
        "<title>Controle de Direção de Máquina Subaquática</title></head><body>"
        "<h1>Controle de Direção de Máquina Subaquática</h1>"
        "<p>X: %d (%.1f%%)</p>"
        "<p>Y: %d (%.1f%%)</p>"
        "<p>Botão: %s</p>"
        "<p>Direção: %s</p>"
        "</body></html>";

    float percentual_x = (estado_joystick.eixo_x / 4095.0f) * 100.0f;
    float percentual_y = (estado_joystick.eixo_y / 4095.0f) * 100.0f;

    char *resposta_html = malloc(1024);
    if (!resposta_html) {
        pbuf_free(p);
        return ERR_MEM;
    }

    snprintf(resposta_html, 1024, formato_resposta,
             estado_joystick.eixo_x, percentual_x,
             estado_joystick.eixo_y, percentual_y,
             estado_joystick.botao_pressionado ? "Pressionado" : "Livre",
             estado_joystick.direcao);

    printf("%s\n", resposta_html);  // debug

    tcp_write(pcb_tcp, resposta_html, strlen(resposta_html), TCP_WRITE_FLAG_COPY);
    tcp_output(pcb_tcp);
    free(resposta_html);
    pbuf_free(p);

    return ERR_OK;
}

static err_t aceitar_conexao_tcp(void *arg, struct tcp_pcb *nova_conexao, err_t err) {
    tcp_recv(nova_conexao, receber_tcp);
    return ERR_OK;
}

int main() {
    stdio_init_all();

    adc_init();
    adc_gpio_init(PINO_X_JOYSTICK);
    adc_gpio_init(PINO_Y_JOYSTICK);

    gpio_init(PINO_BOTAO_JOYSTICK);
    gpio_set_dir(PINO_BOTAO_JOYSTICK, GPIO_IN);
    gpio_pull_up(PINO_BOTAO_JOYSTICK);

    if (cyw43_arch_init()) {
        printf("Erro ao inicializar Wi-Fi\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_SENHA, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
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
        printf("Erro ao vincular porta: %d\n", err);
        return 1;
    }

    pcb = tcp_listen(pcb);
    if (!pcb) {
        printf("Erro ao escutar conexões\n");
        return 1;
    }

    tcp_accept(pcb, aceitar_conexao_tcp);

    while (true) {
        cyw43_arch_poll();
        sleep_ms(10);
    }

    cyw43_arch_deinit();
    return 0;
}
