cmake_minimum_required(VERSION 3.11)

project(infhome)

add_executable(${PROJECT_NAME}
    main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspnet
    pspnet_apctl
    pspnet_inet
    psputility
)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Infhome"
    VERSION 01.00
)
