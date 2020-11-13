#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace editorui {

static constexpr glm::vec2 DEFAULT_NODE_SIZE = {64, 24};
static constexpr glm::vec4 DEFAULT_NODE_COLOR = {0.6f, 0.6f, 0.6f, 0.7f};

struct Node {
  enum class Type : uint32_t {
    NORMAL,      //< normal node
    ANCHOR,      //< an anchor to route links,
                 //  implicitly has one input
                 //  and infinite number of outputs
    COMMENTBOX,  //< comment
    GROUPBOX     //< grouping
  } type = Type::NORMAL;
  std::string name = "";
  int numInputs = 1;
  int numOutputs = 1;
  glm::vec2 pos = {0, 0};
  glm::vec2 size = DEFAULT_NODE_SIZE;
  glm::vec4 color = DEFAULT_NODE_COLOR;

  glm::vec2 inputPinPos(int i) const {
    if (type==Type::NORMAL) {
      return glm::vec2((size.x * 0.9f) * float(i+1) / (numInputs + 1) - size.x*0.45f, -size.y / 2.f) + pos;
    } else {
      return pos;
    }
  }
  glm::vec2 outputPinPos(int i) const {
    if (type == Type::NORMAL) {
      return glm::vec2((size.x * 0.9f) * float(i+1) / (numOutputs + 1) - size.x*0.45f, size.y / 2.f) + pos;
    } else {
      return pos;
    }
  }

  // TODO: next-gen API
  // virtual std::vector<glm::vec2> getShape() const {
  //   return{ {-32,12}, {32,12}, {32,-12}, {-32,-12} };
  // }
  // virtual bool hitTest(glm::vec2 const& pt) const {
  //   return pt.x <= DEFAULT_NODE_SIZE.x / 2 && pt.x >= -DEFAULT_NODE_SIZE.x / 2 && pt.y <= DEFAULT_NODE_SIZE.y / 2 && pt.y >= -DEFAULT_NODE_SIZE.y / 2;
  // }
};

struct Link {
  size_t startNode;
  int startPin;
  size_t endNode;
  int endPin;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<size_t> nodeOrder;
  std::vector<Link> links;

  bool canvasShowGrid = true;
  glm::vec2 canvasOffset = {0, 0};
  float scale = 1;
  size_t activeNode = -1;
  std::set<size_t> nodeSelection;
  enum class UIState : uint8_t {
    VIEWING,
    BOX_SELECTING,
    BOX_DESELECTING,
    PLACING_NEW_NODE,
    DRAGING_LINK_HEAD,
    DRAGING_LINK_BODY,
    DRAGING_LINK_TAIL,
  } uiState = UIState::VIEWING;
  glm::vec2 selectionBoxStart = {0, 0};
  glm::vec2 selectionBoxEnd = {0, 0};

  size_t addNode(Node const& node) {
    nodes.push_back(node);
    initOrder();
    return nodes.size() - 1;
  }

  void removeNode(size_t idx) {
    nodes.erase(nodes.begin() + idx);
    auto itr = std::find(nodeOrder.begin(), nodeOrder.end(), idx);
    nodeOrder.erase(itr);
    for (auto& i : nodeOrder) {
      if (i > idx) --i;
    }
    for (auto itr = links.begin(); itr != links.end(); ++itr) {
      auto& link = *itr;
      if (link.startNode == idx || link.endNode == idx) {
        itr = links.erase(itr);
      } else {
        if (link.startNode > idx) --link.startNode;
        if (link.endNode > idx) --link.endNode;
      }
    }
    if (activeNode == idx)
      activeNode = -1;
    else if (activeNode > idx)
      activeNode -= 1;
  }

  template <class Container>
  void removeNodes(Container const& indices) {
    std::vector<size_t> idxMap(nodes.size()), reverseIdxMap(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) reverseIdxMap[i] = idxMap[i] = i;
    for (size_t idx : indices) idxMap[idx] = -1;
    size_t to = 0;
    for (size_t from = 0; from < idxMap.size(); ++from) {
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
      for (auto itr = links.begin(); itr != links.end(); ++itr) {
        auto& link = *itr;
        if (link.startNode == idx || link.endNode == idx) {
          itr = links.erase(itr);
        }
      }
    }
    for (auto& link : links) {
      link.startNode = reverseIdxMap[link.startNode];
      link.endNode = reverseIdxMap[link.endNode];
    }
    for (auto& i : nodeOrder) {
      i = reverseIdxMap[i];
    }
    if (activeNode != -1) activeNode = reverseIdxMap[activeNode];
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

void updateAndDraw(Graph& graph);

}  // namespace editorui
