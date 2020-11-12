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
  std::vector<size_t> nodeOrder;
  bool showGrid = true;
  size_t activeNode = -1;
  std::set<size_t> nodeSelection;
  enum class OperationState : uint8_t {
    VIEWING,
    BOX_SELECTING,
    BOX_DESELECTING,
    NEWNODE_PENDING_CONFIRM,
    DRAGING_LINK_HEAD,
    DRAGING_LINK_BODY,
    DRAGING_LINK_TAIL,
  } operationState;
  glm::vec2 selectionBoxStart;
  glm::vec2 selectionBoxEnd;

  void addNode(Node const& node) {
    nodes.push_back(node);
    initOrder();
  }

  void removeNode(size_t idx) {
    nodes.erase(nodes.begin() + idx);
    auto itr = std::find(nodeOrder.begin(), nodeOrder.end(), idx);
    nodeOrder.erase(itr);
    for (auto& i : nodeOrder) {
      if (i > idx) --i;
    }
    if (activeNode == idx)
      activeNode = -1;
    else if (activeNode > idx)
      activeNode -= 1;
  }
  template <class Container>
  void removeNodes(Container const& indices) {
    std::vector<size_t> idxMap(nodes.size()), reverseIdxMap(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
      reverseIdxMap[i] = idxMap[i] = i;
    for (size_t idx : indices)
      idxMap[idx] = -1;
    size_t to = 0;
    for (size_t  from = 0; from < idxMap.size(); ++from) {
      if (idxMap[from] != -1) {
        idxMap[to] = idxMap[from];
        nodes[to] = std::move(nodes[from]);
        reverseIdxMap[from] = to;
        ++to;
      } else {
        reverseIdxMap[from] = -1;
      }
    }
    nodes.resize(to);
    
    // TODO: slow here
    for (auto idx : indices) {
      auto itr = std::find(nodeOrder.begin(), nodeOrder.end(), idx);
      nodeOrder.erase(itr);
    }
    for (auto& i : nodeOrder) {
      i = reverseIdxMap[i];
    }
    if (activeNode != -1)
      activeNode = reverseIdxMap[activeNode];
  }

  void initOrder() {
    size_t oldsize = nodeOrder.size();
    if (oldsize < nodes.size()) {
      nodeOrder.resize(nodes.size());
      for (; oldsize < nodes.size(); ++oldsize) nodeOrder[oldsize] = oldsize;
    }
  }

  void shiftToEnd(size_t nodeid) {
    initOrder();
    size_t idx = 0;
    for (; idx < nodeOrder.size(); ++idx)
      if (nodeOrder[idx] == nodeid) break;
    if (idx < nodeOrder.size()) {
      for (size_t i = idx + 1; i < nodeOrder.size(); ++i) {
        nodeOrder[i - 1] = nodeOrder[i];
      }
      nodeOrder.back() = nodeid;
    }
  }
};

void draw(Graph& graph);

}  // namespace editorui
