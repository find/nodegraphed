#pragma once
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstdint>
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

struct GraphView;
class Node;
class Graph;

/// NodeGraphHook - this is the public interface.
/// implement these functions to bind your own node & graph with the UI graph
class NodeGraphHook
{
public:
  /// called after the UI graph was saved
  /// @param host: the graph hosts this hook lives within
  /// @param jsobj: the json section to write to
  /// @return: succesfully saved or not
  virtual bool save(Graph const* host, nlohmann::json& jsobj) { return false; }

  /// called after the UI graph was loaed
  /// @param host: the graph hosts this hook lives within
  /// @param jsobj: the json section to load
  /// @return: succesfully loaded or not
  virtual bool load(Graph* host, nlohmann::json const& jsobj) { return false; }

  /// check if the graph can be created given this host
  virtual bool graphCanBeCreated(Graph const* host) { return true; }

  /// creates a new custom graph
  virtual void* createGraph(Graph const* host) { return nullptr; }

  /// check if the node of given name can be created
  virtual bool nodeCanBeCreated(Graph const* host, std::string const& name) { return true; }

  /// creates a new custom node
  virtual void* createNode(Graph* host, std::string const& name) { return nullptr; }

  /// called when trying to change a node's name
  /// @param node: the UI node hosting logical node
  /// @param newname: the new node name, can be changed, your change will be the real display name
  /// @return: true if this rename is acceptable, else false
  virtual bool onNodeNameChanged(Node const* node, std::string& newname) { return true; }

  /// just let you know that the UI node color has been changed
  virtual void onNodeColorChanged(Node const* node, glm::vec4 const& newcolor) {}

  /// your node size
  virtual glm::vec2 getNodeSize(Node const* node) { return DEFAULT_NODE_SIZE; }

  // as the name states ..
  virtual int getNodeMinInputCount(Node const* node) { return 1; }
  virtual int getNodeMaxInputCount(Node const* node) { return 4; }
  virtual int getNodeOutputCount(Node const* node) { return 1; }

  /// called after the default shape has been drawn
  /// you may draw some kind of overlays there
  virtual void onNodeDraw(Node const* node, GraphView const& gv) {}

  /// called after the default graph has been drawn
  /// you may draw some kind of overlays there
  virtual void onGraphDraw(Graph const* host, GraphView const& gv) {}

  /// called when the UI node has been inspected
  virtual void onNodeInspect(Node* node, GraphView const& gv) {}

  virtual bool onNodeSelected(Node const* node, GraphView const& gv) { return true; }
  virtual void onNodeDeselected(Node const* node, GraphView const& gv) { }
  virtual bool onNodeClicked(Node const* node) { return true; }
  virtual bool onNodeDoubleClicked(Node const* node) { return true; }
  virtual bool onNodeMovedTo(Node* node, glm::vec2 const& pos) { return true; }
  virtual bool canDeleteNode(Node* node) { return true; }
  virtual void beforeDeleteNode(Node* node) {}
  virtual void beforeDeleteGraph(Graph* host) {}
  virtual bool canLinkTo(Node* source, int srcOutputPin, Node* dest, int destInputPin)
  {
    return true;
  }
  virtual void onLinkAttached(Node* source, int srcOutputPin, Node* dest, int destInputPin) {}
  virtual void onLinkDetached(Node* source, int srcOutputPin, Node* dest, int destInputPin) {}
  virtual std::vector<std::string> const& nodeClassList() { static std::vector<std::string> emptyList = {}; return emptyList; }
};

class NodeIdAllocator
{
  static NodeIdAllocator* instance_;
  size_t                  nextId_ = 0;

public:
  static NodeIdAllocator& instance();
  void                    setInitialId(size_t id) { nextId_ = id; }
  size_t                  newId() { return ++nextId_; }
};

class Node
{
public:
  enum class Type : uint32_t
  {
    NORMAL,     //< normal node
    ANCHOR,     //< an anchor to route links,
                //  implicitly has one input
                //  and infinite number of outputs
    COMMENTBOX, //< comment
    GROUPBOX    //< grouping
  };

private:
  friend class Graph;
  Type           type_       = Type::NORMAL;
  std::string    name_       = "";
  int            numInputs_  = 4;
  int            numOutputs_ = 1;
  glm::vec2      pos_        = {0, 0};
  glm::vec4      color_      = DEFAULT_NODE_COLOR;
  void*          payload_    = nullptr;
  NodeGraphHook* hook_       = nullptr;

public:
  void setHook(NodeGraphHook* hook) { hook_ = hook; }

