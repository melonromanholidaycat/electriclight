#pragma once
#include "esp_err.h"
typedef int esp_netif_t;
typedef struct { unsigned addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
#define IPSTR "%u"
#define IP2STR(ip) (ip)->addr
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
void esp_netif_destroy_default_wifi(esp_netif_t *s);
