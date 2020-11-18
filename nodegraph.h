#pragma once
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace editorui {
struct NodePin
{
  enum
  {
    INPUT,
    OUTPUT,
    NONE
  } type;
  size_t nodeIndex;
  int    pinNumber;
};

inline bool operator==(NodePin const& a, NodePin const& b)
{
  return a.nodeIndex == b.nodeIndex && a.pinNumber == b.pinNumber && a.type == b.type;
}

struct Link
{
  NodePin source;
  NodePin destiny;
};

inline bool operator==(Link const& a, Link const& b)
{
  return a.source == b.source && a.destiny == b.destiny;
}
} // namespace editorui

namespace std {
template<>
struct hash<editorui::NodePin>
{
  size_t operator()(editorui::NodePin const& pin) const noexcept
  {
    return std::hash<size_t>()(pin.nodeIndex) ^ std::hash<int>()(pin.pinNumber) ^ (int)pin.type;
  }
};
} // namespace std

namespace editorui {

static constexpr glm::vec2 DEFAULT_NODE_SIZE  = {64, 24};
static constexpr glm::vec4 DEFAULT_NODE_COLOR = {0.6f, 0.6f, 0.6f, 0.8f};

class NodeIdAllocator
{
  static NodeIdAllocator* instance_;
  size_t                  nextId_ = 0;

public:
  static NodeIdAllocator& instance();
  void                    setInitialId(size_t id) { nextId_ = id; }
  size_t                  newId() { return ++nextId_; }
};

struct Node
{
  enum class Type : uint32_t
  {
    NORMAL,     //< normal node
    ANCHOR,     //< an anchor to route links,
                //  implicitly has one input
                //  and infinite number of outputs
    COMMENTBOX, //< comment
    GROUPBOX    //< grouping
  } type                 = Type::NORMAL;
  std::string name       = "";
  int         numInputs  = 1;
  int         numOutputs = 1;
  glm::vec2   pos        = {0, 0};
  glm::vec4   color      = DEFAULT_NODE_COLOR;

  glm::vec2 size() const
  {
    return glm::vec2(std::max<float>(std::max(numInputs, numOutputs) * 10 / 0.9f, DEFAULT_NODE_SIZE.x), DEFAULT_NODE_SIZE.y);
  }
  glm::vec2 inputPinPos(int i) const
  {
    if (type == Type::NORMAL) {
      return glm::vec2((size().x * 0.9f) * float(i + 1) / (numInputs + 1) - size().x * 0.45f,
                       -size().y / 2.f - 4) +
             pos;
    } else {
      return pos;
    }
  }
  glm::vec2 outputPinPos(int i) const
  {
    if (type == Type::NORMAL) {
      return glm::vec2((size().x * 0.9f) * float(i + 1) / (numOutputs + 1) - size().x * 0.45f,
                       size().y / 2.f + 4) +
             pos;
    } else {
      return pos;
    }
  }

  // TODO
  bool beforeCreate();
  void afterCreate();
  bool onNameChanged(std::string const& newname);
  void onColorChanged(glm::vec4 const& newcolor);
  bool onSelected();
  bool onDoubleClicked();
  bool onMovedTo(glm::vec2 const& pos);
  bool beforeDelete();
  void afterDelete();
  bool onLinkedFrom(int myInputPin, Node const* that, int thatOutputPin);
  bool onLinkedTo(int myOutputPin, Node const* that, int thatInputPin);
};

class Graph;

struct GraphView
{
  glm::vec2        canvasOffset = {0, 0};
  float            canvasScale  = 1;
  bool             drawGrid     = true;
  bool             drawName     = true;
  size_t           hoveredNode  = -1;
  size_t           activeNode   = -1;
  NodePin          hoveredPin   = {NodePin::NONE, size_t(-1), -1};
  NodePin          activePin    = {NodePin::NONE, size_t(-1), -1};
  std::set<size_t> nodeSelection;
  enum class UIState : uint8_t
  {
    VIEWING,
    BOX_SELECTING,
    BOX_DESELECTING,
    PLACING_NEW_NODE,
    DRAGGING_NODES,
    DRAGGING_LINK_HEAD,
    DRAGGING_LINK_BODY,
    DRAGGING_LINK_TAIL,
    CUTING_LINK,
  } uiState                   = UIState::VIEWING;
  glm::vec2 selectionBoxStart = {0, 0};
  glm::vec2 selectionBoxEnd   = {0, 0};
  Link      pendingLink = {{NodePin::OUTPUT, size_t(-1), -1}, {NodePin::INPUT, size_t(-1), -1}};
  glm::vec2 pendingLinkPos;
  std::vector<glm::vec2> linkCuttingStroke;

  Graph* graph = nullptr;

  void onGraphChanged(); // callback when graph has changed
};

class Graph
{
protected:
  std::unordered_map<size_t, Node> nodes_;
  // std::vector<Link> links_;
  std::unordered_map<NodePin, NodePin>
      links_; // map from destiny to source, because each input pin accepts only one source, but
              // each output pin can be linked to many input pins
  std::unordered_map<NodePin, std::vector<glm::vec2>> linkPathes_; // cached link pathes
  std::vector<size_t>  nodeOrder_;
  std::set<GraphView*> viewers_;

public:
  auto const& nodes() const { return nodes_; }
  auto const& links() const { return links_; }
  auto const& linkPathes() const { return linkPathes_; }
  auto const& order() const { return nodeOrder_; }
  auto const& viewers() const { return viewers_; }

