#
# Copyright (C) 2018 The Android Open Source Project
# Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# refer to tanh_v1_dynamic.mod.py about the structore

# This adds reshape as the first op in a model and
# returns output of reshape, which is dynamic tensor

'''
Testing Softmax op when the input is dynamic.

      input [2, 5]  shape [2]  (value of shape will be [2, 5])
          |             |
          +-------------+
          |
       Reshape (added by DynamicInputGenerator since it generates its output to be dynamic)
          |
          | dynamic tensor at compilation time but the shape will be [2, 5] at execution time
          |
         Softmax
          |
        output (dynamic tensor, [2, 5] at execution time)
'''

import dynamic_tensor

model = Model()

model_input_shape = [2, 5]

beta = Float32Scalar("beta", 1.)

dynamic_layer = dynamic_tensor.DynamicInputGenerator(model, model_input_shape, "TENSOR_FLOAT32")

test_node_input = dynamic_layer.getTestNodeInput()

# write SOFTMAX test. input is `test_input`

# note output shape is used by expected output's shape
model_output = Output("output", "TENSOR_FLOAT32", "{2, 5}")

model.Operation("SOFTMAX", test_node_input, beta).To(model_output)

model_input_data = [1., 2., 3., 4., 5.,
                    -1., -2., -3., -4., -5.]

model_output_data = [0.011656231, 0.031684921, 0.086128544, 0.234121657, 0.636408647,
                    0.636408647, 0.234121657, 0.086128544, 0.031684921, 0.011656231]

Example({
    # use these two as input
    dynamic_layer.getModelInput(): model_input_data,
    dynamic_layer.getShapeInput() : model_input_shape,

    model_output: model_output_data,
})
