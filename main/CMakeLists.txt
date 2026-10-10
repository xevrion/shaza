idf_component_register(
    SRCS "app_main.cpp" "mic.cpp" "mictest.cpp"
    INCLUDE_DIRS "." "../core"
    REQUIRES roundkit waveshare__esp32_s3_touch_amoled_1_75c lvgl__lvgl esp_timer
)
