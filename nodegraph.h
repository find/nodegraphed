#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace editorui {

struct Node {
  size_t id;
  std::string name;
  int numInputs;
  int numOutpus;
  glm::vec2 pos;
  // glm::vec2 size;
};

struct Graph {
  glm::vec2 offset = {0, 0};
  float scale = 1;
  std::vector<Node> nodes;
  std::vector<size_t> nodeorder;
  bool showGrid = 1;

  void initOrder() {
    size_t oldsize = nodeorder.size();
    if (oldsize < nodes.size()) {
      nodeorder.resize(nodes.size());
      for (; oldsize < nodes.size(); ++oldsize) nodeorder[oldsize] = oldsize;
    }
  }

  void shiftToEnd(size_t nodeid) {
    initOrder();
    size_t idx = 0;
    for (; idx < nodeorder.size(); ++idx)
      if (nodes[idx].id == nodeid) break;
    if (idx < nodeorder.size()) {
      for (size_t i = idx + 1; i < nodeorder.size(); ++i) {
        nodeorder[i - 1] = nodeorder[i];
      }
      nodeorder.back() = nodeid;
    }
  }
};

void draw(Graph& graph);

}  // namespace editorui
