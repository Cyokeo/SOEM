# This software is dual-licensed under GPLv3 and a commercial
# license. See the file LICENSE.md distributed with this software for
# full license information.

target_sources(soem PRIVATE
  contrib/osal/macosx/osal.c
  contrib/osal/macosx/osal_defs.h
  contrib/oshw/macosx/oshw.c
  contrib/oshw/macosx/oshw.h
  contrib/oshw/macosx/nicdrv.c
  contrib/oshw/macosx/nicdrv.h
)

target_include_directories(soem PUBLIC
  $<BUILD_INTERFACE:${SOEM_SOURCE_DIR}/contrib/osal/macosx>
  $<BUILD_INTERFACE:${SOEM_SOURCE_DIR}/contrib/oshw/macosx>
  $<INSTALL_INTERFACE:include/soem>
)

foreach(target IN ITEMS
    soem
    ec_sample
    eepromtool
    eni_test
    eoe_test
    firm_update
    simple_ng
    slaveinfo)
  if (TARGET ${target})
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
    )
  endif()
endforeach()

target_link_libraries(soem PUBLIC pthread pcap)

install(FILES
  contrib/osal/macosx/osal_defs.h
  contrib/oshw/macosx/nicdrv.h
  DESTINATION include/soem
)
