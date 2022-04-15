link_libraries(greenrooms_s ${CUNIT_LIBRARIES})
include_directories(${CUNIT_INCLUDE_DIRS})

set(TEST_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(TESTS
            rct_test1
            rct_test_consumer
            rct_test_transport_webrtc
            rct_test_transport_direct
            rct_test_transport_plain
            rct_test_consumer_data
            rct_test_transport_pipe
            )

file(
  COPY .
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
  FILES_MATCHING
  PATTERN "*.ini"
  PATTERN "*.key"
  PATTERN "*.pem"
)

add_executable(probe probe.c)

foreach(TN IN ITEMS ${TESTS})
  add_executable(${TN} ${TN}.c)
  set_target_properties(${TN} PROPERTIES COMPILE_FLAGS "-DIW_STATIC")
  add_test(
    NAME ${TN}
    WORKING_DIRECTORY ${TEST_DATA_DIR}
    COMMAND ${TEST_TOOL_CMD} $<TARGET_FILE:${TN}>)
endforeach()
