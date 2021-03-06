find_package(HDF5 COMPONENTS CXX QUIET)

if(NOT HDF5_FOUND)
  message(STATUS "Build record-minmax: FAILED (missing HDF5)")
  return()
endif(NOT HDF5_FOUND)

nnas_find_package(Boost QUIET COMPONENTS program_options)
if(NOT Boost_FOUND)
  message(STATUS "Build record-minmax: FAILED (missing Boost::program_options)")
  return()
endif(NOT Boost_FOUND)

set(DRIVER "driver/Driver.cpp")

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(record-minmax ${DRIVER} ${SOURCES})
target_include_directories(record-minmax PRIVATE include)
target_include_directories(record-minmax PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(record-minmax PRIVATE ${HDF5_INCLUDE_DIRS})

target_link_libraries(record-minmax ${HDF5_CXX_LIBRARIES})
target_link_libraries(record-minmax ${Boost_LIBRARIES})
target_link_libraries(record-minmax safemain)
target_link_libraries(record-minmax luci_import)
target_link_libraries(record-minmax luci_export)
target_link_libraries(record-minmax luci_interpreter)

install(TARGETS record-minmax DESTINATION bin)
