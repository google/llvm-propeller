//===--- Transformer.cpp - Transformer library implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Transformer.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/Refactoring/SourceCode.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <deque>
#include <string>
#include <utility>
#include <vector>

using namespace clang;
using namespace tooling;

using ast_matchers::MatchFinder;
using ast_type_traits::ASTNodeKind;
using ast_type_traits::DynTypedNode;
using llvm::Error;
using llvm::Expected;
using llvm::Optional;
using llvm::StringError;
using llvm::StringRef;
using llvm::Twine;

using MatchResult = MatchFinder::MatchResult;

// Did the text at this location originate in a macro definition (aka. body)?
// For example,
//
//   #define NESTED(x) x
//   #define MACRO(y) { int y  = NESTED(3); }
//   if (true) MACRO(foo)
//
// The if statement expands to
//
//   if (true) { int foo = 3; }
//                   ^     ^
//                   Loc1  Loc2
//
// For SourceManager SM, SM.isMacroArgExpansion(Loc1) and
// SM.isMacroArgExpansion(Loc2) are both true, but isOriginMacroBody(sm, Loc1)
// is false, because "foo" originated in the source file (as an argument to a
// macro), whereas isOriginMacroBody(SM, Loc2) is true, because "3" originated
// in the definition of MACRO.
static bool isOriginMacroBody(const clang::SourceManager &SM,
                              clang::SourceLocation Loc) {
  while (Loc.isMacroID()) {
    if (SM.isMacroBodyExpansion(Loc))
      return true;
    // Otherwise, it must be in an argument, so we continue searching up the
    // invocation stack. getImmediateMacroCallerLoc() gives the location of the
    // argument text, inside the call text.
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  }
  return false;
}

static llvm::Error invalidArgumentError(Twine Message) {
  return llvm::make_error<StringError>(llvm::errc::invalid_argument, Message);
}

static llvm::Error typeError(StringRef Id, const ASTNodeKind &Kind,
                             Twine Message) {
  return invalidArgumentError(
      Message + " (node id=" + Id + " kind=" + Kind.asStringRef() + ")");
}

static llvm::Error missingPropertyError(StringRef Id, Twine Description,
                                        StringRef Property) {
  return invalidArgumentError(Description + " requires property '" + Property +
                              "' (node id=" + Id + ")");
}

static Expected<CharSourceRange>
getTargetRange(StringRef Target, const DynTypedNode &Node, ASTNodeKind Kind,
               NodePart TargetPart, ASTContext &Context) {
  switch (TargetPart) {
  case NodePart::Node: {
    // For non-expression statements, associate any trailing semicolon with the
    // statement text.  However, if the target was intended as an expression (as
    // indicated by its kind) then we do not associate any trailing semicolon
    // with it.  We only associate the exact expression text.
    if (Node.get<Stmt>() != nullptr) {
      auto ExprKind = ASTNodeKind::getFromNodeKind<clang::Expr>();
      if (!ExprKind.isBaseOf(Kind))
        return getExtendedRange(Node, tok::TokenKind::semi, Context);
    }
    return CharSourceRange::getTokenRange(Node.getSourceRange());
  }
  case NodePart::Member:
    if (auto *M = Node.get<clang::MemberExpr>())
      return CharSourceRange::getTokenRange(
          M->getMemberNameInfo().getSourceRange());
    return typeError(Target, Node.getNodeKind(),
                     "NodePart::Member applied to non-MemberExpr");
  case NodePart::Name:
    if (const auto *D = Node.get<clang::NamedDecl>()) {
      if (!D->getDeclName().isIdentifier())
        return missingPropertyError(Target, "NodePart::Name", "identifier");
      SourceLocation L = D->getLocation();
      auto R = CharSourceRange::getTokenRange(L, L);
      // Verify that the range covers exactly the name.
      // FIXME: extend this code to support cases like `operator +` or
      // `foo<int>` for which this range will be too short.  Doing so will
      // require subcasing `NamedDecl`, because it doesn't provide virtual
      // access to the \c DeclarationNameInfo.
      if (getText(R, Context) != D->getName())
        return CharSourceRange();
      return R;
    }
    if (const auto *E = Node.get<clang::DeclRefExpr>()) {
      if (!E->getNameInfo().getName().isIdentifier())
        return missingPropertyError(Target, "NodePart::Name", "identifier");
      SourceLocation L = E->getLocation();
      return CharSourceRange::getTokenRange(L, L);
    }
    if (const auto *I = Node.get<clang::CXXCtorInitializer>()) {
      if (!I->isMemberInitializer() && I->isWritten())
        return missingPropertyError(Target, "NodePart::Name",
                                    "explicit member initializer");
      SourceLocation L = I->getMemberLocation();
      return CharSourceRange::getTokenRange(L, L);
    }
    return typeError(
        Target, Node.getNodeKind(),
        "NodePart::Name applied to neither DeclRefExpr, NamedDecl nor "
        "CXXCtorInitializer");
  }
  llvm_unreachable("Unexpected case in NodePart type.");
}

