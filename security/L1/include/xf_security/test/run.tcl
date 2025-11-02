open_project -reset proj_sha256
add_files "../sha224_256.hpp"
add_files "test.cpp"
set_top sha256
open_solution "solution1" -flow_target vitis
set_part {xc7z020clg484-1}
create_clock -period 10 -name default
csim_design
exit
