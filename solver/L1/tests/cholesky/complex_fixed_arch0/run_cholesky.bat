@echo off
call D:\FPGA\amd\Vitis_HLS\2024.2\settings64.bat
cd /d D:\FPGA\amd\hlstrack2025\solver\L1\tests\cholesky\complex_fixed_arch0
vitis_hls -f run_hls.tcl