  size_t addNode(Node const& node)
  {
    size_t id = NodeIdAllocator::instance().newId();
    nodes_.insert({id, node});
    nodeOrder_.push_back(id);
    return id;
  }

  Node& noderef(size_t idx) { return nodes_.at(idx); }
  Node const& noderef(size_t idx) const { return nodes_.at(idx); }
  auto const& linkPath(NodePin const& pin) const { return linkPathes_.at(pin); }

  void addViewer(GraphView* view)
  {
    if (view)
      viewers_.insert(view);
  }

  void removeViewer(GraphView* view)
  {
    if (view)
      viewers_.erase(view);
  }

  void notifyViewers()
  {
    for (auto* v : viewers_)
      v->onGraphChanged();
  }

  std::vector<glm::vec2> genLinkPath(glm::vec2 const& start, glm::vec2 const& end)
  {
    std::vector<glm::vec2> path;
    float const xcenter = (start.x + end.x) * 0.5f;
    float const ycenter = (start.y + end.y) * 0.5f;
    path = {start,
            glm::vec2(start.x, glm::mix(start.y, end.y, 0.33f)),
            glm::vec2(end.x, glm::mix(start.y, end.y, 0.67f)),
            end};
    return path;
  }

  void updateLinkPath(size_t nodeidx, int ipin=-1)
  {
    if (ipin != -1) {
      auto np = NodePin{ NodePin::INPUT, nodeidx, ipin };
      auto linkitr = links_.find(np);
      if (linkitr!=links_.end()) {
        linkPathes_[np] = genLinkPath(
          nodes_[linkitr->second.nodeIndex].outputPinPos(linkitr->second.pinNumber),
          nodes_[nodeidx].inputPinPos(ipin));
      }
    } else {
      for (auto itr = links_.begin(); itr != links_.end(); ++itr) {
        if (itr->first.nodeIndex == nodeidx || itr->second.nodeIndex == nodeidx) {
          linkPathes_[itr->first] = genLinkPath(
            nodes_[itr->second.nodeIndex].outputPinPos(itr->second.pinNumber),
            nodes_[itr->first.nodeIndex].inputPinPos(itr->first.pinNumber));
        }
      }
    }
  }

  void addLink(size_t srcnode, int srcpin, size_t dstnode, int dstpin)
  {
    if (nodes_.find(srcnode) != nodes_.end() && nodes_.find(dstnode) != nodes_.end()) {
      removeLink(dstnode, dstpin);
      auto dst = NodePin{ NodePin::INPUT, dstnode, dstpin };
      links_[dst] = NodePin{ NodePin::OUTPUT, srcnode, srcpin };
      updateLinkPath(dstnode, dstpin);
    }
    notifyViewers();
  }

  void removeLink(size_t dstnode, int dstpin)
  {
    auto const np = NodePin{ NodePin::INPUT, dstnode, dstpin };
    links_.erase(np);
    linkPathes_.erase(np);
  }

  size_t upstreamNodeOf(size_t nodeidx, int pin)
  {
    auto src = links_.find(NodePin{NodePin::INPUT, nodeidx, pin});
    return src == links_.end() ? -1 : src->second.nodeIndex;
  }

  void removeNode(size_t idx)
  {
    nodes_.erase(idx);
    auto itr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
    nodeOrder_.erase(itr);
    for (auto itr = links_.begin(); itr != links_.end();) {
      if (itr->second.nodeIndex == idx || itr->first.nodeIndex == idx) {
        itr = links_.erase(itr);
      } else {
        ++itr;
      }
    }
    notifyViewers();
  }

  template<class Container>
  void removeNodes(Container const& indices)
  {
    for (auto idx : indices) {
      nodes_.erase(idx);
      auto itr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
      nodeOrder_.erase(itr);
      for (auto itr = links_.begin(); itr != links_.end();) {
        if (itr->second.nodeIndex == idx || itr->first.nodeIndex == idx) {
          itr = links_.erase(itr);
        } else {
          ++itr;
        }
      }
    }
    notifyViewers();
  }

  template<class Container>
  void moveNodes(Container const& indices, glm::vec2 const& delta)
  {
    for (auto idx : indices) {
      noderef(idx).pos += delta;
    }
    for (auto idx : indices) {
      updateLinkPath(idx);
    }
  }

  void shiftToEnd(size_t nodeid)
  {
    size_t idx = 0;
    for (; idx < nodeOrder_.size(); ++idx)
      if (nodeOrder_[idx] == nodeid)
        break;
    if (idx < nodeOrder_.size()) {
      for (size_t i = idx + 1; i < nodeOrder_.size(); ++i) {
        nodeOrder_[i - 1] = nodeOrder_[i];
      }
      nodeOrder_.back() = nodeid;
    }
  }
};

void updateAndDraw(GraphView& graph, char const* name);

} // namespace editorui
