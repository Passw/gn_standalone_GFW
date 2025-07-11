// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/args.h"

#include "gn/scheduler.h"
#include "gn/test_with_scope.h"
#include "util/test/test.h"

// Assertions for VerifyAllOverridesUsed() and DeclareArgs() with multiple
// toolchains.
TEST(ArgsTest, VerifyAllOverridesUsed) {
  TestWithScope setup1, setup2;
  Args args;
  Scope::KeyValueMap key_value_map1;
  Err err;
  LiteralNode assignment1;

  setup1.scope()->SetValue("a", Value(nullptr, true), &assignment1);
  setup1.scope()->GetCurrentScopeValues(&key_value_map1);
  EXPECT_TRUE(args.DeclareArgs(key_value_map1, setup1.scope(), &err));

  LiteralNode assignment2;
  setup2.scope()->SetValue("b", Value(nullptr, true), &assignment2);
  Scope::KeyValueMap key_value_map2;
  setup2.scope()->GetCurrentScopeValues(&key_value_map2);
  EXPECT_TRUE(args.DeclareArgs(key_value_map2, setup2.scope(), &err));

  // Override "a", shouldn't see any errors as "a" was defined.
  args.AddArgOverride("a", Value(nullptr, true));
  EXPECT_TRUE(args.VerifyAllOverridesUsed(&err));

  // Override "a", & "b", shouldn't see any errors as both were defined.
  args.AddArgOverride("b", Value(nullptr, true));
  EXPECT_TRUE(args.VerifyAllOverridesUsed(&err));

  // Override "a", "b" and "c", should fail as "c" was not defined.
  args.AddArgOverride("c", Value(nullptr, true));
  EXPECT_FALSE(args.VerifyAllOverridesUsed(&err));
}

// Ensure that arg overrides get only set after the they were declared.
TEST(ArgsTest, VerifyOverrideScope) {
  TestWithScope setup;
  Args args;
  Err err;

  args.AddArgOverride("a", Value(nullptr, "avalue"));
  args.AddArgOverride("current_os", Value(nullptr, "theiros"));

  Scope::KeyValueMap toolchain_overrides;
  toolchain_overrides["b"] = Value(nullptr, "bvalue");
  toolchain_overrides["current_os"] = Value(nullptr, "myos");
  args.SetupRootScope(setup.scope(), toolchain_overrides);

  // Overrides of arguments not yet declared aren't applied yet.
  EXPECT_EQ(nullptr, setup.scope()->GetValue("a"));
  EXPECT_EQ(nullptr, setup.scope()->GetValue("b"));

  // |current_os| is a system var. and already declared.
  // Thus it should have our override value.
  ASSERT_NE(nullptr, setup.scope()->GetValue("current_os"));
  EXPECT_EQ(Value(nullptr, "myos"), *setup.scope()->GetValue("current_os"));

  Scope::KeyValueMap key_value_map1;
  key_value_map1["a"] = Value(nullptr, "avalue2");
  key_value_map1["b"] = Value(nullptr, "bvalue2");
  key_value_map1["c"] = Value(nullptr, "cvalue2");
  EXPECT_TRUE(args.DeclareArgs(key_value_map1, setup.scope(), &err));

  ASSERT_NE(nullptr, setup.scope()->GetValue("a"));
  EXPECT_EQ(Value(nullptr, "avalue"), *setup.scope()->GetValue("a"));

  ASSERT_NE(nullptr, setup.scope()->GetValue("b"));
  EXPECT_EQ(Value(nullptr, "bvalue"), *setup.scope()->GetValue("b"));

  // This wasn't overwritten, so it should have the default value.
  ASSERT_NE(nullptr, setup.scope()->GetValue("c"));
  EXPECT_EQ(Value(nullptr, "cvalue2"), *setup.scope()->GetValue("c"));
}

// Ensure that GetArgFromAllArguments() searches for an arg from all arguments.
TEST(ArgsTest, VerifyGetArgFromAllArguments) {
  TestWithScope setup;
  Args args1;
  Err err;

  Scope::KeyValueMap key_value_map;
  Value a_value = Value(nullptr, "avalue");
  key_value_map["a"] = a_value;
  EXPECT_TRUE(args1.DeclareArgs(key_value_map, setup.scope(), &err));

  // Should not find "a" from overrides.
  ASSERT_EQ(nullptr, args1.GetArgOverride("a"));

  // Should find "a" from all args as it's declared.
  EXPECT_EQ(a_value, *args1.GetArgFromAllArguments("a"));

  // Should not find "b" from all args as it's not declared.
  EXPECT_FALSE(args1.GetArgFromAllArguments("b"));

  Args args2;
  args2.AddArgOverrides(key_value_map);

  // Should find "a" from overrides.
  const Value* a_value_from_ovderrides = args2.GetArgOverride("a");
  ASSERT_NE(nullptr, a_value_from_ovderrides);
  EXPECT_EQ(a_value, *a_value_from_ovderrides);

  // Should find "a" from all args since GetArgFromAllArguments() includes
  // overrides.
  EXPECT_EQ(a_value, *args2.GetArgFromAllArguments("a"));
}
