/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd. All Rights Reserved
 * Copyright 2017 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "util/Utils.h"
#include "ir/InternalType.h"
#include "ir/Shape.h"
#include "ir/operation/AvgPool2D.h"
#include "ir/operation/MaxPool2D.h"
#include "util/ShapeInference.h"
#include "util/logging.h"

#include <cassert>
#include <sstream>
#include <cmath>

// TODO Move this file to runtime/onert/core/src/util

namespace onert
{
namespace shape_inference
{

//
// Helper functions
//

namespace
{

template <typename T, typename U>
typename std::enable_if<std::is_integral<T>::value && std::is_integral<U>::value,
                        typename std::common_type<T, U>::type>::type
ceil_div(T dividend, U divisor)
{
  assert(dividend > 0 && divisor > 0 && "this implementations is for positive numbers only");
  return (dividend + divisor - 1) / divisor;
}

// Calculate the result of broadcast of two shapes
ir::Shape broadcastShapes(const ir::Shape &lhs_shape, const ir::Shape &rhs_shape)
{
  ir::Shape out_shape;
  auto max_rank = std::max(lhs_shape.rank(), rhs_shape.rank());

  for (int idx = 0; idx < max_rank; ++idx)
  {
    // Go over operands dimensions from right to left
    int lhs_idx = lhs_shape.rank() - idx - 1;
    int rhs_idx = rhs_shape.rank() - idx - 1;

    int32_t lhs_dim = lhs_idx >= 0 ? lhs_shape.dim(lhs_idx) : 1;
    int32_t rhs_dim = rhs_idx >= 0 ? rhs_shape.dim(rhs_idx) : 1;

    if (lhs_dim != 1 && rhs_dim != 1 && lhs_dim != rhs_dim)
      throw std::runtime_error("Incompatible shapes for broadcast");

    out_shape.prepend(std::max(lhs_dim, rhs_dim));
  }

  return out_shape;
}

} // namespace

//
// Shape inference
//

// Calculate output height and width of convolution-like operation
std::pair<int, int> calcConvLikeHeightAndWidth(const int in_h, const int in_w, const int ker_h,
                                               const int ker_w, const ir::Padding pad,
                                               const ir::Stride stride)
{
  int32_t out_h = 0, out_w = 0;

  switch (pad.type)
  {
    case ir::PaddingType::SAME:
      out_h = ceil_div(in_h, stride.vertical);
      out_w = ceil_div(in_w, stride.horizontal);
      break;
    case ir::PaddingType::VALID:
      out_h = ceil_div(in_h - ker_h + 1, stride.vertical);
      out_w = ceil_div(in_w - ker_w + 1, stride.horizontal);
      break;
    case ir::PaddingType::EXPLICIT:
      out_h = (in_h + pad.param.top + pad.param.bottom - ker_h) / stride.vertical + 1;
      out_w = (in_w + pad.param.left + pad.param.right - ker_w) / stride.horizontal + 1;
      break;
    default:
      assert(false);
  }

  return {out_h, out_w};
}

ir::Shape inferEltwiseShape(const ir::Shape &lhs_shape, const ir::Shape &rhs_shape)
{
  return broadcastShapes(lhs_shape, rhs_shape);
}

ir::Shape argMaxShapes(const ir::Shape &input_shape, int axis, int rank)
{
  ir::Shape out_shape;
  for (int idx = 0; idx < rank; ++idx)
  {
    if (idx != axis)
    {
      int32_t input_dim = input_shape.dim(idx);
      out_shape.append(input_dim);
    }
  }

  return out_shape;
}

ir::Shape inferAvgPoolShape(const ir::Shape &in_shape, const ir::operation::AvgPool2D::Param &param,
                            const ir::Layout layout)
{
  assert(layout == ir::Layout::NHWC);
  auto ifm_shape = in_shape.asFeature(layout);
  const auto out_h_w = calcConvLikeHeightAndWidth(ifm_shape.H, ifm_shape.W, param.kh, param.kw,
                                                  param.padding, param.stride);
  // Pooling don't change number of channels and batch size
  return ir::Shape{ifm_shape.N, out_h_w.first, out_h_w.second, ifm_shape.C};
}

ir::Shape inferReduceShapes(const ir::Shape &input_shape, const std::vector<int> &axes,
                            bool keep_dims)
{
  int num_axis = axes.size();
  int input_num_dims = input_shape.rank();
  if (input_num_dims == 0)
  {
    ir::Shape out_shape(0);
    return out_shape;
  }
  if (keep_dims)
  {
    ir::Shape out_shape;
    for (int idx = 0; idx < input_num_dims; ++idx)
    {
      bool is_axis = false;
      for (int axis_idx = 0; axis_idx < num_axis; ++axis_idx)
      {
        if (axes[axis_idx] == idx || axes[axis_idx] + input_num_dims == idx)
        {
          is_axis = true;
          break;
        }
      }
      if (is_axis)
      {
        out_shape.append(1);
      }
      else
      {
        out_shape.append(input_shape.dim(idx));
      }
    }
    return out_shape;
  }
  else
  {
    // Calculates size of reducing axis.
    int num_reduce_axis = num_axis;
    for (int i = 0; i < num_axis; ++i)
    {
      int current = axes[i];
      if (current < 0)
      {
        current += input_num_dims;
      }
      assert(0 <= current && current < input_num_dims);
      for (int j = 0; j < i; ++j)
      {
        int previous = axes[j];
        if (previous < 0)
        {
          previous += input_num_dims;
        }
        if (current == previous)
        {
          --num_reduce_axis;
          break;
        }
      }
    }
    // Determines output dimensions.
    ir::Shape out_shape;
    int num_skip_axis = 0;
    for (int idx = 0; idx < input_num_dims; ++idx)
    {
      bool is_axis = false;
      for (int axis_idx = 0; axis_idx < num_axis; ++axis_idx)
      {
        if (axes[axis_idx] == idx || axes[axis_idx] + input_num_dims == idx)
        {
          ++num_skip_axis;
          is_axis = true;
          break;
        }
      }
      if (!is_axis)
      {
        out_shape.append(input_shape.dim(idx));
      }
    }
    return out_shape;
  }
}

ir::Shape inferBatchMatMulShape(const ir::Shape &lhs_shape, const ir::Shape &rhs_shape,
                                const ir::operation::BatchMatMul::Param &param)
{
  bool adj_x = param.adj_x;
  bool adj_y = param.adj_y;
  ir::Shape output_shape;

  int output_rank = std::max(lhs_shape.rank(), rhs_shape.rank());

  // Extend lhs and rhs shape
  ir::Shape extended_lhs_shape(lhs_shape);
  ir::Shape extended_rhs_shape(rhs_shape);
  extended_lhs_shape.extendRank(output_rank);
  extended_rhs_shape.extendRank(output_rank);

  for (int i = 0; i < output_rank - 2; i++)
  {
    const int lhs_dim = extended_lhs_shape.dim(i);
    const int rhs_dim = extended_rhs_shape.dim(i);
    int broadcast_dim = lhs_dim;
    if (lhs_dim != rhs_dim)
    {
      if (lhs_dim == 1)
      {
        broadcast_dim = rhs_dim;
      }
      else if (rhs_dim != 1)
      {
        throw std::runtime_error{"BatchMatMul shape inference: invalid brodcasting input shape"};
      }
    }

    output_shape.append(broadcast_dim);
  }

  // Fill in the matmul dimensions.
  int lhs_rows_index = adj_x ? output_rank - 1 : output_rank - 2;
  int rhs_cols_index = adj_y ? output_rank - 2 : output_rank - 1;

  output_shape.append(extended_lhs_shape.dim(lhs_rows_index));
  output_shape.append(extended_rhs_shape.dim(rhs_cols_index));

  return output_shape;
}

ir::Shape inferBroadcastToShape(const ir::Shape wshape, const int32_t *shape_buffer)
{
  const int num_elements = wshape.num_elements();

  assert(num_elements != 0);
  assert(shape_buffer);

  ir::Shape new_shape(num_elements);

  for (int i = 0; i < num_elements; ++i)
  {
    assert(shape_buffer[i] != 0); // It shouldn't be 0.
    new_shape.dim(i) = shape_buffer[i];
  }

  return new_shape;
}

ir::Shape inferConcatShape(const Shapes &in_shapes, const ir::operation::Concat::Param &param)
{
  const int32_t concat_axis = param.axis >= 0 ? param.axis : in_shapes[0].rank() + param.axis;
  const auto &first_in_shape = in_shapes[0];

  // Check that all shapes are equal except for concat axis dimension
  for (const auto &in_shape : in_shapes)
  {
    if (in_shape.rank() != first_in_shape.rank())
      throw std::runtime_error("Rank in all input tensors should be same");

    for (int64_t dim_idx = 0; dim_idx < in_shape.rank(); ++dim_idx)
      if (!(dim_idx == concat_axis || in_shape.dim(dim_idx) == first_in_shape.dim(dim_idx)))
        throw std::runtime_error("All tensor should have same dimension "
                                 "except dimension on passed axis");
  }

  // Calculate output shape
  ir::Shape out_shape(first_in_shape);
  out_shape.dim(concat_axis) = 0;
  for (const auto &in_shape : in_shapes)
    out_shape.dim(concat_axis) += in_shape.dim(concat_axis);
  return out_shape;
}

ir::Shape inferConv2DShape(const ir::Shape &in_shape, const ir::Shape &ker_shape,
                           const ir::operation::Conv2D::Param &param, ir::Layout layout)
{
  auto ifm_shape = in_shape.asFeature(layout);

  // Kernel format is [depth_out, kernel_height, kernel_width, depth_in]
  auto kf_shape = ker_shape.asFeature(layout);
  assert(ifm_shape.C == kf_shape.C);

  const auto out_h_w = calcConvLikeHeightAndWidth(ifm_shape.H, ifm_shape.W, kf_shape.H, kf_shape.W,
                                                  param.padding, param.stride);

  return ir::Shape{ifm_shape.N, out_h_w.first, out_h_w.second, kf_shape.N};
}

ir::Shape inferDepthwiseConv2DShape(const ir::Shape &in_shape, const ir::Shape &ker_shape,
                                    const ir::operation::DepthwiseConv2D::Param &param,
                                    ir::Layout layout)
{
  assert(layout == ir::Layout::NHWC);
  auto ifm_shape = in_shape.asFeature(layout);

  // Kernel format is [1, kernel_height, kernel_width, depth_out]
  auto kf_shape = ker_shape.asFeature(layout);
  assert(kf_shape.C == static_cast<int32_t>(ifm_shape.C * param.multiplier));
  assert(kf_shape.N == 1);

  const auto out_h_w = calcConvLikeHeightAndWidth(ifm_shape.H, ifm_shape.W, kf_shape.H, kf_shape.W,
                                                  param.padding, param.stride);

  return ir::Shape{ifm_shape.N, out_h_w.first, out_h_w.second, kf_shape.C};
}

ir::Shape inferExpandDimsShape(const ir::Shape &in_shape, int32_t axis)
{
  ir::Shape out_shape(in_shape.rank() + 1);

  axis = ((axis >= 0) ? axis : /* when axis < 0 */ (out_shape.rank() + axis));
  if (!(0 <= axis && axis <= in_shape.rank()))
    throw std::runtime_error("axis of dim is out of range");

  for (int x = 0, out_x = 0; out_x < out_shape.rank(); ++out_x)
  {
    if (out_x == axis)
      out_shape.dim(out_x) = 1;
    else
      out_shape.dim(out_x) = in_shape.dim(x++);
  }

  return out_shape;
}

ir::Shape inferFillShape(const ir::Shape &in_shape, const int32_t *buffer)
{
  ir::Shape out_shape(in_shape.dim(0));

  for (int out_x = 0; out_x < out_shape.rank(); ++out_x)
  {
    out_shape.dim(out_x) = buffer[out_x];
  }

  return out_shape;
}

ir::Shape inferFullyConnectedShape(const ir::Shape &in_shape, const ir::Shape &ker_shape)
{
  assert(in_shape.rank() >= 2);
  assert(ker_shape.rank() == 2);

  const auto input_size_with_batch = in_shape.num_elements();
  const auto num_units = ker_shape.dim(0);
  const auto input_size = ker_shape.dim(1);
  const auto batch_size = input_size_with_batch / input_size;
  assert(input_size_with_batch % input_size == 0);

  return {ir::Shape({static_cast<int32_t>(batch_size), num_units})};
}

ir::Shape gatherShapes(const ir::Shape &input_shape, const ir::Shape &indices_shape, int axis,
                       int rank)
{
  ir::Shape out_shape;
  const int indices_rank = indices_shape.rank();
  for (int idx = 0; idx < rank; ++idx)
  {
    if (idx == axis)
    {
      for (int indices_idx = 0; indices_idx < indices_rank; indices_idx++)
      {
        out_shape.append(indices_shape.dim(indices_idx));
      }
    }
    else
    {
      out_shape.append(input_shape.dim(idx));
    }
  }

  return out_shape;
}

ir::Shape inferMaxPoolShape(const ir::Shape &in_shape, const ir::operation::MaxPool2D::Param &param,
                            const ir::Layout layout)
{
  assert(layout == ir::Layout::NHWC);
  auto ifm_shape = in_shape.asFeature(layout);
  const auto out_h_w = calcConvLikeHeightAndWidth(ifm_shape.H, ifm_shape.W, param.kh, param.kw,
                                                  param.padding, param.stride);
  // Pooling don't change number of channels and batch size
  return ir::Shape{ifm_shape.N, out_h_w.first, out_h_w.second, ifm_shape.C};
}

ir::Shape onehotShape(const ir::Shape &input_shape, const int depth, int axis)
{
  assert(depth >= 0);
  const auto rank = input_shape.rank() + 1;
  ir::Shape newShape(rank);

  axis = (axis == -1) ? (rank - 1) : axis;

  for (int i = 0; i < rank; ++i)
  {
    if (i < axis)
    {
      newShape.dim(i) = input_shape.dim(i);
    }
    else if (i == axis)
    {
      newShape.dim(i) = depth;
    }
    else
    {
      newShape.dim(i) = input_shape.dim(i - 1);
    }
  }

  return newShape;
}

ir::Shape packShapes(const ir::Shape &input_shape, int axis, int rank, int num)
{
  ir::Shape out_shape;
  int in_idx = 0;

  for (int out_idx = 0; out_idx < rank; ++out_idx)
  {
    if (out_idx == axis)
    {
      out_shape.append(num);
    }
    else
    {
      out_shape.append(input_shape.dim(in_idx++));
    }
  }

  return out_shape;
}

ir::Shape inferPadShape(const ir::Shape &in_shape, const int32_t *pad_buf, const size_t num_pads)
{
  assert(num_pads % 2 == 0);
  const int32_t rank = num_pads / 2;

  ir::Shape ret(rank);
  for (int32_t i = 0; i < rank; ++i)
  {
    const auto before_padding = pad_buf[i * 2];
    const auto after_padding = pad_buf[i * 2 + 1];

    ret.dim(i) = in_shape.dim(i) + before_padding + after_padding;
  }

  return ret;
}

template <typename T> ir::Shape inferRangeShape(T start_val, T limit_val, T delta_val)
{
  ir::Shape out_shape(static_cast<int>(1));

  out_shape.dim(0) =
      (std::is_integral<T>::value
           ? ((std::abs(start_val - limit_val) + std::abs(delta_val) - 1) / std::abs(delta_val))
           : std::ceil(std::abs((start_val - limit_val) / delta_val)));
  return out_shape;
}

// template instantiation
template ir::Shape inferRangeShape(int start_val, int limit_val, int delta_val);
template ir::Shape inferRangeShape(float start_val, float limit_val, float delta_val);

ir::Shape inferReshapeShape(const int32_t *shape_buf, const int32_t shape_num_elements,
                            const size_t total_num_elements)
{
  ir::Shape ret(shape_num_elements);
  int32_t flatten_dim = ir::Shape::UNSPECIFIED_DIM;
  for (int32_t i = 0; i < shape_num_elements; ++i)
  {
    if (shape_buf[i] < 0)
    {
      if (flatten_dim != ir::Shape::UNSPECIFIED_DIM)
        throw std::runtime_error("Reshape: 2nd param has special dim(for flatten) more than twice");
      flatten_dim = i;
      ret.dim(i) = 1;
    }
    else
    {
      ret.dim(i) = shape_buf[i];
    }
  }
  if (flatten_dim != ir::Shape::UNSPECIFIED_DIM)
    ret.dim(flatten_dim) = total_num_elements / ret.num_elements();

  // Check reshapable
  if (total_num_elements != static_cast<size_t>(ret.num_elements()))
    throw std::runtime_error("Reshape: 2nd param is not compatible with the shape of input");

  return ret;
}

ir::Shape inferSelectShape(const ir::Shape &input_cond_shape, const ir::Shape &input_true_shape,
                           const ir::Shape &input_false_shape)
{
  auto haveSameShapes = [](const ir::Shape &input_cond_shape, const ir::Shape &input_true_shape,
                           const ir::Shape &input_false_shape) {
    if ((input_cond_shape.rank() != input_true_shape.rank()) ||
        input_cond_shape.rank() != input_false_shape.rank())
    {
      return false;
    }

    int rank = input_cond_shape.rank();
    for (int i = 0; i < rank; ++i)
    {
      if (input_cond_shape.dim(i) != input_true_shape.dim(i) ||
          input_cond_shape.dim(i) != input_false_shape.dim(i))
      {
        return false;
      }
    }

    return true;
  };

  auto calculateShape = [](const ir::Shape &input_cond_shape, const ir::Shape &input_true_shape,
                           const ir::Shape &input_false_shape, ir::Shape &new_shape) {
    ir::Shape cond_shape = input_cond_shape;
    ir::Shape true_shape = input_true_shape;
    ir::Shape false_shape = input_false_shape;
    int most_rank =
        (cond_shape.rank() >= true_shape.rank()) && (cond_shape.rank() >= false_shape.rank())
            ? cond_shape.rank()
            : (false_shape.rank() >= true_shape.rank() ? false_shape.rank() : true_shape.rank());

    ir::Shape calculate_shape(most_rank);

    cond_shape.extendRank(most_rank);
    true_shape.extendRank(most_rank);
    false_shape.extendRank(most_rank);

    for (int i = 0; i < most_rank; ++i)
    {
      calculate_shape.dim(i) =
          (cond_shape.dim(i) >= true_shape.dim(i)) && (cond_shape.dim(i) >= false_shape.dim(i))
              ? cond_shape.dim(i)
              : (false_shape.dim(i) >= true_shape.dim(i) ? false_shape.dim(i) : true_shape.dim(i));

      if ((cond_shape.dim(i) != calculate_shape.dim(i) && cond_shape.dim(i) != 1) ||
          (true_shape.dim(i) != calculate_shape.dim(i) && true_shape.dim(i) != 1) ||
          (false_shape.dim(i) != calculate_shape.dim(i) && false_shape.dim(i) != 1))
      {
        return false;
      }
    }

    new_shape = calculate_shape;

    return true;
  };

  bool havesame = haveSameShapes(input_cond_shape, input_true_shape, input_false_shape);
  if (havesame)
  {
    return input_cond_shape;
  }

  ir::Shape new_shape;
  bool possible = calculateShape(input_cond_shape, input_true_shape, input_false_shape, new_shape);

  if (!possible)
  {
    throw std::runtime_error("Broadcasting is not possible.");
  }

  return new_shape;
}

ir::Shape inferSliceShape(const ir::Shape &input_shape, const int32_t *begins, const int32_t *sizes)
{
  const uint32_t rank = input_shape.rank();
  ir::Shape out_shape(rank);

  for (uint32_t idx = 0; idx < rank; ++idx)
  {
    const auto input_dim = input_shape.dim(idx);

    // begin is zero-based
    auto begin = begins[idx];
    if (begin < 0)
      throw std::runtime_error("shape inference Slice: Invalid begin.");

    // size is one-based
    auto size = sizes[idx];
    if (size < -1)
      throw std::runtime_error("shape inference Slice: Invalid size.");

    if (size == -1)
    {
      size = input_dim - begin;
    }
    else
    {
      if (input_dim < begin + size)
        throw std::runtime_error("shape inference Slice: Invalid begin and size.");
    }
    out_shape.dim(idx) = size;
  }

  return out_shape;
}

ir::Shape inferSplitShape(const ir::Shape input_shape, int axis_value, int num_splits)
{
  ir::Shape newShape(input_shape);

  assert(axis_value >= 0);
  assert(axis_value < input_shape.rank());

  const int input_size = input_shape.dim(axis_value);
  assert(input_size % num_splits == 0);
  const int slice_size = input_size / num_splits;

  newShape.dim(axis_value) = slice_size;

  return newShape;
}

ir::Shape inferSqueezeShape(const ir::Shape &in_shape, const ir::operation::Squeeze::Param &param)
{
  const int ndims = param.ndim;
  const int *squeeze_dims = param.dims;
  bool should_squeeze[8] = {false};
  int num_squeezed_dims = 0;
  int shape_rank = in_shape.rank();
  if (ndims == 0)
  {
    for (int idx = 0; idx < shape_rank; ++idx)
    {
      if (in_shape.dim(idx) == 1)
      {
        should_squeeze[idx] = true;
        ++num_squeezed_dims;
      }
    }
  }
  else
  {
    for (int idx = 0; idx < ndims; ++idx)
    {
      int current = squeeze_dims[idx];
      if (current < 0)
      {
        current += shape_rank;
      }

      if (!(current >= 0 && current < shape_rank && in_shape.dim(current) == 1))
      {
        throw std::runtime_error(
            "The following conditions must be met: 0 <= dim < Shape rank, dim == 1");
      }

      if (!should_squeeze[current])
      {
        ++num_squeezed_dims;
      }
      should_squeeze[current] = true;
    }
  }

  // Set output shape.
  ir::Shape out_shape(shape_rank - num_squeezed_dims);
  for (int in_idx = 0, out_idx = 0; in_idx < shape_rank; ++in_idx)
  {
    if (!should_squeeze[in_idx])
    {
      out_shape.dim(out_idx++) = in_shape.dim(in_idx);
    }
  }

  return out_shape;
}

// helper for for StridedSlice
template <typename T>
StridedSliceParams buildStridedSliceParams(const T *begin, const T *end, const T *strides,
                                           const uint32_t begin_mask, const uint32_t end_mask,
                                           const uint32_t shrink_axis_mask, const uint8_t rank)
{
  StridedSliceParams op_params;
  op_params.start_indices_count = rank;
  op_params.stop_indices_count = rank;
  op_params.strides_count = rank;

  for (int i = 0; i < op_params.strides_count; ++i)
  {
    op_params.start_indices[i] = begin[i];
    op_params.stop_indices[i] = end[i];
    op_params.strides[i] = strides[i];

    assert(op_params.strides[i] != 0);
  }

  op_params.begin_mask = begin_mask;
  op_params.ellipsis_mask = 0; // NYI
  op_params.end_mask = end_mask;
  op_params.new_axis_mask = 0; // NYI
  op_params.shrink_axis_mask = shrink_axis_mask;

  assert(sizeof(op_params.begin_mask) * 4 >= rank);

  return op_params;
}

// template instantiation
template StridedSliceParams
buildStridedSliceParams(const uint32_t *begin, const uint32_t *end, const uint32_t *strides,
                        const uint32_t begin_mask, const uint32_t end_mask,
                        const uint32_t shrink_axis_mask, const uint8_t rank);

int Clamp(const int v, const int lo, const int hi)
{
  assert(!(hi < lo));
  if (hi < v)
    return hi;
  if (v < lo)
    return lo;
  return v;
}

int StartForAxis(const StridedSliceParams &params, const ir::Shape &input_shape, int axis)
{
  const auto begin_mask = params.begin_mask;
  const auto *start_indices = params.start_indices;
  const auto *strides = params.strides;
  // Begin with the specified index.
  int start = start_indices[axis];

  // begin_mask override
  if (begin_mask & 1 << axis)
  {
    if (strides[axis] > 0)
    {
      // Forward iteration - use the first element. These values will get
      // clamped below (Note: We could have set them to 0 and axis_size-1, but
      // use lowest() and max() to maintain symmetry with StopForAxis())
      start = std::numeric_limits<int>::lowest();
    }
    else
    {
      // Backward iteration - use the last element.
      start = std::numeric_limits<int>::max();
    }
  }

  // Handle negative indices
  int axis_size = input_shape.dim(axis);
  if (start < 0)
  {
    start += axis_size;
  }

  // Clamping
  start = Clamp(start, 0, axis_size - 1);

  return start;
}

// Return the "real" index for the end of iteration along that axis. This is an
// "end" in the traditional C sense, in that it points to one past the last
// element. ie. So if you were iterating through all elements of a 1D array of
// size 4, this function would return 4 as the stop, because it is one past the
// "real" indices of 0, 1, 2 & 3.
int StopForAxis(const StridedSliceParams &params, const ir::Shape &input_shape, int axis,
                int start_for_axis)
{
  const auto end_mask = params.end_mask;
  const auto shrink_axis_mask = params.shrink_axis_mask;
  const auto *stop_indices = params.stop_indices;
  const auto *strides = params.strides;

  // Begin with the specified index
  const bool shrink_axis = shrink_axis_mask & (1 << axis);
  int stop = stop_indices[axis];

  // When shrinking an axis, the end position does not matter (and can be
  // incorrect when negative indexing is used, see Issue #19260). Always use
  // start_for_axis + 1 to generate a length 1 slice, since start_for_axis has
  // already been adjusted for negative indices.
  if (shrink_axis)
  {
    stop = start_for_axis + 1;
  }

  // end_mask override
  if (end_mask & (1 << axis))
  {
    if (strides[axis] > 0)
    {
      // Forward iteration - use the last element. These values will get
      // clamped below
      stop = std::numeric_limits<int>::max();
    }
    else
    {
      // Backward iteration - use the first element.
      stop = std::numeric_limits<int>::lowest();
    }
  }

  // Handle negative indices

  const int axis_size = input_shape.dim(axis);
  if (stop < 0)
  {
    stop += axis_size;
  }

  // Clamping
  // Because the end index points one past the last element, we need slightly
  // different clamping ranges depending on the direction.
  if (strides[axis] > 0)
  {
    // Forward iteration
    stop = Clamp(stop, 0, axis_size);
  }
  else
  {
    // Backward iteration
    stop = Clamp(stop, -1, axis_size - 1);
  }

  return stop;
}

ir::Shape inferStridedSliceShape(const ir::Shape &input_shape, const StridedSliceParams &op_params,
                                 uint32_t rank)
{
  ir::Shape out_shape;

  for (uint32_t idx = 0; idx < rank; ++idx)
  {
    int32_t stride = op_params.strides[idx];
    int32_t begin = StartForAxis(op_params, input_shape, idx);
    int32_t end = StopForAxis(op_params, input_shape, idx, begin);

    // When shrinking an axis, the end position does not matter (and can be
    // incorrect when negative indexing is used, see Issue #19260). Always use
    // begin + 1 to generate a length 1 slice, since begin has
    // already been adjusted for negative indices by StartForAxis.
    const bool shrink_axis = op_params.shrink_axis_mask & (1 << idx);
    if (shrink_axis)
    {
      end = begin + 1;
    }

    int32_t dim_shape = std::ceil((end - begin) / static_cast<float>(stride));
    dim_shape = dim_shape < 0 ? 0 : dim_shape;
    if (!shrink_axis)
    {
      out_shape.append(dim_shape);
    }
  }

  return out_shape;
}

ir::Shape inferTileShape(const ir::Shape &in_shape, const int32_t *multiplier)
{
  // assert(in_shape.rank() == multiplier.rank());
  ir::Shape new_Shape(in_shape.rank());

  for (int i = 0; i < in_shape.rank(); ++i)
  {
    assert(multiplier[i]); // multiplier[i] shuld not be 0.
    new_Shape.dim(i) = in_shape.dim(i) * multiplier[i];
  }
  return new_Shape;
}

ir::Shape inferTransposeShape(const ir::Shape &in_shape, const std::vector<int> &perm)
{
  if (static_cast<int>(perm.size()) > in_shape.rank())
  {
    throw std::runtime_error("inferTransposeShape failed, bad rank size: " +
                             std::to_string(static_cast<int>(perm.size())));
  }
  ir::Shape out_shape(static_cast<int>(perm.size()));
  for (int idx = 0; idx < static_cast<int>(perm.size()); idx++)
  {
    if (perm[idx] < 0 || perm[idx] >= static_cast<int>(perm.size()))
    {
      throw std::runtime_error("inferTransposeShape failed, bad perm value: " +
                               std::to_string(perm[idx]));
    }
    out_shape.dim(idx) = in_shape.dim(perm[idx]);
  }
  return out_shape;
}

ir::Shape unpackShapes(const ir::Shape &input_shape, int axis, int rank)
{
  ir::Shape out_shape;

  for (int out_idx = 0; out_idx < rank; out_idx++)
  {
    if (out_idx != axis)
    {
      out_shape.append(input_shape.dim(out_idx));
    }
  }

  return out_shape;
}

/*
  StaticInferer
  - Write methods except visit()
  - For visit() of each operator, find each op's C file
*/

void StaticInferer::handleBinaryArithmeticOp(const ir::Operation &op,
                                             const ir::OperandIndex lhs_idx,
                                             const ir::OperandIndex rhs_idx)
{
  const auto &lhs = _operands.at(lhs_idx);
  const auto &rhs = _operands.at(rhs_idx);

  const auto output_idx = op.getOutputs().at(0);
  ir::Operand &output = _operands.at(output_idx);

  if (lhs.info().isDynamic() || rhs.info().isDynamic())
  {
    output.info().setDynamic();
    return;
  }

  // re-sizing output shape
  ir::Shape new_shape = inferEltwiseShape(lhs.info().shape(), rhs.info().shape());
  output.info().shape(new_shape);
}

void StaticInferer::handleSimpleUnaryOp(const ir::Operation &op, const ir::OperandIndex input_idx)
{
  const auto &input = _operands.at(input_idx);

  // get mutable output operand
  const auto output_idx = op.getOutputs().at(0);
  ir::Operand &output = _operands.at(output_idx);

  // if input is dynamic, output also becomes dynamic
  if (input.info().isDynamic())
  {
    output.info().setDynamic();
    return;
  }

  // re-sizing output shape
  ir::Shape new_shape = input.info().shape();
  output.info().shape(new_shape);
}

void StaticInferer::dump()
{
  auto get_shape_str = [](const ir::Shape &shape) {
    std::stringstream sstream;
    sstream << "shape : {";
    for (int i = 0; i < shape.rank(); i++)
    {
      if (i == 0)
        sstream << shape.dim(i);
      else
        sstream << " " << shape.dim(i);
    }
    sstream << "}";
    return sstream.str();
  };

  for (const auto &pair : _lowered_subgs)
  {
    const auto index = pair.first;
    const auto &lowered_subg = pair.second;
    VERBOSE(StaticInferer) << "SubGraph #" << index.value() << std::endl;
    lowered_subg->graph().operands().iterate(
        [&](const ir::OperandIndex &ind, const ir::Operand &operand) {
          VERBOSE(StaticInferer) << "Operand #" << ind.value() << ", "
                                 << (operand.info().isDynamic() ? "Dynamic" : "Static") << ", "
                                 << get_shape_str(operand.info().shape()) << std::endl;
        });
  }
}

/*
 * DynamicInferer
  - Write methods except visit()
  - For visit() of each operator, find each op's C file
 */

void DynamicInferer::handleBinaryArithmeticOp(const ir::Operation &op,
                                              const ir::OperandIndex lhs_idx,
                                              const ir::OperandIndex rhs_idx)
{
  auto lhs = _tensor_registry->getITensor(lhs_idx);
  auto lhs_shape = lhs->getShape();

  auto rhs = _tensor_registry->getITensor(rhs_idx);
  auto rhs_shape = rhs->getShape();

  /*
    Here, the state after compilation (satic shape inference) could be one of the following:

              lhs       rhs              output     execution-time shape inf required
      ------------------------------------------    ---------------------------------
      case 1) static    static           static      X
      case 2) one or both are dynamic    dynamic     O

    Then nnfw_apply_tensorinf() could change one or both inputs dynamic.
    So, in this method, we have one more state and we have to re-calculate shape for this shape.

      case 3) one or both are dynamic    static      O

    So, only when all inputs are static, we can skip dynamic shape inference.
  */
  if ((!lhs->is_dynamic()) && (!rhs->is_dynamic()))
    return;

  auto output_idx = op.getOutputs().at(0);
  auto output = _tensor_registry->getITensor(output_idx);

  ir::Shape new_shape = inferEltwiseShape(lhs_shape, rhs_shape);

  _dynamic_tensor_manager->applyShape(output_idx, new_shape);
  assert(output->buffer() != nullptr);
}

void DynamicInferer::handleSimpleUnaryOp(const ir::Operation &op, const ir::OperandIndex input_ind)
{
  // check if input is not dynamic
  auto input = _tensor_registry->getITensor(input_ind);
  auto output_shape = input->getShape();

  /*
    Here, the state after compilation (satic shape inference) could be one of the following:

              input      output    execution-time shape inf required
      -------------------------    ---------------------------------
      case 1) static     static      X
      case 2) dynamic    dynamic     O

    Then nnfw_apply_tensorinf() could change input dynamic.
    So, in this method, we have one more state and we have to re-calculate shape for this shape.

      case 3) dynamic    static      O

    So, only when input is static, we can skip dynamic shape inference.
  */
  if (!input->is_dynamic())
    return;

  auto output_ind = op.getOutputs().at(0);
  auto output = _tensor_registry->getITensor(output_ind);

  _dynamic_tensor_manager->applyShape(output_ind, output_shape);
  assert(output->buffer() != nullptr);
}

} // namespace shape_inference
} // namespace onert
