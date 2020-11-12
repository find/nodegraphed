#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <cstdint>

namespace editorui {

struct Node {
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
  size_t activeNode = -1;
  std::set<size_t> nodeSelection;
  enum class OperationState : uint8_t {
    VIEWING,
    BOX_SELECTING,
    BOX_DESELECTING,
    NEWNODE_PENDING_CONFIRM
  } operationState;
  glm::vec2 selectionBoxStart;
  glm::vec2 selectionBoxEnd;

  void addNode(Node const& node) {
    nodes.push_back(node);
    initOrder();
  }

  void removeNode(size_t idx) {
    nodes.erase(nodes.begin() + idx);
    auto itr = std::find(nodeorder.begin(), nodeorder.end(), idx);
    nodeorder.erase(itr);
    for (auto& i : nodeorder) {
      if (i > idx) --i;
    }
    if (activeNode == idx)
      activeNode = -1;
    else if (activeNode > idx)
      activeNode -= 1;
  }

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
      if (nodeorder[idx] == nodeid) break;
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
