/*
 * Copyright 2019 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file adler32.hpp
 * @brief header file for adler32.
 * This file part of Vitis Security Library.
 *
 */

#ifndef _XF_SECURITY_ADLER32_HPP_
#define _XF_SECURITY_ADLER32_HPP_

#include <ap_int.h>
#include <hls_stream.h>
#include <hls_math.h>
#if !defined(__SYNTHESIS__)
#include <iostream>
#endif

namespace xf {
namespace security {
namespace internal {

// 最大的小于65536的质数
const ap_uint<32> BASE_0 = 65521;
// 预计算的BASE倍数，用于快速减法
const ap_uint<32> BASE_2X = 2 * BASE_0;
const ap_uint<32> BASE_3X = 3 * BASE_0;
const ap_uint<32> BASE_4X = 4 * BASE_0;

// 优化的树状加法器 - 8倍并行展开
#pragma HLS inline
static ap_uint<16> inline reduceSum(ap_uint<8> bytes[16], int count) {
    ap_uint<16> sum = 0;
    // 完全展开循环以最大化并行性
#pragma HLS unroll factor=16
    for (int i = 0; i < count; i++) {
        sum += bytes[i];
    }
    return sum;
}

// 优化的模运算 - 使用查找表和快速减法
#pragma HLS inline
static ap_uint<16> inline modAdler(ap_uint<32> val) {
    // 快速路径：大多数情况下的简单减法
    if (val < BASE_4X) {
        if (val >= BASE_3X) val -= BASE_3X;
        else if (val >= BASE_2X) val -= BASE_2X;
        else if (val >= BASE_0) val -= BASE_0;
    } else {
        // 多次减法以处理较大的值
        while (val >= BASE_0) {
            val -= BASE_0;
        }
    }
    return val;
}

// 批量计算s1和s2的优化函数 - 支持16字节并行处理
#pragma HLS inline
static void processBatch(ap_uint<8> bytes[16], int W, ap_uint<32>& s1, ap_uint<32>& s2) {
    // 并行计算s1和s2的中间值
    ap_uint<32> sTmp0 = 0;
    ap_uint<32> sTmp1 = 0;
    
    // 完全展开循环以最大化并行性
#pragma HLS unroll factor=16
    for (int i = 0; i < W; i++) {
        sTmp0 += bytes[i];
        sTmp1 += bytes[i] * (W - i);
    }
    
    // 计算最终更新值
    ap_uint<32> sTmp2 = s1 * W + sTmp1;
    
    // 优化的模运算更新
    s1 = modAdler(s1 + sTmp0);
    s2 = modAdler(s2 + sTmp2);
}

} // end of namespace internal

/**
 * @brief adler32 computes the Adler-32 checksum of an input data.
 * @tparam W byte number of input data, the value of W includes 1, 2, 4, 8, 16.
 * @param adlerStrm initialize adler32 value
 * @param inStrm messages to be checked
 * @param inLenStrm length of messages to be checked.
 * @param endInLenStrm end flag of inLenStrm
 * @param outStrm checksum result
 * @param end flag of outStrm
 */
template <int W>
void adler32(hls::stream<ap_uint<32> >& adlerStrm,
             hls::stream<ap_uint<W * 8> >& inStrm,
             hls::stream<ap_uint<32> >& inLenStrm,
             hls::stream<bool>& endInLenStrm,
             hls::stream<ap_uint<32> >& outStrm,
             hls::stream<bool>& endOutStrm) {
    // 使用DATAFLOW指令优化数据流
#pragma HLS DATAFLOW
    
    bool e = endInLenStrm.read();
    while (!e) {
        ap_uint<32> adler = adlerStrm.read();
        ap_uint<32> len = inLenStrm.read();
        e = endInLenStrm.read();

        ap_uint<32> s1 = adler & 0xffff;
        ap_uint<32> s2 = ((adler >> 16) & 0xffff);
        ap_uint<W * 8> inData;
        
        // 定义字节数组并完全分区
        ap_uint<8> bytes[16];
#pragma HLS array_partition variable=bytes dim=1 complete
        
        // 处理完整的W字节块 - 8倍循环展开
        process_full_blocks:
        for (ap_uint<32> i = 0; i < len / W; i++) {
#pragma HLS PIPELINE II = 1
#pragma HLS loop_tripcount max = 100 min = 100
            
            inData = inStrm.read();
            
            // 16倍并行提取字节
#pragma HLS unroll factor=16
            for (int j = 0; j < W; j++) {
                bytes[j] = inData(j * 8 + 7, j * 8);
            }
            
            // 使用优化的批量处理函数
            internal::processBatch(bytes, W, s1, s2);
        }

        // 处理剩余字节 - 单字节处理流水线
        process_remainder:
        for (int j = 0; j < len % W; j++) {
#pragma HLS PIPELINE II = 1
#pragma HLS loop_tripcount max = W min = W
            if (j == 0) inData = inStrm.read();
            ap_uint<8> byte_val = inData(j * 8 + 7, j * 8);
            
            // 直接更新s1和s2，使用优化的模运算
            s1 = internal::modAdler(s1 + byte_val);
            s2 = internal::modAdler(s2 + s1);
        }

        // 输出结果
        ap_uint<32> res = (s2 << 16) + s1;
        outStrm.write(res);
        endOutStrm.write(false);
    }
    endOutStrm.write(true);
}

/**
 * @brief adler32 computes the Adler-32 checksum of an input data.
 * @tparam W byte number of input data, the value of W includes 1, 2, 4, 8, 16.
 * @param adlerStrm initialize adler32 value
 * @param inStrm messages to be checked
 * @param inPackLenStrm effective length of each pack from inStrm. inPackLen.range(4,0) = effective len of pack,
 * inPackLen.range(6,5) = 0x1 means end of one message, inPackLen.range(6,5) = 0x2 means end of all message.
 * messages.
 * @param outStrm checksum result
 * @param end flag of outStrm
 */
template <int W>
void adler32(hls::stream<ap_uint<32> >& adlerStrm,
             hls::stream<ap_uint<W * 8> >& inStrm,
             hls::stream<ap_uint<7> >& inPackLenStrm,
             hls::stream<ap_uint<32> >& outStrm,
             hls::stream<bool>& endOutStrm) {
    // 使用DATAFLOW指令优化数据流
#pragma HLS DATAFLOW
    
    // 预定义字节数组并完全分区
    ap_uint<8> bytes[16];
#pragma HLS array_partition variable=bytes dim=1 complete
    
    ap_uint<7> inPackLen = inPackLenStrm.read();
    while (inPackLen[6] != 1) {
        ap_uint<32> adler = adlerStrm.read();

        ap_uint<32> s1 = adler & 0xffff;
        ap_uint<32> s2 = ((adler >> 16) & 0xffff);
        ap_uint<W * 8> inData;

        // 处理完整的数据包 - 流水线处理
        process_full_packets:
        while (inPackLen[5] == 0) {
#pragma HLS PIPELINE II = 1
#pragma HLS loop_tripcount max = 100 min = 100
            inData = inStrm.read();
            
            // 16倍并行提取字节
#pragma HLS unroll factor=16
            for (int i = 0; i < W; i++) {
                bytes[i] = inData(i * 8 + 7, i * 8);
            }
            
            // 使用优化的批量处理函数
            internal::processBatch(bytes, W, s1, s2);

            inPackLen = inPackLenStrm.read();
        }

        // 处理剩余字节 - 优化的模运算
        process_packet_remainder:
        int remainder_len = inPackLen.range(4, 0);
        for (int j = 0; j < remainder_len; j++) {
#pragma HLS PIPELINE II = 1
#pragma HLS loop_tripcount max = W min = W
            if (j == 0) inData = inStrm.read();
            ap_uint<8> byte_val = inData(j * 8 + 7, j * 8);
            
            // 使用优化的模运算函数
            s1 = internal::modAdler(s1 + byte_val);
            s2 = internal::modAdler(s2 + s1);
        }
        
        inPackLen = inPackLenStrm.read();

        // 输出结果
        ap_uint<32> res = (s2 << 16) + s1;
        outStrm.write(res);
        endOutStrm.write(false);
    }
    endOutStrm.write(true);
}

/**
 * @brief adler32 computes the Adler-32 checksum of an input data.
 * @tparam W byte number of input data, the value of W includes 1, 2, 4, 8, 16.
 * @param adlerStrm initialize adler32 value
 * @param inStrm messages to be checked
 * @param inPackLenStrm effective length of each pack from inStrm, 1~W. 0 means end of message
 * @param endInPackLenStrm end flag of inPackLenStrm, 1 "false" for 1 message, 1 "true" means no message anymore.
 * @param outStrm checksum result
 * @param end flag of outStrm
 */
template <int W>
void adler32(hls::stream<ap_uint<32> >& adlerStrm,
             hls::stream<ap_uint<W * 8> >& inStrm,
             hls::stream<ap_uint<5> >& inPackLenStrm,
             hls::stream<bool>& endInPackLenStrm,
             hls::stream<ap_uint<32> >& outStrm,
             hls::stream<bool>& endOutStrm) {
    // 应用DATAFLOW指令优化数据流
#pragma HLS DATAFLOW
    
    // 预定义字节数组并完全分区
    ap_uint<8> bytes[16];
#pragma HLS array_partition variable=bytes dim=1 complete
    
    bool e = endInPackLenStrm.read();
    while (!e) {
        ap_uint<32> adler = adlerStrm.read();
        ap_uint<5> inPackLen = inPackLenStrm.read();

        ap_uint<32> s1 = adler & 0xffff;
        ap_uint<32> s2 = ((adler >> 16) & 0xffff);
        ap_uint<W * 8> inData;

        // 处理完整宽度的数据包 - 8倍循环展开
        process_full_width_packets:
        while (inPackLen == W) {
#pragma HLS PIPELINE II = 1
#pragma HLS loop_tripcount max = 100 min = 100
            inPackLen = inPackLenStrm.read();
            inData = inStrm.read();
            
            // 16倍并行提取字节
#pragma HLS unroll factor=16
            for (int i = 0; i < W; i++) {
                bytes[i] = inData(i * 8 + 7, i * 8);
            }
            
            // 使用优化的批量处理函数
            internal::processBatch(bytes, W, s1, s2);
        }

        // 处理非完整宽度的数据包
        if (inPackLen != 0) {
            process_partial_packets:
            for (int j = 0; j < inPackLen; j++) {
#pragma HLS PIPELINE II = 1
#pragma HLS loop_tripcount max = W min = W
                if (j == 0) inData = inStrm.read();
                ap_uint<8> byte_val = inData(j * 8 + 7, j * 8);
                
                // 使用优化的模运算函数
                s1 = internal::modAdler(s1 + byte_val);
                s2 = internal::modAdler(s2 + s1);
            }
            inPackLen = inPackLenStrm.read();
        }

        // 输出结果
        ap_uint<32> res = (s2 << 16) + s1;
        outStrm.write(res);
        endOutStrm.write(false);
        e = endInPackLenStrm.read();
    }
    endOutStrm.write(true);
}

} // end of namespace security
} // end of namespace xf
#endif // _XF_SECURITY_ADLER32_HPP_
