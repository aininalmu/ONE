/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "Args.h"
#include "RecordMinMax.h"

int entry(const int argc, char **argv)
{
  using namespace record_minmax;

  Args args(argc, argv);
  auto input_model_path = args.getInputModelFilePath();
  auto input_data_path = args.getInputDataFilePath();
  auto output_model_path = args.getOutputModelFilePath();

  RecordMinMax rmm;

  // Initialize interpreter and observer
  rmm.initialize(input_model_path);

  // Profile min/max while executing the given input data
  rmm.profileData(input_data_path);

  // Save profiled values to the model
  rmm.saveModel(output_model_path);

  return EXIT_SUCCESS;
}
