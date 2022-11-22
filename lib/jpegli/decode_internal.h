// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JPEGLI_DECODE_INTERNAL_H_
#define LIB_JPEGLI_DECODE_INTERNAL_H_

#include <stdint.h>

#include <array>
#include <set>
#include <vector>

#include "hwy/aligned_allocator.h"
#include "lib/jpegli/huffman.h"

namespace jpegli {

template <typename T1, typename T2>
constexpr inline T1 DivCeil(T1 a, T2 b) {
  return (a + b - 1) / b;
}

constexpr int kMaxComponents = 4;
constexpr int kJpegDCAlphabetSize = 12;

typedef int16_t coeff_t;

// Represents one component of a jpeg file.
struct JPEGComponent {
  JPEGComponent()
      : id(0),
        h_samp_factor(1),
        v_samp_factor(1),
        quant_idx(0),
        width_in_blocks(0),
        height_in_blocks(0) {}

  // One-byte id of the component.
  uint32_t id;
  // Horizontal and vertical sampling factors.
  // In interleaved mode, each minimal coded unit (MCU) has
  // h_samp_factor x v_samp_factor DCT blocks from this component.
  int h_samp_factor;
  int v_samp_factor;
  // The index of the quantization table used for this component.
  uint32_t quant_idx;
  // The dimensions of the component measured in 8x8 blocks.
  uint32_t width_in_blocks;
  uint32_t height_in_blocks;
  // The DCT coefficients of this component, laid out block-by-block, divided
  // through the quantization matrix values.
  hwy::AlignedFreeUniquePtr<coeff_t[]> coeffs;
};

// Quantization values for an 8x8 pixel block.
struct JPEGQuantTable {
  std::array<int32_t, DCTSIZE2> values;
  // The index of this quantization table as it was parsed from the input JPEG.
  // Each DQT marker segment contains an 'index' field, and we save this index
  // here. Valid values are 0 to 3.
  uint32_t index = 0;
};

// Huffman table indexes and MCU dimensions used for one component of one scan.
struct JPEGComponentScanInfo {
  uint32_t comp_idx;
  uint32_t dc_tbl_idx;
  uint32_t ac_tbl_idx;
  uint32_t mcu_ysize_blocks;
  uint32_t mcu_xsize_blocks;
};

// Contains information that is used in one scan.
struct JPEGScanInfo {
  // Parameters used for progressive scans (named the same way as in the spec):
  //   Ss : Start of spectral band in zig-zag sequence.
  //   Se : End of spectral band in zig-zag sequence.
  //   Ah : Successive approximation bit position, high.
  //   Al : Successive approximation bit position, low.
  uint32_t Ss;
  uint32_t Se;
  uint32_t Ah;
  uint32_t Al;
  uint32_t num_components = 0;
  std::array<JPEGComponentScanInfo, kMaxComponents> components;
  size_t MCU_rows;
  size_t MCU_cols;
};

// State of the decoder that has to be saved before decoding one MCU in case
// we run out of the bitstream.
struct MCUCodingState {
  coeff_t last_dc_coeff[kMaxComponents];
  int eobrun;
  std::vector<coeff_t> coeffs;
};

/* clang-format off */
constexpr uint32_t kJPEGNaturalOrder[80] = {
  0,   1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63,
  // extra entries for safety in decoder
  63, 63, 63, 63, 63, 63, 63, 63,
  63, 63, 63, 63, 63, 63, 63, 63
};

/* clang-format on */

}  // namespace jpegli

// Use this forward-declared libjpeg struct to hold all our private variables.
// TODO(szabadka) Remove variables that have a corresponding version in cinfo.
struct jpeg_decomp_master {
  enum class State {
    kStart,
    kProcessMarkers,
    kScan,
    kRender,
    kEnd,
  };
  State state_ = State::kStart;

  //
  // Input handling state.
  //
  // Number of bits after codestream_pos_ that were already processed.
  size_t codestream_bits_ahead_ = 0;

  //
  // Marker data processing state.
  //
  bool found_soi_ = false;
  bool found_sos_ = false;
  bool found_app0_ = false;
  bool found_dri_ = false;
  bool found_sof_ = false;
  bool found_eoi_ = false;
  bool is_ycbcr_ = true;
  size_t icc_index_ = 0;
  size_t icc_total_ = 0;
  std::vector<uint8_t> icc_profile_;
  size_t restart_interval_ = 0;
  std::vector<jpegli::JPEGQuantTable> quant_;
  std::vector<jpegli::JPEGComponent> components_;
  std::vector<jpegli::HuffmanTableEntry> dc_huff_lut_;
  std::vector<jpegli::HuffmanTableEntry> ac_huff_lut_;
  uint8_t huff_slot_defined_[256] = {};
  std::set<int> markers_to_save_;

  // Fields defined by SOF marker.
  bool is_progressive_;
  int max_h_samp_;
  int max_v_samp_;
  size_t iMCU_rows_;
  size_t iMCU_cols_;
  size_t iMCU_width_;
  size_t iMCU_height_;

  // Initialized at strat of frame.
  uint16_t scan_progression_[jpegli::kMaxComponents][DCTSIZE2];

  //
  // Per scan state.
  //
  jpegli::JPEGScanInfo scan_info_;
  size_t scan_mcu_row_;
  size_t scan_mcu_col_;
  jpegli::coeff_t last_dc_coeff_[jpegli::kMaxComponents];
  int eobrun_;
  int restarts_to_go_;
  int next_restart_marker_;

  jpegli::MCUCodingState mcu_;

  //
  // Rendering state.
  //
  size_t output_bit_depth_ = 8;
  size_t output_stride_;

  hwy::AlignedFreeUniquePtr<float[]> MCU_row_buf_;
  size_t MCU_row_stride_;
  size_t MCU_plane_size_;
  size_t MCU_buf_current_row_;
  size_t MCU_buf_ready_rows_;

  size_t output_row_;
  size_t output_mcu_row_;
  size_t output_ci_;
  // Temporary buffers for vertically upsampled chroma components. We keep a
  // ringbuffer of 3 * kBlockDim rows so that we have access for previous and
  // next rows.
  hwy::AlignedFreeUniquePtr<float[]> chroma_;
  size_t num_chroma_;
  size_t chroma_plane_size_;

  // In the rendering order, vertically upsampled chroma components come first.
  std::vector<size_t> component_order_;
  hwy::AlignedFreeUniquePtr<float[]> idct_scratch_;
  hwy::AlignedFreeUniquePtr<float[]> upsample_scratch_;
  hwy::AlignedFreeUniquePtr<uint8_t[]> output_scratch_;

  hwy::AlignedFreeUniquePtr<float[]> dequant_;
  // Per channel and per frequency statistics about the number of nonzeros and
  // the sum of coefficient absolute values, used in dequantization bias
  // computation.
  hwy::AlignedFreeUniquePtr<int[]> nonzeros_;
  hwy::AlignedFreeUniquePtr<int[]> sumabs_;
  std::vector<size_t> num_processed_blocks_;
  hwy::AlignedFreeUniquePtr<float[]> biases_;
};

#endif  // LIB_JPEGLI_DECODE_INTERNAL_H_