  void setPayload(void* payload) { payload_ = payload; }

  void* payload() const { return payload_; }

  std::string const& name() const { return name_; }

  void setName(std::string name)
  {
    if (hook_ ? hook_->onNodeNameChanged(this, name) : true)
      name_ = std::move(name);
  }

  glm::vec2 pos() const { return pos_; }

  void setPos(glm::vec2 const& p)
  {
    if (hook_ ? hook_->onNodeMovedTo(this, p) : true)
      pos_ = p;
  }

  glm::vec4 color() const { return color_; }

  void setColor(glm::vec4 const& c)
  {
    color_ = c;
    if (hook_)
      hook_->onNodeColorChanged(this, c);
  }

  Type type() const { return type_; }

  int minInputCount() const
  {
    if (hook_)
      return hook_->getNodeMinInputCount(this);
    return 0;
  }

  int maxInputCount() const
  {
    if (hook_)
      return hook_->getNodeMaxInputCount(this);
    return numInputs_;
  }

  int outputCount() const
  {
    if (hook_)
      return hook_->getNodeOutputCount(this);
    return numOutputs_;
  }

  glm::vec2 size() const
  {
    if (hook_)
      return hook_->getNodeSize(this);
    return glm::vec2(
        std::max<float>(std::max(maxInputCount(), outputCount()) * 10 / 0.9f, DEFAULT_NODE_SIZE.x),
        DEFAULT_NODE_SIZE.y);
  }

  glm::vec2 inputPinPos(int i) const
  {
    if (type() == Type::NORMAL) {
      return glm::vec2((size().x * 0.9f) * float(i + 1) / (maxInputCount() + 1) - size().x * 0.45f,
                       -size().y / 2.f - 4) +
             pos();
    } else {
      return pos();
    }
  }

  glm::vec2 outputPinPos(int i) const
  {
    if (type() == Type::NORMAL) {
      return glm::vec2((size().x * 0.9f) * float(i + 1) / (outputCount() + 1) - size().x * 0.45f,
                       size().y / 2.f + 4) +
             pos();
    } else {
      return pos();
    }
  }

  bool onSelected(GraphView const& gv)
  {
    if (hook_)
      return hook_->onNodeSelected(this, gv);
    return true;
  }

  void onDeselected(GraphView const& gv)
  {
    if (hook_)
      hook_->onNodeDeselected(this, gv);
  }

  void onDraw(GraphView const& gv) const
  {
    if (hook_)
      hook_->onNodeDraw(this, gv);
  }

  void onInspect(GraphView const& gv)
  {
    if (hook_)
      hook_->onNodeInspect(this, gv);
  }
};

class Graph;

struct GraphView
{
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
  };

  glm::vec2 canvasOffset = {0, 0};
  float     canvasScale  = 1;
  bool      drawGrid     = true;
  bool      drawName     = true;
  size_t    hoveredNode  = -1;
  size_t    activeNode   = -1;
  NodePin   hoveredPin   = {NodePin::NONE, size_t(-1), -1};
  NodePin   activePin    = {NodePin::NONE, size_t(-1), -1};

  UIState     uiState           = UIState::VIEWING;
  glm::vec2   selectionBoxStart = {0, 0};
  glm::vec2   selectionBoxEnd   = {0, 0};
  Link        pendingLink       = {{NodePin::OUTPUT, size_t(-1), -1}, {NodePin::INPUT, size_t(-1), -1}};
  std::string pendingNodeClass  = "node";

  glm::vec2 pendingLinkPos;
  std::vector<glm::vec2> linkCuttingStroke;
  std::set<size_t> nodeSelection;

  Graph* graph = nullptr;

  void onGraphChanged(); // callback when graph has changed
};

class Graph
{
protected:
  std::unordered_map<size_t, Node> nodes_;
  // std::vector<Link> links_;
  std::unordered_map<NodePin, NodePin>
      links_; // map from destiny to source, because each input pin accepts one
              // source only, but each output pin can be linked to many input pins
  std::unordered_map<NodePin, std::vector<glm::vec2>>
      linkPathes_; // cached link pathes
  std::vector<size_t>  nodeOrder_;
  std::set<GraphView*> viewers_;
  NodeGraphHook*       hook_    = nullptr;
  void*                payload_ = nullptr;

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

public:
  auto const& nodes() const { return nodes_; }
  auto const& links() const { return links_; }
  auto const& linkPathes() const { return linkPathes_; }
  auto const& order() const { return nodeOrder_; }
  auto const& viewers() const { return viewers_; }

