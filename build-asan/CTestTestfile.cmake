# CMake generated Testfile for 
# Source directory: /Users/kolibri/Documents/os
# Build directory: /Users/kolibri/Documents/os/build-asan
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(kolibri_tests "/Users/kolibri/Documents/os/build-asan/kolibri_tests")
set_tests_properties(kolibri_tests PROPERTIES  _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;153;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(ks_compiler_roundtrip "/opt/homebrew/bin/cmake" "-Dks_compiler=/Users/kolibri/Documents/os/build-asan/ks_compiler" "-P" "/Users/kolibri/Documents/os/build-asan/ks_compiler_roundtrip.cmake")
set_tests_properties(ks_compiler_roundtrip PROPERTIES  _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;158;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(kolibri_node_custom_key_inline "/opt/homebrew/Frameworks/Python.framework/Versions/3.13/bin/python3.13" "/Users/kolibri/Documents/os/tests/run_kolibri_node_hmac.py" "/Users/kolibri/Documents/os/build-asan/kolibri_node" "inline")
set_tests_properties(kolibri_node_custom_key_inline PROPERTIES  _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;165;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(kolibri_node_custom_key_file "/opt/homebrew/Frameworks/Python.framework/Versions/3.13/bin/python3.13" "/Users/kolibri/Documents/os/tests/run_kolibri_node_hmac.py" "/Users/kolibri/Documents/os/build-asan/kolibri_node" "file")
set_tests_properties(kolibri_node_custom_key_file PROPERTIES  _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;170;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(kolibri_node_usage "/Users/kolibri/Documents/os/build-asan/kolibri_node" "--help")
set_tests_properties(kolibri_node_usage PROPERTIES  _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;177;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(kolibri_indexer_usage "/Users/kolibri/Documents/os/build-asan/kolibri_indexer")
set_tests_properties(kolibri_indexer_usage PROPERTIES  WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;179;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(kolibri_queue_usage "/Users/kolibri/Documents/os/build-asan/kolibri_queue")
set_tests_properties(kolibri_queue_usage PROPERTIES  WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;182;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
add_test(kolibri_sim_usage "/Users/kolibri/Documents/os/build-asan/kolibri_sim")
set_tests_properties(kolibri_sim_usage PROPERTIES  WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/Users/kolibri/Documents/os/CMakeLists.txt;185;add_test;/Users/kolibri/Documents/os/CMakeLists.txt;0;")
