// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/err.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"

namespace functions {

namespace {

void MarkUsedAllValues(const FunctionCallNode* function,
                       Scope* source,
                       const std::set<std::string>& exclusion_set) {
  source->MarkAllUsed(exclusion_set);
}

void MarkUsedFromList(Scope* source,
                      const std::vector<Value>& list,
                      Err* err) {
  for (const Value& cur : list) {
    if (!cur.VerifyTypeIs(Value::STRING, err))
      return;
    source->GetValue(cur.string_value(), true);
  }
}

}  // namespace

const char kMarkUsedFrom[] = "mark_used_from";
const char kMarkUsedFrom_HelpShort[] =
    "mark_used_from: Marks variables as used from a different scope.";
const char kMarkUsedFrom_Help[] =
    R"(mark_used_from: Marks variables as used from a different scope.

  mark_used_from(from_scope, variable_list_or_star,
                 variables_to_not_mark_list = [])

  Marks the given variables from the given scope as used if they exist. This is
  normally used in the context of templates to mark variables defined in the
  template invocation as used in order to prevent "Assignment had no effect"
  errors.

  The variables in the given variable_list will be marked used if they exist in
  the given scope or any enclosing scope. If they do not exist, nothing will
  happen.

  As a special case, if the variable_list is a string with the value of "*", all
  variables from the given scope will be marked used. "*" only marks variables
  used that exist directly on the from_scope, not enclosing ones. Otherwise it
  would mark all global variables as used.

  If variables_to_not_mark_list is non-empty, then it must contains a list of
  variable names that will not be marked used. This is mostly useful when
  variable_list_or_star has a value of "*".

  See also "forward_variables_from" for copying variables from a different
  scope.

Examples

  # This is a common action template. It would invoke a script with
  # some given parameters, and wants to use the various types of deps
  # and the visibility from the invoker if it's defined. It also injects
  # an additional dependency to all targets depending on the visibility
  # flag.
  template("my_test") {
    action(target_name) {
      forward_variables_from(invoker, [ "data_deps", "deps",
                                        "public_deps", "visibility" ])
      if (defined(visibility) && visibility) {
        if (defined(invoker.extra_deps)) {
          # Add these extra deps to the dependencies.
          # "deps" may or may not be defined at this point.
          if (defined(deps)) {
            deps += invoker.extra_deps
          } else {
            deps = invoker.extra_deps
          }
        }
      } else {
        # Don't do anything with these extra deps.
        mark_used_from(invoker, [ "extra_deps" ])
      }
    }
  }

  # This is a template around a target whose type depends on a global
  # variable. It marks all values from the invoker as used.
  template("my_wrapper") {
    target(my_wrapper_target_type, target_name) {
      mark_used_from(invoker, "*")
    }

  # A template that wraps another. It adds behavior based on one
  # variable, and forwards all others to the nested target.
  template("my_ios_test_app") {
    ios_test_app(target_name) {
      mark_used_from(invoker, "*", ["test_bundle_name"])
      if (!defined(extra_substitutions)) {
        extra_substitutions = []
      }
      extra_substitutions += [ "BUNDLE_ID_TEST_NAME=$test_bundle_name" ]
    }
 }
)";

// This function takes a ListNode rather than a resolved vector of values
// both avoid copying the potentially-large source scope, and so the variables
// in the source scope can be marked as used.
Value RunMarkUsedFrom(Scope* scope,
                      const FunctionCallNode* function,
                      const ListNode* args_list,
                      Err* err) {
  const auto& args_vector = args_list->contents();
  if (args_vector.size() != 2 && args_vector.size() != 3) {
    *err = Err(function, "Wrong number of arguments.",
               "Expecting two or three arguments.");
    return Value();
  }

  // Extract the scope identifier. This assumes the first parameter is an
  // identifier. It is difficult to write code where this is not the case, and
  // this saves an expensive scope copy. If necessary, this could be expanded
  // to execute the ParseNode and get the value out if it's not an identifer.
  const IdentifierNode* identifier = args_vector[0]->AsIdentifier();
  if (!identifier) {
    *err = Err(args_vector[0].get(), "Expected an identifier for the scope.");
    return Value();
  }

  // Extract the source scope.
  Value* value = scope->GetMutableValue(identifier->value().value(),
                                        Scope::SEARCH_NESTED, true);
  if (!value) {
    *err = Err(identifier, "Undefined identifier.");
    return Value();
  }
  if (!value->VerifyTypeIs(Value::SCOPE, err))
    return Value();
  Scope* source = value->scope_value();

  // Extract the exclusion list if defined.
  std::set<std::string> exclusion_set;
  if (args_vector.size() == 3) {
    Value exclusion_value = args_vector[2]->Execute(scope, err);
    if (err->has_error())
      return Value();

    if (exclusion_value.type() != Value::LIST) {
      *err = Err(exclusion_value, "Not a valid list of variables to exclude.",
                 "Expecting a list of strings.");
      return Value();
    }

    for (const Value& cur : exclusion_value.list_value()) {
      if (!cur.VerifyTypeIs(Value::STRING, err))
        return Value();

      exclusion_set.insert(cur.string_value());
    }
  }

  // Extract the list. If all_values is not set, the what_value will be a list.
  Value what_value = args_vector[1]->Execute(scope, err);
  if (err->has_error())
    return Value();
  if (what_value.type() == Value::STRING) {
    if (what_value.string_value() == "*") {
      MarkUsedAllValues(function, source, exclusion_set);
      return Value();
    }
  } else {
    if (what_value.type() == Value::LIST) {
      MarkUsedFromList(source, what_value.list_value(), err);
      return Value();
    }
  }

  // Not the right type of argument.
  *err = Err(what_value, "Not a valid list of variables to mark used.",
             "Expecting either the string \"*\" or a list of strings.");
  return Value();
}

}  // namespace functions
