// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-context-specialization.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/contexts.h"

namespace v8 {
namespace internal {
namespace compiler {

Reduction JSContextSpecializer::Reduce(Node* node) {
  if (node->opcode() == IrOpcode::kJSLoadContext) {
    return ReduceJSLoadContext(node);
  }
  if (node->opcode() == IrOpcode::kJSStoreContext) {
    return ReduceJSStoreContext(node);
  }
  return NoChange();
}


Reduction JSContextSpecializer::ReduceJSLoadContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadContext, node->opcode());

  HeapObjectMatcher m(NodeProperties::GetValueInput(node, 0));
  // If the context is not constant, no reduction can occur.
  if (!m.HasValue()) {
    return NoChange();
  }

  const ContextAccess& access = ContextAccessOf(node->op());

  // Find the right parent context.
  Handle<Context> context = Handle<Context>::cast(m.Value().handle());
  for (size_t i = access.depth(); i > 0; --i) {
    context = handle(context->previous(), isolate());
  }

  // If the access itself is mutable, only fold-in the parent.
  if (!access.immutable()) {
    // The access does not have to look up a parent, nothing to fold.
    if (access.depth() == 0) {
      return NoChange();
    }
    const Operator* op = jsgraph_->javascript()->LoadContext(
        0, access.index(), access.immutable());
    node->set_op(op);
    node->ReplaceInput(0, jsgraph_->Constant(context));
    return Changed(node);
  }
  Handle<Object> value =
      handle(context->get(static_cast<int>(access.index())), isolate());

  // Even though the context slot is immutable, the context might have escaped
  // before the function to which it belongs has initialized the slot.
  // We must be conservative and check if the value in the slot is currently the
  // hole or undefined. If it is neither of these, then it must be initialized.
  if (value->IsUndefined() || value->IsTheHole()) {
    return NoChange();
  }

  // Success. The context load can be replaced with the constant.
  // TODO(titzer): record the specialization for sharing code across multiple
  // contexts that have the same value in the corresponding context slot.
  Node* constant = jsgraph_->Constant(value);
  ReplaceWithValue(node, constant);
  return Replace(constant);
}


Reduction JSContextSpecializer::ReduceJSStoreContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreContext, node->opcode());

  HeapObjectMatcher m(NodeProperties::GetValueInput(node, 0));
  // If the context is not constant, no reduction can occur.
  if (!m.HasValue()) {
    return NoChange();
  }

  const ContextAccess& access = ContextAccessOf(node->op());

  // The access does not have to look up a parent, nothing to fold.
  if (access.depth() == 0) {
    return NoChange();
  }

  // Find the right parent context.
  Handle<Context> context = Handle<Context>::cast(m.Value().handle());
  for (size_t i = access.depth(); i > 0; --i) {
    context = handle(context->previous(), isolate());
  }

  node->set_op(javascript()->StoreContext(0, access.index()));
  node->ReplaceInput(0, jsgraph_->Constant(context));
  return Changed(node);
}


Isolate* JSContextSpecializer::isolate() const { return jsgraph()->isolate(); }


JSOperatorBuilder* JSContextSpecializer::javascript() const {
  return jsgraph()->javascript();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
