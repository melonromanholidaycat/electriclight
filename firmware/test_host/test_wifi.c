#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_wifi.h"
#include "app_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
const char *esp_err_to_name(esp_err_t e) { (void)e;return "test error"; }
static unsigned bits;
static int checks, connects, driver_mode, scenario;
static int64_t clock_us;
static bool fail_join;
static void (*event_fn)(void*,esp_event_base_t,int32_t,void*);
static void (*watch_fn)(void*);
static app_config_t config;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
void test_log(const char *t,const char *f,...) {(void)t;(void)f;}
size_t strlcpy(char *d,const char *s,size_t n) {size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
EventGroupHandle_t xEventGroupCreate(void){bits=0;return (void*)1;}
EventBits_t xEventGroupSetBits(EventGroupHandle_t e,EventBits_t b){(void)e;return bits|=b;}
EventBits_t xEventGroupClearBits(EventGroupHandle_t e,EventBits_t b){(void)e;unsigned old=bits;bits&=~b;return old;}
EventBits_t xEventGroupGetBits(EventGroupHandle_t e){(void)e;return bits;}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t e,EventBits_t b,int c,int a,TickType_t t) {
    (void)e;(void)b;(void)c;(void)a;
    if (t==20000) {
        if(fail_join){for(int i=0;i<6;i++)event_fn(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);}
        else {ip_event_got_ip_t ip={.ip_info.ip.addr=123};event_fn(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);}
    } else {
        clock_us+=(int64_t)t*1000;
        if(scenario==1 && clock_us==2000000) {
            ip_event_got_ip_t ip={.ip_info.ip.addr=456};event_fn(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);
        }
        if(scenario==1 && clock_us==3000000) {
            CHECK(app_wifi_has_ip());
            for(int i=0;i<6;i++)event_fn(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
        }
    }
    return bits;
}
int64_t esp_timer_get_time(void){return clock_us;}
void vTaskDelete(void *t){(void)t;}
int xTaskCreate(void(*f)(void*),const char*n,unsigned s,void*a,int p,void*h){(void)n;(void)s;(void)a;(void)p;(void)h;watch_fn=f;return pdPASS;}
const app_config_t *app_config_get(void){return &config;}
esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int32_t i,void(*f)(void*,esp_event_base_t,int32_t,void*),void*a,void*h){(void)b;(void)i;(void)a;(void)h;event_fn=f;return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){return (void*)1;}
esp_netif_t *esp_netif_create_default_wifi_ap(void){return (void*)2;}
void esp_netif_destroy_default_wifi(esp_netif_t *s){(void)s;}
esp_err_t esp_wifi_init(const wifi_init_config_t*c){(void)c;return ESP_OK;}
esp_err_t esp_wifi_set_mode(int m){driver_mode=m;return ESP_OK;}
esp_err_t esp_wifi_set_config(int i,const wifi_config_t*c){(void)i;(void)c;return ESP_OK;}
esp_err_t esp_wifi_start(void){if(driver_mode==WIFI_MODE_STA)event_fn(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);return ESP_OK;}
esp_err_t esp_wifi_stop(void){int before=connects;event_fn(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);CHECK(before==connects);return ESP_OK;}
esp_err_t esp_wifi_connect(void){connects++;return ESP_OK;}
esp_err_t mdns_init(void){return ESP_OK;}
void mdns_hostname_set(const char*s){(void)s;}
void mdns_instance_name_set(const char*s){(void)s;}
void mdns_service_add(const char*n,const char*s,const char*p,int port,void*t,int count){(void)n;(void)s;(void)p;(void)port;(void)t;(void)count;}
int main(void) {
    strcpy(config.sta_ssid,"test");
    CHECK(app_wifi_start(EL_RADIO_ON)==ESP_OK);CHECK(app_wifi_has_ip());CHECK(watch_fn!=NULL);
    for(int i=0;i<6;i++)event_fn(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    CHECK(!app_wifi_has_ip());watch_fn(NULL);
    CHECK(app_wifi_mode()==APP_WIFI_FALLBACK && app_wifi_has_ip());
    // A DHCP/IP loss without a disconnect event also has a deadline.
    clock_us=0;CHECK(app_wifi_start(EL_RADIO_ON)==ESP_OK);
    event_fn(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);CHECK(!app_wifi_has_ip());watch_fn(NULL);
    CHECK(app_wifi_mode()==APP_WIFI_FALLBACK);CHECK(clock_us>=20000000 && clock_us<=22000000);
    // Successful reconnect clears old failure state; a later loss still recovers.
    clock_us=0;scenario=1;CHECK(app_wifi_start(EL_RADIO_ON)==ESP_OK);
    event_fn(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);watch_fn(NULL);
    CHECK(clock_us>=3000000);CHECK(app_wifi_mode()==APP_WIFI_FALLBACK);
    fail_join=true;CHECK(app_wifi_start(EL_RADIO_ON)==ESP_OK);CHECK(app_wifi_mode()==APP_WIFI_FALLBACK);
    int before=connects;CHECK(app_wifi_start(EL_RADIO_SAFE)==ESP_OK);CHECK(connects==before);
    printf("%d WiFi lifecycle checks passed\n",checks);
}
