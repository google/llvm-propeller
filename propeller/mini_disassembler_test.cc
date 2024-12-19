#include "propeller/mini_disassembler.h"

#include <memory>
#include <string>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/MC/MCInst.h"
#include "llvm/lib/Target/X86/MCTargetDesc/X86MCTargetDesc.h"
#include "propeller/binary_content.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::Not;

TEST(MiniDisassemblerTest, DisassembleOne) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<MiniDisassembler> md,
      MiniDisassembler::Create(binary_content->object_file.get()));
  ASSERT_OK_AND_ASSIGN(llvm::MCInst inst, md->DisassembleOne(0x4008e4));
  EXPECT_EQ(inst.getOpcode(), llvm::X86::RET64);
}

TEST(MiniDisassemblerTest, DisassembleOneFailure) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<MiniDisassembler> md,
      MiniDisassembler::Create(binary_content->object_file.get()));
  EXPECT_THAT(md->DisassembleOne(0x999999999), Not(IsOk()));
}

TEST(MiniDisassemblerTest, RetMayAffectControlFlow) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<MiniDisassembler> md,
      MiniDisassembler::Create(binary_content->object_file.get()));
  ASSERT_OK_AND_ASSIGN(llvm::MCInst ret_inst, md->DisassembleOne(0x4008e4));
  EXPECT_TRUE(md->MayAffectControlFlow(ret_inst));
}

TEST(MiniDisassemblerTest, CallMayAffectControlFlow) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<MiniDisassembler> md,
      MiniDisassembler::Create(binary_content->object_file.get()));
  ASSERT_OK_AND_ASSIGN(llvm::MCInst call_inst, md->DisassembleOne(0x4008c9));
  EXPECT_TRUE(md->MayAffectControlFlow(call_inst));
}

TEST(MiniDisassemblerTest, BranchMayAffectControlFlow) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<MiniDisassembler> md,
      MiniDisassembler::Create(binary_content->object_file.get()));
  EXPECT_THAT(md->MayAffectControlFlow(0x4008b6), IsOkAndHolds(true));
}

TEST(MiniDisassemblerTest, PushMayNotAffectControlFlow) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<MiniDisassembler> md,
      MiniDisassembler::Create(binary_content->object_file.get()));
  ASSERT_OK_AND_ASSIGN(llvm::MCInst push_inst, md->DisassembleOne(0x400590));
  EXPECT_FALSE(md->MayAffectControlFlow(push_inst));
}

}  // namespace
}  // namespace propeller
