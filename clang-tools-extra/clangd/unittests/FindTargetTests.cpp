//===-- FindSymbolsTests.cpp -------------------------*- C++ -*------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "FindTarget.h"

#include "Selection.h"
#include "TestTU.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Annotations.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <initializer_list>

namespace clang {
namespace clangd {
namespace {

// A referenced Decl together with its DeclRelationSet, for assertions.
//
// There's no great way to assert on the "content" of a Decl in the general case
// that's both expressive and unambiguous (e.g. clearly distinguishes between
// templated decls and their specializations).
//
// We use the result of pretty-printing the decl, with the {body} truncated.
struct PrintedDecl {
  PrintedDecl(const char *Name, DeclRelationSet Relations = {})
      : Name(Name), Relations(Relations) {}
  PrintedDecl(const Decl *D, DeclRelationSet Relations = {})
      : Relations(Relations) {
    std::string S;
    llvm::raw_string_ostream OS(S);
    D->print(OS);
    llvm::StringRef FirstLine =
        llvm::StringRef(OS.str()).take_until([](char C) { return C == '\n'; });
    FirstLine = FirstLine.rtrim(" {");
    Name = FirstLine.rtrim(" {");
  }

  std::string Name;
  DeclRelationSet Relations;
};
bool operator==(const PrintedDecl &L, const PrintedDecl &R) {
  return std::tie(L.Name, L.Relations) == std::tie(R.Name, R.Relations);
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const PrintedDecl &D) {
  return OS << D.Name << " Rel=" << D.Relations;
}

// The test cases in for targetDecl() take the form
//  - a piece of code (Code = "...")
//  - Code should have a single AST node marked as a [[range]]
//  - an EXPECT_DECLS() assertion that verify the type of node selected, and
//    all the decls that targetDecl() considers it to reference
// Despite the name, these cases actually test allTargetDecls() for brevity.
class TargetDeclTest : public ::testing::Test {
protected:
  using Rel = DeclRelation;
  std::string Code;
  std::vector<const char *> Flags;

  // Asserts that `Code` has a marked selection of a node `NodeType`,
  // and returns allTargetDecls() as PrintedDecl structs.
  // Use via EXPECT_DECLS().
  std::vector<PrintedDecl> assertNodeAndPrintDecls(const char *NodeType) {
    llvm::Annotations A(Code);
    auto TU = TestTU::withCode(A.code());
    TU.ExtraArgs = Flags;
    auto AST = TU.build();
    EXPECT_THAT(AST.getDiagnostics(), ::testing::IsEmpty()) << Code;
    llvm::Annotations::Range R = A.range();
    SelectionTree Selection(AST.getASTContext(), AST.getTokens(), R.Begin,
                            R.End);
    const SelectionTree::Node *N = Selection.commonAncestor();
    if (!N) {
      ADD_FAILURE() << "No node selected!\n" << Code;
      return {};
    }
    EXPECT_EQ(N->kind(), NodeType) << Selection;

    std::vector<PrintedDecl> ActualDecls;
    for (const auto &Entry : allTargetDecls(N->ASTNode))
      ActualDecls.emplace_back(Entry.first, Entry.second);
    return ActualDecls;
  }
};

// This is a macro to preserve line numbers in assertion failures.
// It takes the expected decls as varargs to work around comma-in-macro issues.
#define EXPECT_DECLS(NodeType, ...)                                            \
  EXPECT_THAT(assertNodeAndPrintDecls(NodeType),                               \
              ::testing::UnorderedElementsAreArray(                            \
                  std::vector<PrintedDecl>({__VA_ARGS__})))                    \
      << Code
using ExpectedDecls = std::vector<PrintedDecl>;

TEST_F(TargetDeclTest, Exprs) {
  Code = R"cpp(
    int f();
    int x = [[f]]();
  )cpp";
  EXPECT_DECLS("DeclRefExpr", "int f()");

