cmake_minimum_required(VERSION 3.5)
project(imagedecoder.svg)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(LunaSVG REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", matches the same approach
                                            # used by other out-of-tree Kodi imagedecoder addons
                    ${LUNASVG_INCLUDE_DIRS})

set(SVG_SOURCES src/SvgPicture.cpp)
set(SVG_HEADERS src/SvgPicture.h)

set(DEPLIBS ${LUNASVG_LIBRARIES})

build_addon(imagedecoder.svg SVG DEPLIBS)

include(CPack)
