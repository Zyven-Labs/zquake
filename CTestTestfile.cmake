# CMake generated Testfile for 
# Source directory: /home/rechenplan/Code/zyven/zquake
# Build directory: /home/rechenplan/Code/zyven/zquake
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[zquake_tests]=] "/home/rechenplan/Code/zyven/zquake/zquake_tests")
set_tests_properties([=[zquake_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/rechenplan/Code/zyven/zquake/CMakeLists.txt;214;add_test;/home/rechenplan/Code/zyven/zquake/CMakeLists.txt;0;")
subdirs("_deps/catch2-build")
