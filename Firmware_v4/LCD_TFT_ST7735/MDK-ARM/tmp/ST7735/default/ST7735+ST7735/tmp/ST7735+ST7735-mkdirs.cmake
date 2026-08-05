# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735")
  file(MAKE_DIRECTORY "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735")
endif()
file(MAKE_DIRECTORY
  "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/1"
  "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735"
  "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735/tmp"
  "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735/src/ST7735+ST7735-stamp"
  "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735/src"
  "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735/src/ST7735+ST7735-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735/src/ST7735+ST7735-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/My_project/buck_boost_converter/Firmware_v4/LCD_TFT_ST7735/MDK-ARM/tmp/ST7735/default/ST7735+ST7735/src/ST7735+ST7735-stamp${cfgdir}") # cfgdir has leading slash
endif()
