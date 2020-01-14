//===- LinalgTraits.h - Linalg Traits ---------------------------*- C++ -*-===//
//
// Part of the MLIR Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_LINALG_LINALGTRAITS_H_
#define MLIR_DIALECT_LINALG_LINALGTRAITS_H_

#include "mlir/Dialect/Linalg/IR/LinalgTypes.h"
#include "mlir/Dialect/Utils/StructuredOpsUtils.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Support/LLVM.h"

namespace mlir {
namespace OpTrait {
namespace linalg {

/// This class provides the API for ops that are known to have a specified
/// number of inputs, all passed as operands. Use as a trait as follows:
///
///   class DotOp : public Op<DotOp, OpTrait::NInputs<2>::Impl> {
///
template <unsigned N> class NInputs {
public:
  template <typename ConcreteType>
  class Impl : public OpTrait::TraitBase<ConcreteType, NInputs<N>::Impl> {
  public:
    static unsigned getNumInputs() { return N; }
  };
};

/// This class provides the API for ops that are known to have a specified
/// number of outputs, all passed as operands. Use as a trait as follows:
///
///   class DotOp : public Op<DotOp, OpTrait::NOutputs<2>::Impl> {
///
template <unsigned N> class NOutputs {
public:
  template <typename ConcreteType>
  class Impl : public OpTrait::TraitBase<ConcreteType, NOutputs<N>::Impl> {
  public:
    static unsigned getNumOutputs() { return N; }
  };
};

/// This class provides the API for structured ops that are known to operate on
/// buffers or tensors. This trait must be used in conjunction with an op
/// definition or a trait that provides the methods `getNumInputs` and
/// `getNumOutputs`. Use as a trait as follows:
///
///   class DotOp : public Op<DotOp, OpTrait::StructuredOpTraits> {
///
template <typename ConcreteType>
class StructuredOpTraits
    : public OpTrait::TraitBase<ConcreteType, StructuredOpTraits> {
private:
  /// Return the number of inputs. For internal use only.
  unsigned nInputs() {
    return cast<ConcreteType>(this->getOperation()).getNumInputs();
  }
  /// Return the number of outputs. For internal use only.
  unsigned nOutputs() {
    return cast<ConcreteType>(this->getOperation()).getNumOutputs();
  }

public:
  /// Return the `i`-th input value.
  Value getInput(unsigned i) {
    assert(i < nInputs());
    return this->getOperation()->getOperand(i);
  }
  /// Return the index of `value` in the list of inputs if found, llvm::None
  /// otherwise.
  Optional<unsigned> getIndexOfInput(Value value) {
    auto it = llvm::find(getInputs(), value);
    if (it != getInputs().end())
      return it - getInputs().begin();
    return llvm::None;
  }
  /// Return the `i`-th input buffer type.
  ShapedType getInputShapedType(unsigned i) {
    return getInput(i).getType().template cast<ShapedType>();
  }
  /// Return the range over inputs.
  Operation::operand_range getInputs() {
    auto range = this->getOperation()->getOperands();
    return {range.begin(), range.begin() + nInputs()};
  }
  /// Return the `i`-th output.
  Value getOutput(unsigned i) {
    return this->getOperation()->getOperand(nInputs() + i);
  }
  /// Return the index of `value` in the list of output values if found,
  /// llvm::None otherwise.
  Optional<unsigned> getIndexOfOutput(Value value) {
    auto it = llvm::find(getOutputs(), value);
    if (it != getOutputs().end())
      return it - getOutputs().begin();
    return llvm::None;
  }
  /// Return the `i`-th output buffer type.
  ShapedType getOutputShapedType(unsigned i) {
    return getOutput(i).getType().template cast<ShapedType>();
  }
  /// Query whether the op has only MemRef input and outputs.
  bool hasBufferSemantics() {
    return this->getOperation()->getNumResults() == 0 &&
           llvm::all_of(getInputsAndOutputs(),
                        [](Value v) { return v.getType().isa<MemRefType>(); });
  }
  /// Query the subset of input operands that are of ranked tensor type.
  SmallVector<RankedTensorType, 4> getInputTensorTypes() {
    SmallVector<RankedTensorType, 4> res;
    for (Type type : getInputs().getTypes())
      if (auto t = type.template dyn_cast<RankedTensorType>())
        res.push_back(t);
    return res;
  }
  /// Query the subset of output operands that are of ranked tensor type.
  SmallVector<RankedTensorType, 4> getOutputTensorTypes() {
    SmallVector<RankedTensorType, 4> res;
    for (Type type : getOutputs().getTypes())
      if (auto t = type.template dyn_cast<RankedTensorType>())
        res.push_back(t);
    return res;
  }
  /// Return the range over outputs.
  Operation::operand_range getOutputs() {
    auto range = this->getOperation()->getOperands();
    return {range.begin() + nInputs(),
            range.begin() + getNumInputsAndOutputs()};
  }
  /// Return the number of inputs and outputs.
  unsigned getNumInputsAndOutputs() { return nInputs() + nOutputs(); }
  /// Return the `i`-th buffer type.
  ShapedType getShapedType(unsigned i) {
    return (i < nInputs()) ? getInputShapedType(i)
                           : getOutputShapedType(i - nInputs());
  }
  /// Return the range over inputs and outputs.
  Operation::operand_range getInputsAndOutputs() {
    auto range = this->getOperation()->getOperands();
    return {range.begin(), range.begin() + getNumInputsAndOutputs()};
  }
  unsigned getNumParallelLoops() {
    return getNumIterators(
        getParallelIteratorTypeName(),
        cast<ConcreteType>(this->getOperation()).iterator_types());
  }
  unsigned getNumReductionLoops() {
    return getNumIterators(
        getReductionIteratorTypeName(),
        cast<ConcreteType>(this->getOperation()).iterator_types());
  }
  unsigned getNumWindowLoops() {
    return getNumIterators(
        getWindowIteratorTypeName(),
        cast<ConcreteType>(this->getOperation()).iterator_types());
  }
  unsigned getNumLoops() {
    return getNumIterators(
        cast<ConcreteType>(this->getOperation()).iterator_types());
  }
  static LogicalResult verifyTrait(Operation *op) {
    auto nOperands = cast<ConcreteType>(op).getNumInputsAndOutputs();
    if (failed(OpTrait::impl::verifyAtLeastNOperands(op, nOperands)))
      return failure();
    return success();
  }
};

} // namespace linalg
} // namespace OpTrait
} // namespace mlir

#endif // MLIR_DIALECT_LINALG_LINALGTRAITS_H_