  auto* hook() const { return hook_; }

  void setHook(NodeGraphHook* hook) { hook_ = hook; }

  void* payload() const { return payload_; }

  void setPayload(void* payload) { payload_ = payload; }

  size_t addNode(std::string const& name, glm::vec2 const& pos)
  {
    size_t id = -1;
    if (hook_ ? hook_->nodeCanBeCreated(this, name) : true) {
      id = NodeIdAllocator::instance().newId();
      Node node;
      if (hook_ && hook_->nodeCanBeCreated(this, name)) {
        node.setName(name);
        node.setPayload(hook_->createNode(this, node.name()));
      } else {
        node.name_ = name;
      }
      node.hook_ = hook_;
      node.pos_  = pos;
      nodes_.insert({id, node});
      nodeOrder_.push_back(id);
    }
    return id;
  }

  Node&       noderef(size_t idx) { return nodes_.at(idx); }
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

  std::vector<glm::vec2> genLinkPath(glm::vec2 const& start,
                                     glm::vec2 const& end,
                                     float            avoidenceWidth = DEFAULT_NODE_SIZE.x)
  {
    std::vector<glm::vec2> path;

    float xcenter = (start.x + end.x) * 0.5f;
    float ycenter = (start.y + end.y) * 0.5f;
    float dx      = end.x - start.x;
    float dy      = end.y - start.y;
    auto  sign    = [](float x) { return x > 0 ? 1 : x < 0 ? -1 : 0; };

    if (dy < 42) {
      if (dy < 20 && std::abs(dx) < avoidenceWidth) {
        xcenter += sign(dx) * avoidenceWidth;
      }
      auto endextend = end + glm::vec2(0, -10);
      dy -= 20;

      path.push_back(start);
      path.push_back(start + glm::vec2(0, 10));
      if (abs(dx) > abs(dy) * 2) {
        path.emplace_back(xcenter - sign(dx * dy) * dy / 2, path.back().y);
        path.emplace_back(xcenter + sign(dx * dy) * dy / 2, endextend.y);
      } else {
        path.emplace_back(xcenter, path.back().y);
        path.emplace_back(xcenter, endextend.y);
      }
      path.push_back(endextend);
      path.push_back(end);
    } else {
      path.push_back(start);
      if (dy > abs(dx) + 42) {
        if (dy < 80) {
          path.emplace_back(start.x, ycenter - abs(dx) / 2);
          path.emplace_back(end.x, ycenter + abs(dx) / 2);
        } else {
          path.emplace_back(start.x, end.y - abs(dx) - 20);
          path.emplace_back(end.x, end.y - 20);
        }
      } else {
        path.emplace_back(start.x, start.y + 20);
        if (dy < abs(dx) + 40) {
          path.emplace_back(start.x + sign(dx) * (dy - 40) / 2, ycenter);
          path.emplace_back(end.x - sign(dx) * (dy - 40) / 2, ycenter);
        }
        path.emplace_back(end.x, end.y - 20);
      }
      path.push_back(end);
    }
    return path;
  }

  void updateLinkPath(size_t nodeidx, int ipin = -1)
  {
    if (ipin != -1) {
      auto np      = NodePin{NodePin::INPUT, nodeidx, ipin};
      auto linkitr = links_.find(np);
      if (linkitr != links_.end()) {
        auto const& startnode = nodes_[linkitr->second.nodeIndex];
        auto const& endnode   = nodes_[nodeidx];
        linkPathes_[np]       = genLinkPath(startnode.outputPinPos(linkitr->second.pinNumber),
                                      endnode.inputPinPos(ipin),
                                      std::min(startnode.size().x, endnode.size().x));
      }
    } else {
      for (auto itr = links_.begin(); itr != links_.end(); ++itr) {
        if (itr->first.nodeIndex == nodeidx || itr->second.nodeIndex == nodeidx) {
          auto const& startnode   = nodes_[itr->second.nodeIndex];
          auto const& endnode     = nodes_[itr->first.nodeIndex];
          linkPathes_[itr->first] = genLinkPath(startnode.outputPinPos(itr->second.pinNumber),
                                                endnode.inputPinPos(itr->first.pinNumber),
                                                std::min(startnode.size().x, endnode.size().x));
        }
      }
    }
  }

