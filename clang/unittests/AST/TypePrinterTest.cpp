//===- unittests/AST/TypePrinterTest.cpp --- Type printer tests -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains tests for QualType::print() and related methods.
//
//===----------------------------------------------------------------------===//

#include "ASTPrint.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace ast_matchers;
using namespace tooling;

namespace {

static void PrintType(raw_ostream &Out, const ASTContext *Context,
                      const QualType *T,
                      PrintingPolicyAdjuster PolicyAdjuster) {
  assert(T && !T->isNull() && "Expected non-null Type");
  PrintingPolicy Policy = Context->getPrintingPolicy();
  if (PolicyAdjuster)
    PolicyAdjuster(Policy);
  T->print(Out, Policy);
}

::testing::AssertionResult
PrintedTypeMatches(StringRef Code, const std::vector<std::string> &Args,
                   const DeclarationMatcher &NodeMatch,
                   StringRef ExpectedPrinted,
                   PrintingPolicyAdjuster PolicyAdjuster) {
  return PrintedNodeMatches<QualType>(Code, Args, NodeMatch, ExpectedPrinted,
                                      "", PrintType, PolicyAdjuster);
}

} // unnamed namespace

TEST(TypePrinter, TemplateId) {
  std::string Code = R"cpp(
    namespace N {
      template <typename> struct Type {};

      template <typename T>
      void Foo(const Type<T> &Param);
    }
  )cpp";
  auto Matcher = parmVarDecl(hasType(qualType().bind("id")));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {}, Matcher, "const Type<T> &",
      [](PrintingPolicy &Policy) { Policy.FullyQualifiedName = false; }));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {}, Matcher, "const N::Type<T> &",
      [](PrintingPolicy &Policy) { Policy.FullyQualifiedName = true; }));
}

TEST(TypePrinter, TemplateId2) {
  std::string Code = R"cpp(
      template <template <typename ...> class TemplatedType>
      void func(TemplatedType<int> Param);
    )cpp";
  auto Matcher = parmVarDecl(hasType(qualType().bind("id")));

  // Regression test ensuring we do not segfault getting the QualType as a
  // string.
  ASSERT_TRUE(PrintedTypeMatches(Code, {}, Matcher, "<int>",
                                 [](PrintingPolicy &Policy) {
                                   Policy.FullyQualifiedName = true;
                                   Policy.PrintCanonicalTypes = true;
                                 }));
}

TEST(TypePrinter, ParamsUglified) {
  llvm::StringLiteral Code = R"cpp(
    template <typename _Tp, template <typename> class __f>
    const __f<_Tp&> *A = nullptr;
  )cpp";
  auto Clean = [](PrintingPolicy &Policy) {
    Policy.CleanUglifiedParameters = true;
  };

  ASSERT_TRUE(PrintedTypeMatches(Code, {},
                                 varDecl(hasType(qualType().bind("id"))),
                                 "const __f<_Tp &> *", nullptr));
  ASSERT_TRUE(PrintedTypeMatches(Code, {},
                                 varDecl(hasType(qualType().bind("id"))),
                                 "const f<Tp &> *", Clean));
}

TEST(TypePrinter, TemplateIdWithNTTP) {
  constexpr char Code[] = R"cpp(
    template <int N>
    struct Str {
      constexpr Str(char const (&s)[N]) { __builtin_memcpy(value, s, N); }
      char value[N];
    };
    template <Str> class ASCII {};

    ASCII<"this nontype template argument is too long to print"> x;
  )cpp";
  auto Matcher = classTemplateSpecializationDecl(
      hasName("ASCII"), has(cxxConstructorDecl(
                            isMoveConstructor(),
                            has(parmVarDecl(hasType(qualType().bind("id")))))));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {"-std=c++20"}, Matcher,
      R"(ASCII<{"this nontype template argument is [...]"}> &&)",
      [](PrintingPolicy &Policy) {
        Policy.EntireContentsOfLargeArray = false;
      }));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {"-std=c++20"}, Matcher,
      R"(ASCII<{"this nontype template argument is too long to print"}> &&)",
      [](PrintingPolicy &Policy) {
        Policy.EntireContentsOfLargeArray = true;
      }));
}

TEST(TypePrinter, TemplateIdWithFullTypeNTTP) {
  constexpr char Code[] = R"cpp(
    enum struct Encoding { UTF8, ASCII };
    template <int N, Encoding E = Encoding::ASCII>
    struct Str {
      constexpr Str(char const (&s)[N]) { __builtin_memcpy(value, s, N); }
      char value[N];
    };
    template <Str> class ASCII {};

    ASCII<"some string"> x;
  )cpp";
  auto Matcher = classTemplateSpecializationDecl(
      hasName("ASCII"), has(cxxConstructorDecl(
                            isMoveConstructor(),
                            has(parmVarDecl(hasType(qualType().bind("id")))))));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {"-std=c++20"}, Matcher,
      R"(ASCII<Str<12, Encoding::ASCII>{"some string"}> &&)",
      [](PrintingPolicy &Policy) {
        Policy.AlwaysIncludeTypeForNonTypeTemplateArgument = true;
      }));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {"-std=c++20"}, Matcher, R"(ASCII<{"some string"}> &&)",
      [](PrintingPolicy &Policy) {
        Policy.AlwaysIncludeTypeForNonTypeTemplateArgument = false;
      }));
}

TEST(TypePrinter, TemplateIdWithComplexFullTypeNTTP) {
  constexpr char Code[] = R"cpp(
  template< typename T, auto ... dims >
  struct NDArray {};

  struct Dimension
  {
      using value_type = unsigned short;

      value_type size{ value_type( 0 ) };
  };

  template < typename ConcreteDim >
  struct DimensionImpl : Dimension {};

  struct Width    : DimensionImpl< Width    > {};
  struct Height   : DimensionImpl< Height   > {};
  struct Channels : DimensionImpl< Channels > {};

  inline constexpr Width    W;
  inline constexpr Height   H;
  inline constexpr Channels C;

  template< auto ... Dims >
  consteval auto makeArray() noexcept
  {
      return NDArray< float, Dims ... >{};
  }

  [[ maybe_unused ]] auto x { makeArray< H, W, C >() };

  )cpp";
  auto Matcher = varDecl(
      allOf(hasAttr(attr::Kind::Unused), hasType(qualType().bind("id"))));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {"-std=c++20"}, Matcher,
      R"(NDArray<float, {{{0}}}, {{{0}}}, {{{0}}}>)",
      [](PrintingPolicy &Policy) {
        Policy.PrintCanonicalTypes = true;
        Policy.AlwaysIncludeTypeForNonTypeTemplateArgument = false;
      }));

  ASSERT_TRUE(PrintedTypeMatches(
      Code, {"-std=c++20"}, Matcher,
      R"(NDArray<float, Height{DimensionImpl<Height>{Dimension{0}}}, Width{DimensionImpl<Width>{Dimension{0}}}, Channels{DimensionImpl<Channels>{Dimension{0}}}>)",
      [](PrintingPolicy &Policy) {
        Policy.PrintCanonicalTypes = true;
        Policy.AlwaysIncludeTypeForNonTypeTemplateArgument = true;
      }));
}
