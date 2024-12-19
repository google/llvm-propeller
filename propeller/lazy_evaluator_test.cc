#include "propeller/lazy_evaluator.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
using ::testing::DoAll;
using ::testing::MockFunction;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SetArgReferee;

namespace {
struct UncopyableImmovable {
  explicit UncopyableImmovable(int value) : value(value) {}

  UncopyableImmovable(const UncopyableImmovable&) = delete;
  UncopyableImmovable& operator=(const UncopyableImmovable&) = delete;

  UncopyableImmovable(UncopyableImmovable&&) = delete;
  UncopyableImmovable& operator=(UncopyableImmovable&&) = delete;

  const int value = 0;
};

struct MoveOnly {
  explicit MoveOnly(int value) : value(value) {}

  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;

  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;

  const int value = 0;
};
}  // namespace

TEST(LazyEvaluator, Evaluates) {
  EXPECT_EQ(LazyEvaluator<int(double)>([](double) { return 1; },
                                       /*inputs=*/0.0)
                .Evaluate(),
            1);
}

TEST(LazyEvaluator, IsLazy) {
  MockFunction<int(double)> mock_function;
  EXPECT_CALL(mock_function, Call).Times(0);

  LazyEvaluator<int(double)> lazy_evaluator(mock_function.AsStdFunction(),
                                            /*inputs=*/0.0);
}

TEST(LazyEvaluator, IsCached) {
  MockFunction<int(double)> mock_function;
  EXPECT_CALL(mock_function, Call).WillOnce(Return(1));

  LazyEvaluator<int(double)> lazy_evaluator(mock_function.AsStdFunction(),
                                            /*inputs=*/0.0);

  EXPECT_EQ(lazy_evaluator.Evaluate(), 1);
  EXPECT_EQ(lazy_evaluator.Evaluate(), 1);
  EXPECT_EQ(lazy_evaluator.Evaluate(), 1);
}

TEST(LazyEvaluator, HandlesConstReferenceInput) {
  const int num = 10;
  MockFunction<double(const int&)> mock_function;
  EXPECT_CALL(mock_function, Call(Ref(num))).WillOnce(Return(1.0));

  EXPECT_EQ(LazyEvaluator<double(const int&)>(
                /*adapter=*/mock_function.AsStdFunction(), /*inputs=*/num)
                .Evaluate(),
            1.0);
}

TEST(LazyEvaluator, HandlesMutableReferenceInput) {
  int num = 10;

  MockFunction<int(int&)> mock_function;
  EXPECT_CALL(mock_function, Call(Ref(num)))
      .WillOnce(DoAll(SetArgReferee<0, int>(11), Return(10)));

  EXPECT_EQ(LazyEvaluator<int(int&)>(
                /*adapter=*/mock_function.AsStdFunction(), /*inputs=*/num)
                .Evaluate(),
            10);
  EXPECT_EQ(num, 11);
}

TEST(LazyEvaluator, HandlesMoveOnlyInput) {
  EXPECT_EQ(LazyEvaluator<int(MoveOnly)>(
                [](MoveOnly move_only) { return move_only.value; },
                /*inputs=*/MoveOnly(10))
                .Evaluate(),
            10);
}

TEST(LazyEvaluator, HandlesConstReferenceOutput) {
  const int num = 10;
  LazyEvaluator<const int&()> lazy_evaluator(
      [&num]() -> const int& { return num; });

  EXPECT_THAT(lazy_evaluator.Evaluate(), Ref(num));
}

TEST(LazyEvaluator, HandlesReferenceOutput) {
  int num = 10;
  LazyEvaluator<int&()> lazy_evaluator([&num]() -> int& { return num; });

  EXPECT_THAT(lazy_evaluator.Evaluate(), Ref(num));
}

TEST(LazyEvaluator, HandlesMoveOnlyOutput) {
  EXPECT_EQ(LazyEvaluator<MoveOnly(int)>([](int num) { return MoveOnly(num); },
                                         /*inputs=*/10)
                .Evaluate()
                .value,
            10);
}

TEST(LazyEvaluator, HandlesVoidInput) {
  EXPECT_EQ(LazyEvaluator<int()>([]() -> int { return 1; }).Evaluate(), 1);
}

TEST(LazyEvaluator, IsLazyForVoidInput) {
  MockFunction<int()> mock_function;
  EXPECT_CALL(mock_function, Call).Times(0);

  LazyEvaluator<int(void)> lazy_evaluator(mock_function.AsStdFunction());
}
}  // namespace propeller
