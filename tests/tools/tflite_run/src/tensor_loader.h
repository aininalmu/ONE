/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __TFLITE_RUN_TENSOR_LOADER_H__
#define __TFLITE_RUN_TENSOR_LOADER_H__

#include <sys/mman.h>

#include <string>
#include <unordered_map>

#include "tflite/TensorView.h"

namespace tflite
{
class Interpreter;
}

namespace TFLiteRun
{

class TensorLoader
{
public:
  TensorLoader(tflite::Interpreter &interpreter);
  void loadDumpedTensors(const std::string &filename);
  void loadRawTensors(const std::string &filename, const std::vector<int> &tensor_indices);
  const nnfw::tflite::TensorView<float> &get(int tensor_idx) const;
  size_t getNums() const { return _tensor_map.size(); }

private:
  size_t loadTensorsFromRawData(const std::vector<int> &tensor_indices);
  tflite::Interpreter &_interpreter;
  std::unique_ptr<float[]> _raw_data;
  std::unordered_map<int, nnfw::tflite::TensorView<float>> _tensor_map;
};

} // end of namespace TFLiteRun

#endif // __TFLITE_RUN_TENSOR_LOADER_H__
