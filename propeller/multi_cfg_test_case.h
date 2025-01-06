// Copyright 2024 The Propeller Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PROPELLER_MULTI_CFG_TEST_CASE_H_
#define PROPELLER_MULTI_CFG_TEST_CASE_H_

#include "propeller/cfg_testutil.h"
#include "propeller/path_node.h"

namespace propeller {
// Returns the default MultiCfgArg to build a ProgramCfg as shown below.
//
//                      **function foo**
// **************************************************************
//      +---+    660     +--------+
// +--- | 2 | <--------- |   0    |
// |    +---+            +--------+
// |      |                |
// |      |                | 181
// |      |                v
// |      |              +--------+
// |      |              |   1    |
// |      |              +--------+
// |      |                  |
// |      |                  | 186
// |      |                  v
// |      |     656        +--------+
// |      +--------------> |   3    | --------------+
// |                       +--------+               |
// |                           |                    |
// |                           | 175                |
// |                           v                    |
// |       10                +------------+         |
// +-----------------------> |      4     |         | 690
//                           +------------+         |
//                             |    |   |           |
//                             |    |   | 185       |
//      +----------------------+    |   |           |
//      |                           |   |           |
//      |                    +------+   |           |
// call |                    |          v           |
//  90  |              call  |       +---------+    |
//      |               85   |       |    5    | <--+
//      |                    |       +---------+
//      |                    |            |
//      |                    |            |
// **************************************************************
//      |            *       |            |
//      v            *       |            |
//  **function bar** *       |            |
//   +-------+       *       |            |               **function qux**
//   |   0   |       *       |            +----------+       +-------+
//   +-------+       *       v                       |       |   0   |
//      |            *    **function baz**           |       +-------+
//      |            *   +-------+              ret  |           |             ^
//      | 90         *   |   0   |              875  |      870  |             |
//      v            *   +-------+                   |           |        call |
//   +-------+       *      |                        |           v         foo |
//   |   1   |       *      | 85                     |       +-------+     874 |
//   +-------+       *      v                        +-----> |   1   | --------+
//                   *   +-------+                           +-------+
//                   *   |   1   |
//                   *   +-------+
//                   *       |
// **************************************************************
//                tail call  |        **function fred**
//                       85  |        +--------+
//                           +----->  |    0   |
//                                    +--------+
//
// **************************************************************
MultiCfgArg GetDefaultProgramCfgArg();

// Returns the default ProgramPathProfileArg to build the path tree.
ProgramPathProfileArg GetDefaultPathProfileArg();
}  // namespace propeller
#endif  // PROPELLER_MULTI_CFG_TEST_CASE_H_