  Code = R"cpp(
    struct S { S operator+(S) const; };
    auto X = S() [[+]] S();
  )cpp";
  EXPECT_DECLS("DeclRefExpr", "S operator+(S) const");
}

TEST_F(TargetDeclTest, UsingDecl) {
  Code = R"cpp(
    namespace foo {
      int f(int);
      int f(char);
    }
    using foo::f;
    int x = [[f]](42);
  )cpp";
  // f(char) is not referenced!
  EXPECT_DECLS("DeclRefExpr", {"using foo::f", Rel::Alias},
               {"int f(int)", Rel::Underlying});

  Code = R"cpp(
    namespace foo {
      int f(int);
      int f(char);
    }
    [[using foo::f]];
  )cpp";
  // All overloads are referenced.
  EXPECT_DECLS("UsingDecl", {"using foo::f", Rel::Alias},
               {"int f(int)", Rel::Underlying},
               {"int f(char)", Rel::Underlying});

  Code = R"cpp(
    struct X {
      int foo();
    };
    struct Y : X {
      using X::foo;
    };
    int x = Y().[[foo]]();
  )cpp";
  EXPECT_DECLS("MemberExpr", {"using X::foo", Rel::Alias},
               {"int foo()", Rel::Underlying});
}

TEST_F(TargetDeclTest, ConstructorInitList) {
  Code = R"cpp(
    struct X {
      int a;
      X() : [[a]](42) {}
    };
  )cpp";
  EXPECT_DECLS("CXXCtorInitializer", "int a");

  Code = R"cpp(
    struct X {
      X() : [[X]](1) {}
      X(int);
    };
  )cpp";
  EXPECT_DECLS("RecordTypeLoc", "struct X");
}

TEST_F(TargetDeclTest, DesignatedInit) {
  Flags = {"-xc"}; // array designators are a C99 extension.
  Code = R"c(
    struct X { int a; };
    struct Y { int b; struct X c[2]; };
    struct Y y = { .c[0].[[a]] = 1 };
  )c";
  EXPECT_DECLS("DesignatedInitExpr", "int a");
}

TEST_F(TargetDeclTest, NestedNameSpecifier) {
  Code = R"cpp(
    namespace a { namespace b { int c; } }
    int x = a::[[b::]]c;
  )cpp";
  EXPECT_DECLS("NestedNameSpecifierLoc", "namespace b");

  Code = R"cpp(
    namespace a { struct X { enum { y }; }; }
    int x = a::[[X::]]y;
  )cpp";
  EXPECT_DECLS("NestedNameSpecifierLoc", "struct X");

  Code = R"cpp(
    template <typename T>
    int x = [[T::]]y;
  )cpp";
  // FIXME: We don't do a good job printing TemplateTypeParmDecls, apparently!
  EXPECT_DECLS("NestedNameSpecifierLoc", "");

  Code = R"cpp(
    namespace a { int x; }
    namespace b = a;
    int y = [[b]]::x;
  )cpp";
  EXPECT_DECLS("NestedNameSpecifierLoc", {"namespace b = a", Rel::Alias},
               {"namespace a", Rel::Underlying});
}

TEST_F(TargetDeclTest, Types) {
  Code = R"cpp(
    struct X{};
    [[X]] x;
  )cpp";
  EXPECT_DECLS("RecordTypeLoc", "struct X");

  Code = R"cpp(
    struct S{};
    typedef S X;
    [[X]] x;
  )cpp";
  EXPECT_DECLS("TypedefTypeLoc", {"typedef S X", Rel::Alias},
               {"struct S", Rel::Underlying});

  Code = R"cpp(
    template<class T>
    void foo() { [[T]] x; }
  )cpp";
  // FIXME: We don't do a good job printing TemplateTypeParmDecls, apparently!
  EXPECT_DECLS("TemplateTypeParmTypeLoc", "");

  Code = R"cpp(
    template<template<typename> class T>
    void foo() { [[T<int>]] x; }
  )cpp";
  EXPECT_DECLS("TemplateSpecializationTypeLoc", "template <typename> class T");

  Code = R"cpp(
    struct S{};
    S X;
    [[decltype]](X) Y;
  )cpp";
  EXPECT_DECLS("DecltypeTypeLoc", {"struct S", Rel::Underlying});

  Code = R"cpp(
    struct S{};
    [[auto]] X = S{};
  )cpp";
  // FIXME: deduced type missing in AST. https://llvm.org/PR42914
  EXPECT_DECLS("AutoTypeLoc");
}

