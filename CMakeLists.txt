cmake_minimum_required(VERSION 3.10)

include_directories(${USB_INC})
link_directories(${USB_LIB})
include_directories(${UVC_INC})
link_directories(${UVC_LIB})

add_library(uvcctl SHARED uvc_control.c)
target_link_libraries(uvcctl usb1.0 uvc)

install(TARGETS uvcctl
        LIBRARY DESTINATION ${ANDROID_ABI}
)

