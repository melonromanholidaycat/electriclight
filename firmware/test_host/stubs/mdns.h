#pragma once
#include "esp_err.h"
esp_err_t mdns_init(void);
void mdns_hostname_set(const char *s);
void mdns_instance_name_set(const char *s);
void mdns_service_add(const char *n,const char *s,const char *p,int port,void *t,int count);