  void addLink(size_t srcnode, int srcpin, size_t dstnode, int dstpin)
  {
    if (nodes_.find(srcnode) != nodes_.end() && nodes_.find(dstnode) != nodes_.end()) {
      if (hook_ ? hook_->canLinkTo(&noderef(srcnode), srcpin, &noderef(dstnode), dstpin) : true) {
        removeLink(dstnode, dstpin);
        auto dst    = NodePin{NodePin::INPUT, dstnode, dstpin};
        links_[dst] = NodePin{NodePin::OUTPUT, srcnode, srcpin};
        if (hook_) {
          hook_->onLinkAttached(&noderef(srcnode), srcpin, &noderef(dstnode), dstpin);
        }
        updateLinkPath(dstnode, dstpin);
      }
    }
    notifyViewers();
  }

  void removeLink(size_t dstnode, int dstpin)
  {
    auto const np                = NodePin{NodePin::INPUT, dstnode, dstpin};
    auto       originalSourceItr = links_.find(NodePin{NodePin::INPUT, dstnode, dstpin});
    if (originalSourceItr != links_.end()) {
      if (hook_)
        hook_->onLinkDetached(&noderef(originalSourceItr->second.nodeIndex),
                              originalSourceItr->second.pinNumber,
                              &noderef(dstnode),
                              dstpin);
      links_.erase(originalSourceItr);
    }
    linkPathes_.erase(np);
  }

  size_t upstreamNodeOf(size_t nodeidx, int pin)
  {
    auto src = links_.find(NodePin{NodePin::INPUT, nodeidx, pin});
    return src == links_.end() ? -1 : src->second.nodeIndex;
  }

  void removeNode(size_t idx)
  {
    if (hook_ && !hook_->canDeleteNode(&noderef(idx)))
      return;
    for (auto itr = links_.begin(); itr != links_.end();) {
      if (itr->second.nodeIndex == idx || itr->first.nodeIndex == idx) {
        if (hook_) {
          hook_->onLinkDetached(&noderef(itr->second.nodeIndex),
                                itr->second.pinNumber,
                                &noderef(itr->first.nodeIndex),
                                itr->first.pinNumber);
        }
        itr = links_.erase(itr);
      } else {
        ++itr;
      }
    }
    if (hook_) {
      hook_->beforeDeleteNode(&noderef(idx));
    }
    nodes_.erase(idx);
    auto oitr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
    if (oitr != nodeOrder_.end())
      nodeOrder_.erase(oitr);
    notifyViewers();
  }

  template<class Container>
  void removeNodes(Container const& indices)
  {
    for (auto idx : indices) {
      if (hook_ && !hook_->canDeleteNode(&noderef(idx)))
        continue;
      for (auto itr = links_.begin(); itr != links_.end();) {
        if (itr->second.nodeIndex == idx || itr->first.nodeIndex == idx) {
          if (hook_)
            hook_->onLinkDetached(&noderef(itr->second.nodeIndex),
                                  itr->second.pinNumber,
                                  &noderef(itr->first.nodeIndex),
                                  itr->first.pinNumber);
          itr = links_.erase(itr);
        } else {
          ++itr;
        }
      }
      if (hook_) {
        hook_->beforeDeleteNode(&noderef(idx));
      }
      nodes_.erase(idx);
      auto oitr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
      if (oitr != nodeOrder_.end())
        nodeOrder_.erase(oitr);
    }
    notifyViewers();
  }

  template<class Container>
  void moveNodes(Container const& indices, glm::vec2 const& delta)
  {
    for (auto idx : indices) {
      auto& node = noderef(idx);
      node.setPos(node.pos() + delta);
    }
    for (auto idx : indices) {
      updateLinkPath(idx);
    }
  }

  void onNodeClicked(size_t nodeid)
  {
    shiftToEnd(nodeid);
    if (hook_)
      hook_->onNodeClicked(&noderef(nodeid));
  }

  void onNodeDoubleClicked(size_t nodeid)
  {
    shiftToEnd(nodeid);
    if (hook_)
      hook_->onNodeDoubleClicked(&noderef(nodeid));
  }

  std::vector<std::string> const& getNodeClassList() const
  {
    static std::vector<std::string> emptyList = {};
    if (hook_)
      return hook_->nodeClassList();
    else
      return emptyList;
  }
};

void updateAndDraw(GraphView& graph, char const* name);

} // namespace editorui
