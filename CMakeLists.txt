cmake_minimum_required(VERSION 3.10)

include_directories(${USB_INC})
link_directories(${USB_LIB})
include_directories(${UVC_INC})
link_directories(${UVC_LIB})

include_directories(${OPENCV}/modules/core/include)
include_directories(${OPENCV}/modules/imgproc/include)
include_directories(${OPENCV}/build_android_${ANDROID_ABI})
#link_directories(${OPENCV}/sdk/native/staticlibs/${ANDROID_ABI})
link_directories(${OPENCV}/build_android_${ANDROID_ABI}/lib/${ANDROID_ABI}/)
set(CMAKE_SHARED_LINKER_FLAGS "-static-libstdc++")

add_library(uvcctl SHARED 
    uvc_control.c
    )
target_link_libraries(uvcctl usb1.0 uvc)

add_library(stack SHARED stack.cpp)
target_link_libraries(stack opencv_core opencv_imgproc log)

install(TARGETS uvcctl stack
        LIBRARY DESTINATION ${ANDROID_ABI}
)

