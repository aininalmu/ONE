file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE TESTS "src/*.test.cpp")
list(REMOVE_ITEM SOURCES ${TESTS})

add_library(mir2loco STATIC ${SOURCES})
target_include_directories(mir2loco PRIVATE src)
target_include_directories(mir2loco PUBLIC include)
target_link_libraries(mir2loco PUBLIC mir)
target_link_libraries(mir2loco PUBLIC loco)

nnas_find_package(GTest QUIET)

if(NOT GTest_FOUND)
  return()
endif(NOT GTest_FOUND)

GTest_AddTest(mir2loco_test ${TESTS})
target_link_libraries(mir2loco_test mir2loco)
