################################################################################
# SociConfig.cmake - CMake build configuration of SOCI library
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################

include(CheckCXXSymbolExists)

if(WIN32)
  check_cxx_symbol_exists("_M_AMD64" "" SOCI_TARGET_ARCH_X64)
  if(NOT RTC_ARCH_X64)
    check_cxx_symbol_exists("_M_IX86" "" SOCI_TARGET_ARCH_X86)
  endif(NOT RTC_ARCH_X64)
  # add check for arm here
  # see http://msdn.microsoft.com/en-us/library/b0084kay.aspx
else(WIN32)
  check_cxx_symbol_exists("__i386__" "" SOCI_TARGET_ARCH_X86)
  check_cxx_symbol_exists("__x86_64__" "" SOCI_TARGET_ARCH_X64)
  check_cxx_symbol_exists("__arm__" "" SOCI_TARGET_ARCH_ARM)
endif(WIN32)

if(NOT DEFINED LIB_SUFFIX)
  if(SOCI_TARGET_ARCH_X64)
    set(_lib_suffix "64")
  else()
    set(_lib_suffix "")
  endif()
  set(LIB_SUFFIX ${_lib_suffix} CACHE STRING "Specifies suffix for the lib directory")
endif()

#
# Force compilation flags and set desired warnings level
#

# This is used to set the -Werror compilation flag only when explicitly
# requested, as e.g. in CI builds.
set(SOCI_WERROR_OPTION "")

if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_CRT_NONSTDC_NO_WARNING)
  add_definitions(-D_SCL_SECURE_NO_WARNINGS)

  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /we4266")
  endif()

  if (SOCI_ENABLE_WERROR)
     set(SOCI_WERROR_OPTION "/WX")
  endif (SOCI_ENABLE_WERROR)
else()

  if (SOCI_ENABLE_WERROR)
     set(SOCI_WERROR_OPTION "-Werror")
  endif (SOCI_ENABLE_WERROR)

  set(SOCI_GCC_CLANG_COMMON_FLAGS
    "-pedantic -Wno-error=parentheses -Wall -Wextra -Wpointer-arith -Wcast-align -Wcast-qual -Wfloat-equal -Woverloaded-virtual -Wredundant-decls -Wno-long-long")

  if(SOCI_UBSAN)
    set(SOCI_GCC_CLANG_COMMON_FLAGS "${SOCI_GCC_CLANG_COMMON_FLAGS} -fsanitize=undefined")
  endif()


  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR "${CMAKE_CXX_COMPILER}" MATCHES "clang")

    if(NOT CMAKE_CXX_COMPILER_VERSION LESS 3.1 AND SOCI_ASAN)
      set(SOCI_GCC_CLANG_COMMON_FLAGS "${SOCI_GCC_CLANG_COMMON_FLAGS} -fsanitize=address")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SOCI_GCC_CLANG_COMMON_FLAGS}")

  elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)

    if(NOT CMAKE_CXX_COMPILER_VERSION LESS 4.8 AND SOCI_ASAN)
      set(SOCI_GCC_CLANG_COMMON_FLAGS "${SOCI_GCC_CLANG_COMMON_FLAGS} -fsanitize=address")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SOCI_GCC_CLANG_COMMON_FLAGS} ")
    if (CMAKE_COMPILER_IS_GNUCXX)
        if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-variadic-macros")
        endif()
    endif()

  else()
	message(WARNING "Unknown toolset - using default flags to build SOCI")
  endif()

endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SOCI_WERROR_OPTION}")
