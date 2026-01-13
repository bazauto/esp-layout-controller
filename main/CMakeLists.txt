idf_component_register(
    SRCS 
        # Hardware/Platform layer (C - ESP-IDF specific)
        "waveshare_rgb_lcd_port.c"
        "lvgl_port.c"
        
        # Application entry (C)
        "main.c"
        
        # Model layer (C++)
        "model/Locomotive.cpp"
        "model/Throttle.cpp"
        "model/Roster.cpp"
        
        # Communication layer (C++)
        "communication/WiFiManager.cpp"
        
        # UI layer (C++)
        "ui/ThrottleMeter.cpp"
        "ui/WiFiConfigScreen.cpp"
        "ui/wifi_config_wrapper.cpp"
        "ui/MainScreen.cpp"
        "ui/main_screen_wrapper.cpp"
        
    INCLUDE_DIRS 
        "."
        "model"
        "hardware"
        "communication"
        "controller"
        "ui"
        "utils"
)

# Enable C++17
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)

