# CMAKE_CURRENT_LIST_DIR, not CMAKE_CURRENT_SOURCE_DIR: ESP-IDF evaluates this
# file twice, and during its early requirements scan the source dir is the
# scratch directory of a generated script rather than this component.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/www/index.html.gz")
    message(FATAL_ERROR "Missing firmware/main/www/index.html.gz - run 'node web/build.js' from the repository root first. The firmware embeds the same page the simulator publishes, so it is generated rather than committed.")
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
