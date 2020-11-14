#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <set>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace editorui {

static constexpr glm::vec2 DEFAULT_NODE_SIZE = {64, 24};
static constexpr glm::vec4 DEFAULT_NODE_COLOR = {0.6f, 0.6f, 0.6f, 0.7f};

class NodeIdAllocator {
  static NodeIdAllocator* instance_;
  size_t nextId_=0;

public:
  static NodeIdAllocator& instance();
  void   setInitialId(size_t id) { nextId_ = id; }
  size_t newId() { return ++nextId_; }
};

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

class Graph;

struct GraphView {
  glm::vec2 canvasOffset = { 0,0 };
  float     canvasScale = 1;
  bool      drawGrid = true;
  bool      isActiveView = false;
  size_t    activeNode = -1;
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
  glm::vec2 selectionBoxStart = { 0, 0 };
  glm::vec2 selectionBoxEnd = { 0, 0 };

  Graph* graph = nullptr;

  void onGraphChanged(); // callback when graph has changed
};

class Graph {
protected:
  std::unordered_map<size_t, Node> nodes_;
  std::vector<Link> links_;
  std::vector<size_t> nodeOrder_;
  std::set<GraphView*> viewers_;

public:

  auto const& nodes() const { return nodes_; }
  auto const& links() const { return links_; }
  auto const& order() const { return nodeOrder_; }
  auto const& viewers() const { return viewers_; }

  size_t addNode(Node const& node) {
    size_t id = NodeIdAllocator::instance().newId();
    nodes_.insert({ id, node });
    nodeOrder_.push_back(id);
    return id;
  }

  Node& noderef(size_t idx) {
    return nodes_.at(idx);
  }

  void addViewer(GraphView* view) {
    if (view)
      viewers_.insert(view);
  }

  void removeViewer(GraphView* view) {
    if (view)
      viewers_.erase(view);
  }

  void removeNode(size_t idx) {
    nodes_.erase(idx);
    auto itr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
    nodeOrder_.erase(itr);
    for (auto itr = links_.begin(); itr != links_.end(); ++itr) {
      auto& link = *itr;
      if (link.startNode == idx || link.endNode == idx) {
        itr = links_.erase(itr);
      }
    }
    for (auto* view : viewers_)
      view->onGraphChanged();
  }

  template <class Container>
  void removeNodes(Container const& indices) {
    for (auto idx : indices) {
      nodes_.erase(idx);
      auto itr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
      nodeOrder_.erase(itr);
      for (auto itr = links_.begin(); itr != links_.end(); ++itr) {
        auto& link = *itr;
        if (link.startNode == idx || link.endNode == idx) {
          itr = links_.erase(itr);
        }
      }
    }
    for (auto* view : viewers_)
      view->onGraphChanged();
  }

  void shiftToEnd(size_t nodeid) {
    size_t idx = 0;
    for (; idx < nodeOrder_.size(); ++idx)
      if (nodeOrder_[idx] == nodeid) break;
    if (idx < nodeOrder_.size()) {
      for (size_t i = idx + 1; i < nodeOrder_.size(); ++i) {
        nodeOrder_[i - 1] = nodeOrder_[i];
      }
      nodeOrder_.back() = nodeid;
    }
  }
};

void updateAndDraw(GraphView& graph, char const* name);

}  // namespace editorui
