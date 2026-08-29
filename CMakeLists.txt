cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBTIFF_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/reframe_tiff.c
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_tiff.c
)

SET(LIBTIFF_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libtiff.a
)

SET(LIBTIFF_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libtiff
        "${LIBTIFF_SRC}"
        "${LIBTIFF_LIB}"
        []
        ""
        "${LIBTIFF_INC}"
        "-s USE_ZLIB=1"
        "1")
