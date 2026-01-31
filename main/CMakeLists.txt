set(APP_SRCS
    # Hardware/Platform layer (C - ESP-IDF specific)
    "waveshare_rgb_lcd_port.c"
    "lvgl_port.c"
    
    # Application entry (C)
    "main.c"
    
    # Model layer (C++)
    "model/Locomotive.cpp"
    "model/Throttle.cpp"
    "model/Roster.cpp"
    "model/Knob.cpp"

    # Hardware layer (C++)
    "hardware/RotaryEncoderHal.cpp"
    
    # Controller layer (C++)
    "controller/ThrottleController.cpp"
    "controller/AppController.cpp"
    "controller/WiFiController.cpp"
    "controller/JmriConnectionController.cpp"
    
    # Communication layer (C++)
    "communication/WiFiManager.cpp"
    "communication/WiThrottleClient.cpp"
    "communication/JmriJsonClient.cpp"
    
    # UI layer (C++)
    "ui/components/ThrottleMeter.cpp"
    "ui/components/VirtualEncoderPanel.cpp"
    "ui/components/RosterCarousel.cpp"
    "ui/components/PowerStatusBar.cpp"
    "ui/components/FunctionPanel.cpp"
    "ui/WiFiConfigScreen.cpp"
    "ui/wrappers/wifi_config_wrapper.cpp"
    "ui/JmriConfigScreen.cpp"
    "ui/wrappers/jmri_config_wrapper.cpp"
    "ui/MainScreen.cpp"
    "ui/wrappers/main_screen_wrapper.cpp"
)

set(REQUIRES_LIST)

if(CONFIG_THROTTLE_TESTS)
    list(APPEND APP_SRCS
        "tests/ThrottleControllerTests.cpp"
        "tests/LocomotiveTests.cpp"
        "tests/ModelRosterTests.cpp"
        "tests/ProtocolParsingTests.cpp"
        "tests/ThrottleModelTests.cpp"
        "tests/TestRunner.cpp"
    )
    list(APPEND REQUIRES_LIST unity)
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS 
        "."
        "model"
        "hardware"
        "communication"
        "controller"
        "ui"
        "utils"
    REQUIRES ${REQUIRES_LIST}
)

# Enable C++17
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)