TEST_F(TargetDeclTest, ClassTemplate) {
  Code = R"cpp(
    // Implicit specialization.
    template<int x> class Foo{};
    [[Foo<42>]] B;
  )cpp";
  EXPECT_DECLS("TemplateSpecializationTypeLoc",
               {"template<> class Foo<42>", Rel::TemplateInstantiation},
               {"class Foo", Rel::TemplatePattern});

  Code = R"cpp(
    // Explicit specialization.
    template<int x> class Foo{};
    template<> class Foo<42>{};
    [[Foo<42>]] B;
  )cpp";
  EXPECT_DECLS("TemplateSpecializationTypeLoc", "template<> class Foo<42>");

  Code = R"cpp(
    // Partial specialization.
    template<typename T> class Foo{};
    template<typename T> class Foo<T*>{};
    [[Foo<int*>]] B;
  )cpp";
  EXPECT_DECLS("TemplateSpecializationTypeLoc",
               {"template<> class Foo<int *>", Rel::TemplateInstantiation},
               {"template <typename T> class Foo<type-parameter-0-0 *>",
                Rel::TemplatePattern});
}

TEST_F(TargetDeclTest, FunctionTemplate) {
  Code = R"cpp(
    // Implicit specialization.
    template<typename T> bool foo(T) { return false; };
    bool x = [[foo]](42);
  )cpp";
  EXPECT_DECLS("DeclRefExpr",
               {"template<> bool foo<int>(int)", Rel::TemplateInstantiation},
               {"bool foo(T)", Rel::TemplatePattern});

  Code = R"cpp(
    // Explicit specialization.
    template<typename T> bool foo(T) { return false; };
    template<> bool foo<int>(int) { return false; };
    bool x = [[foo]](42);
  )cpp";
  EXPECT_DECLS("DeclRefExpr", "template<> bool foo<int>(int)");
}

TEST_F(TargetDeclTest, VariableTemplate) {
  // Pretty-printer doesn't do a very good job of variable templates :-(
  Code = R"cpp(
    // Implicit specialization.
    template<typename T> int foo;
    int x = [[foo]]<char>;
  )cpp";
  EXPECT_DECLS("DeclRefExpr", {"int foo", Rel::TemplateInstantiation},
               {"int foo", Rel::TemplatePattern});

  Code = R"cpp(
    // Explicit specialization.
    template<typename T> int foo;
    template <> bool foo<char>;
    int x = [[foo]]<char>;
  )cpp";
  EXPECT_DECLS("DeclRefExpr", "bool foo");

  Code = R"cpp(
    // Partial specialization.
    template<typename T> int foo;
    template<typename T> bool foo<T*>;
    bool x = [[foo]]<char*>;
  )cpp";
  EXPECT_DECLS("DeclRefExpr", {"bool foo", Rel::TemplateInstantiation},
               {"bool foo", Rel::TemplatePattern});
}

TEST_F(TargetDeclTest, TypeAliasTemplate) {
  Code = R"cpp(
    template<typename T, int X> class SmallVector {};
    template<typename U> using TinyVector = SmallVector<U, 1>;
    [[TinyVector<int>]] X;
  )cpp";
  EXPECT_DECLS("TemplateSpecializationTypeLoc",
               {"template<> class SmallVector<int, 1>",
                Rel::TemplateInstantiation | Rel::Underlying},
               {"class SmallVector", Rel::TemplatePattern | Rel::Underlying},
               {"using TinyVector = SmallVector<U, 1>",
                Rel::Alias | Rel::TemplatePattern});
}

