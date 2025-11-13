# CMake generated Testfile for 
# Source directory: /home/runner/work/kolibri-project/kolibri-project
# Build directory: /home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(kolibri_tests "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_tests")
set_tests_properties(kolibri_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;160;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(test_semantic "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/test_semantic")
set_tests_properties(test_semantic PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;165;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(test_context "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/test_context")
set_tests_properties(test_context PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;170;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(test_corpus "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/test_corpus")
set_tests_properties(test_corpus PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;175;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(test_generation "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/test_generation")
set_tests_properties(test_generation PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;180;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(test_compress "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/test_compress")
set_tests_properties(test_compress PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;185;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(ks_compiler_roundtrip "/usr/local/bin/cmake" "-Dks_compiler=/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/ks_compiler" "-P" "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/ks_compiler_roundtrip.cmake")
set_tests_properties(ks_compiler_roundtrip PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;190;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(kolibri_node_custom_key_inline "/usr/bin/python3.12" "/home/runner/work/kolibri-project/kolibri-project/tests/run_kolibri_node_hmac.py" "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_node" "inline")
set_tests_properties(kolibri_node_custom_key_inline PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;197;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(kolibri_node_custom_key_file "/usr/bin/python3.12" "/home/runner/work/kolibri-project/kolibri-project/tests/run_kolibri_node_hmac.py" "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_node" "file")
set_tests_properties(kolibri_node_custom_key_file PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;202;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(kolibri_node_usage "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_node" "--help")
set_tests_properties(kolibri_node_usage PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;209;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(kolibri_indexer_usage "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_indexer")
set_tests_properties(kolibri_indexer_usage PROPERTIES  WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;211;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(kolibri_queue_usage "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_queue")
set_tests_properties(kolibri_queue_usage PROPERTIES  WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;214;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
add_test(kolibri_sim_usage "/home/runner/work/kolibri-project/kolibri-project/_codeql_build_dir/kolibri_sim")
set_tests_properties(kolibri_sim_usage PROPERTIES  WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;217;add_test;/home/runner/work/kolibri-project/kolibri-project/CMakeLists.txt;0;")
