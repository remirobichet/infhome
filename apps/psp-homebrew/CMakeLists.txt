cmake_minimum_required(VERSION 3.11)

project(infhome)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    main.c
    dashboard.c
    network.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspdebug
    pspsdk
    # Resolve newlib glue before SDK stubs so each import table stays contiguous.
    -Wl,--start-group
    pthreadglue
    pthread
    cglue
    c
    -Wl,--end-group
    pspdisplay
    pspctrl
    pspge
    pspnet
    pspnet_apctl
    pspnet_inet
    psputility
    psppower
)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Infhome"
    VERSION 01.01
)
