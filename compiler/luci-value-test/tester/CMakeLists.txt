
set(SRCS_EVAL_TESTER
      src/EvalTester.cpp
   )

add_executable(luci_eval_tester ${SRCS_EVAL_TESTER})
target_link_libraries(luci_eval_tester PRIVATE luci_import)
target_link_libraries(luci_eval_tester PRIVATE luci_lang)
target_link_libraries(luci_eval_tester PRIVATE luci_interpreter)
target_link_libraries(luci_eval_tester PRIVATE safemain)
