#include <torch/csrc/jit/passes/onnx/preclude_inlining.h>
#include <torch/csrc/jit/jit_log.h>
#include <torch/csrc/jit/passes/onnx/helper.h>

namespace torch {
namespace jit {

void functionCallSubstitution(Block* block) {
  for (auto it = block->nodes().begin(), end = block->nodes().end();
       it != end;) {
    Node* cur = *it++;
    switch (cur->kind()) {
      case prim::CallFunction: {
        AT_ASSERT(cur->input(0)->node()->kind() == prim::Constant);
        auto function_constant = cur->input(0)->node();
        auto fun_type =
            function_constant->output()->type()->expect<FunctionType>();

        if ((fun_type->function()->qualname().qualifiedName().find(
                 "torch.nn.functional") != std::string::npos) &&
            (fun_type->function()->qualname().qualifiedName().find(
                 "interpolate") != std::string::npos)) {
          cur->removeInput(0);
          Node* interpolate_node = block->owningGraph()->create(
              Symbol::fromQualString("aten::__interpolate"),
              {cur->inputs()},
              cur->outputs().size());
          interpolate_node->output()->copyMetadata(cur->output());
          interpolate_node->insertAfter(cur);
          cur->replaceAllUsesWith(interpolate_node);
          cur->removeAllInputs();
          cur->destroy();
        } else {
          cur->removeInput(0);
          functionCallSubstitution(fun_type->function()->graph()->block());
          GRAPH_UPDATE(
              "Inlining in ONNX preclude inlining function '",
              fun_type->function()->name(),
              "' to ",
              *cur);
          GRAPH_UPDATE(
              "Function in ONNX preclude inlining body: ",
              *fun_type->function()->optimized_graph());
          inlineCallTo(cur, fun_type->function(), false);
        }
      } break;
      case prim::CallMethod: {
        const std::string& name = cur->s(attr::name);
        if (auto class_type = cur->input(0)->type()->cast<ClassType>()) {
          auto function = class_type->getMethod(name);
          if (!function->isGraphFunction()) {
            continue;
          }
          functionCallSubstitution(function->graph()->block());
          GRAPH_UPDATE(
              "Inlining in ONNX preclude inlining function '",
              function->name(),
              "' to ",
              *cur);
          GRAPH_UPDATE(
              "Function in ONNX preclude inlining body: ",
              function->optimized_graph());
          inlineCallTo(cur, function, false);
        }
      } break;
      default: {
        for (auto b : cur->blocks()) {
          functionCallSubstitution(b);
        }
      } break;
    }
  }
}

// This pass is to be used for ONNX conversion only. The ONNX converter depends
// on a number of deprecated aten operators. These operators are removed from IR
// and replaced by the compiled python function code. However, in-order to
// maintain the behavior for ONNX conversion, we replace these function calls
// with the aten symbolic which can still be used by the ONNX converter.
void ONNXFunctionCallSubstitution(Graph& graph) {
  GRAPH_DUMP("Before stop-inlining calls: ", &graph);
  functionCallSubstitution(graph.block());
  GRAPH_DUMP("After stop-inlining calls: ", &graph);
}

} // namespace jit
} // namespace torch

