idf_component_register(
    SRCS
        "src/bambu_status.c"
        "src/bambu_status_stream.c"
        "src/bambu_service.c"
        "src/bambu_studio_api.c"
        "src/bambu_model.c"
        "src/bambu_discovery.c"
        "src/bambu_camera.c"
        "src/bambu_ftp.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt nvs_flash esp_event cjson mbedtls esp_netif esp_timer esp-tls lwip)