Expected<SmallVector<Transformation, 1>>
tooling::translateEdits(const MatchResult &Result,
                        llvm::ArrayRef<ASTEdit> Edits) {
  SmallVector<Transformation, 1> Transformations;
  auto &NodesMap = Result.Nodes.getMap();
  for (const auto &Edit : Edits) {
    auto It = NodesMap.find(Edit.Target);
    assert(It != NodesMap.end() && "Edit target must be bound in the match.");

    Expected<CharSourceRange> RangeOrErr = getTargetRange(
        Edit.Target, It->second, Edit.Kind, Edit.Part, *Result.Context);
    if (auto Err = RangeOrErr.takeError())
      return std::move(Err);
    Transformation T;
    T.Range = *RangeOrErr;
    if (T.Range.isInvalid() ||
        isOriginMacroBody(*Result.SourceManager, T.Range.getBegin()))
      return SmallVector<Transformation, 0>();
    T.Replacement = Edit.Replacement(Result);
    Transformations.push_back(std::move(T));
  }
  return Transformations;
}

RewriteRule tooling::makeRule(ast_matchers::internal::DynTypedMatcher M,
                              SmallVector<ASTEdit, 1> Edits) {
  M.setAllowBind(true);
  // `tryBind` is guaranteed to succeed, because `AllowBind` was set to true.
  return RewriteRule{*M.tryBind(RewriteRule::RootId), std::move(Edits),
                     nullptr};
}

constexpr llvm::StringLiteral RewriteRule::RootId;

void Transformer::registerMatchers(MatchFinder *MatchFinder) {
  MatchFinder->addDynamicMatcher(Rule.Matcher, this);
}

void Transformer::run(const MatchResult &Result) {
  if (Result.Context->getDiagnostics().hasErrorOccurred())
    return;

  // Verify the existence and validity of the AST node that roots this rule.
  auto &NodesMap = Result.Nodes.getMap();
  auto Root = NodesMap.find(RewriteRule::RootId);
  assert(Root != NodesMap.end() && "Transformation failed: missing root node.");
  SourceLocation RootLoc = Result.SourceManager->getExpansionLoc(
      Root->second.getSourceRange().getBegin());
  assert(RootLoc.isValid() && "Invalid location for Root node of match.");

  auto TransformationsOrErr = translateEdits(Result, Rule.Edits);
  if (auto Err = TransformationsOrErr.takeError()) {
    llvm::errs() << "Transformation failed: " << llvm::toString(std::move(Err))
                 << "\n";
    return;
  }
  auto &Transformations = *TransformationsOrErr;
  if (Transformations.empty()) {
    // No rewrite applied (but no error encountered either).
    RootLoc.print(llvm::errs() << "note: skipping match at loc ",
                  *Result.SourceManager);
    llvm::errs() << "\n";
    return;
  }

  // Convert the result to an AtomicChange.
  AtomicChange AC(*Result.SourceManager, RootLoc);
  for (const auto &T : Transformations) {
    if (auto Err = AC.replace(*Result.SourceManager, T.Range, T.Replacement)) {
      AC.setError(llvm::toString(std::move(Err)));
      break;
    }
  }

  Consumer(AC);
}
