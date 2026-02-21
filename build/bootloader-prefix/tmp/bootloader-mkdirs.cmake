# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/llatva/git/d26badge-freertos-fw/esp-idf/components/bootloader/subproject"
  "/home/llatva/git/d26badge-freertos-fw/build/bootloader"
  "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix"
  "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix/tmp"
  "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix/src/bootloader-stamp"
  "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix/src"
  "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/llatva/git/d26badge-freertos-fw/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
