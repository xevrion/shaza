idf_component_register(
    SRCS "app_main.cpp" "mic.cpp" "mictest.cpp" "wifi.cpp" "timesync.cpp" "listener.cpp" "recognizer.cpp"
         "../core/paint.cpp" "../core/text.cpp" "../core/reply.cpp" "../core/app.cpp" "../core/look.cpp"
         "../core/signature.cpp" "../core/json.cpp" "../core/shazam.cpp" "../core/song.cpp"
    INCLUDE_DIRS "." "../core"
    REQUIRES roundkit waveshare__esp32_s3_touch_amoled_1_75c lvgl__lvgl espressif__esp_new_jpeg
             esp_wifi wpa_supplicant esp_netif esp_event esp_http_client esp-tls mbedtls nvs_flash esp_timer lwip
)

# Without this every sqrtf is a library call that checks for errors; the drawing code takes one
# per pixel.
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-math-errno)

# idf.py build -DSHAZA_AUTOTAP=1 taps the screen by itself 20 s after each result, to measure frame
# times through every phase without anyone there.
if(SHAZA_AUTOTAP)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SHAZA_AUTOTAP=1)
endif()

# idf.py build -DSHAZA_MICTEST=1 builds the microphone test instead of the app.
if(SHAZA_MICTEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SHAZA_MICTEST=1)
endif()
