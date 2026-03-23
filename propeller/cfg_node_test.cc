// Copyright 2026 The Propeller Authors.
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

#include "propeller/cfg_node.h"

#include <string>

#include "gtest/gtest.h"
#include "llvm/Object/BBAddrMap.h"

namespace propeller {
namespace {

using ::llvm::object::BBAddrMap;

TEST(CFGNodeTest, GetName) {
  BBAddrMap::BBEntry::Metadata metadata = {
      .HasReturn = false,
      .HasTailCall = false,
      .IsEHPad = false,
      .CanFallThrough = false,
  };

  // Case 1: Entry node
  CFGNode entry_node(/*addr=*/0x1000, /*bb_index=*/0, /*bb_id=*/1, /*size=*/10,
                     metadata, /*hash=*/0, /*function_index=*/100);
  EXPECT_EQ(entry_node.GetName(), "100");

  // Case 2: Non-entry node
  CFGNode non_entry_node(/*addr=*/0x1020, /*bb_index=*/2, /*bb_id=*/3,
                         /*size=*/10, metadata, /*hash=*/0,
                         /*function_index=*/100);
  EXPECT_EQ(non_entry_node.GetName(), "100.2.id3");

  // Case 3: Cloned entry node
  CFGNode cloned_entry_node(/*addr=*/0x1000, /*bb_index=*/0, /*bb_id=*/1,
                            /*size=*/10, metadata, /*hash=*/0,
                            /*function_index=*/100, /*clone_number=*/1);
  EXPECT_EQ(cloned_entry_node.GetName(), "100.c1");

  // Case 4: Cloned non-entry node
  CFGNode cloned_non_entry_node(/*addr=*/0x1020, /*bb_index=*/2, /*bb_id=*/3,
                                /*size=*/10, metadata, /*hash=*/0,
                                /*function_index=*/100, /*clone_number=*/2);
  EXPECT_EQ(cloned_non_entry_node.GetName(), "100.2.id3.c2");
}

}  // namespace
}  // namespace propeller
