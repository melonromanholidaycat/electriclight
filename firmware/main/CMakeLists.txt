set(WWW "${CMAKE_CURRENT_SOURCE_DIR}/www/index.html.gz")
if(NOT EXISTS "${WWW}")
    message(FATAL_ERROR
        "Missing main/www/index.html.gz.\n"
        "Run `node web/build.js` from the repository root first: the firmware "
        "embeds the same page the simulator publishes, and it is generated "
        "rather than committed.")
endif()

idf_component_register(
    SRCS
        "main.c"
        "app_config.c"
        "app_log.c"
        "app_wifi.c"
        "app_http.c"
        "app_ota.c"
    INCLUDE_DIRS "."
    EMBED_FILES "www/index.html.gz"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_http_server
        esp_netif
        esp_event
        app_update
        esp_app_format
        esp_timer
        esp_system
        json
)