TEST_F(TargetDeclTest, MemberOfTemplate) {
  Code = R"cpp(
    template <typename T> struct Foo {
      int x(T);
    };
    int y = Foo<int>().[[x]](42);
  )cpp";
  EXPECT_DECLS("MemberExpr", {"int x(int)", Rel::TemplateInstantiation},
               {"int x(T)", Rel::TemplatePattern});

  Code = R"cpp(
    template <typename T> struct Foo {
      template <typename U>
      int x(T, U);
    };
    int y = Foo<char>().[[x]]('c', 42);
  )cpp";
  EXPECT_DECLS("MemberExpr",
               {"template<> int x<int>(char, int)", Rel::TemplateInstantiation},
               {"int x(T, U)", Rel::TemplatePattern});
}

TEST_F(TargetDeclTest, Lambda) {
  Code = R"cpp(
    void foo(int x = 42) {
      auto l = [ [[x]] ]{ return x + 1; };
    };
  )cpp";
  EXPECT_DECLS("DeclRefExpr", "int x = 42");

  // It seems like this should refer to another var, with the outer param being
  // an underlying decl. But it doesn't seem to exist.
  Code = R"cpp(
    void foo(int x = 42) {
      auto l = [x]{ return [[x]] + 1; };
    };
  )cpp";
  EXPECT_DECLS("DeclRefExpr", "int x = 42");

  Code = R"cpp(
    void foo() {
      auto l = [x = 1]{ return [[x]] + 1; };
    };
  )cpp";
  // FIXME: why both auto and int?
  EXPECT_DECLS("DeclRefExpr", "auto int x = 1");
}

TEST_F(TargetDeclTest, ObjC) {
  Flags = {"-xobjective-c"};
  Code = R"cpp(
    @interface Foo {}
    -(void)bar;
    @end
    void test(Foo *f) {
      [f [[bar]] ];
    }
  )cpp";
  EXPECT_DECLS("ObjCMessageExpr", "- (void)bar");

  Code = R"cpp(
    @interface Foo { @public int bar; }
    @end
    int test(Foo *f) {
      return [[f->bar]];
    }
  )cpp";
  EXPECT_DECLS("ObjCIvarRefExpr", "int bar");

  Code = R"cpp(
    @interface Foo {}
    -(int) x;
    -(void) setX:(int)x;
    @end
    void test(Foo *f) {
      [[f.x]] = 42;
    }
  )cpp";
  EXPECT_DECLS("ObjCPropertyRefExpr", "- (void)setX:(int)x");

  Code = R"cpp(
    @interface Foo {}
    @property int x;
    @end
    void test(Foo *f) {
      [[f.x]] = 42;
    }
  )cpp";
  EXPECT_DECLS("ObjCPropertyRefExpr",
               "@property(atomic, assign, unsafe_unretained, readwrite) int x");

  Code = R"cpp(
    @protocol Foo
    @end
    id test() {
      return [[@protocol(Foo)]];
    }
  )cpp";
  EXPECT_DECLS("ObjCProtocolExpr", "@protocol Foo");

  Code = R"cpp(
    @interface Foo
    @end
    void test([[Foo]] *p);
  )cpp";
  EXPECT_DECLS("ObjCInterfaceTypeLoc", "@interface Foo");

  Code = R"cpp(
    @protocol Foo
    @end
    void test([[id<Foo>]] p);
  )cpp";
  EXPECT_DECLS("ObjCObjectTypeLoc", "@protocol Foo");

  Code = R"cpp(
    @class C;
    @protocol Foo
    @end
    void test(C<[[Foo]]> *p);
  )cpp";
  // FIXME: there's no AST node corresponding to 'Foo', so we're stuck.
  EXPECT_DECLS("ObjCObjectTypeLoc");
}

} // namespace
} // namespace clangd
} // namespace clang
