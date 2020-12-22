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
    NONE,
    INPUT,
    OUTPUT,
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
  virtual bool onSave(Graph const* host, nlohmann::json& jsobj, std::string const& path) { return false; }

  /// called after the UI graph was loaed
  /// @param host: the graph hosts this hook lives within
  /// @param jsobj: the json section to load
  /// @return: succesfully loaded or not
  virtual bool onLoad(Graph* host, nlohmann::json const& jsobj, std::string const& path) { return false; }

  /// serialize selection of nodes into json
  /// @param host: the graph hosts this hook lives within
  /// @param jsobj: the json section to write to
  /// @param nodeset: the node selection to save
  /// @return: succesfully saved or not
  virtual bool onPartialSave(Graph const* host, nlohmann::json& jsobj, std::set<size_t> const& nodeset) { return true; }

  /// deserialize selection of nodes from json
  /// @param host: the graph hosts this hook lives within
  /// @param jsobj: the json section to load from
  /// @param nodeset: the node selection of given jsobj
  /// @param idmap: map from old nodeid to new node id on partial node
  /// @return: succesfully loaded or not
  virtual bool onPartialLoad( Graph* host,
                              nlohmann::json const& jsobj,
                              std::set<size_t> const& nodeset,
                              std::unordered_map<size_t, size_t> const& idmap)
  { return true; }

  /// creates a new custom graph
  virtual void* createGraph(Graph const* host) { return nullptr; }

  /// creates a new custom node of given type and name
  virtual void* createNode(Graph* host,                 // the graph this node belongs to
                           std::string const& type,     // node's type name
                           std::string const& desiredName, // desired name
                           std::string& acceptedName)   // if the desired name is not legal inside this network,
                                                        // yet can be modified to fit, this outputs the modified
                                                        // and accpted new name
  {
    acceptedName = desiredName;
    return nullptr;
  }

  /// called when trying to change a node's name
  /// @param node: the UI node hosting logical node
  /// @param desiredName: the name we want
  /// @param acceptedName: modified name if desiredName is illegal, but can be modified to be legal
  /// @return: true if this rename is acceptable, else false
  virtual bool onNodeNameChanged(Node const* node, std::string const& desiredName, std::string& acceptedName)
  {
    acceptedName = desiredName;
    return true;
  }

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

  /// called when inspecting datasheet of this node
  virtual void onInspectNodeData(Node* node, GraphView const& gv) {}

  /// called when inspecting summary of whole graph (i.e. when nothing was selected)
  virtual void onInspectGraphSummary(Graph* graph, GraphView const& gv) {}

  virtual bool onNodeSelected(Node const* node, GraphView const& gv) { return true; }
  virtual void onNodeDeselected(Node const* node, GraphView const& gv) { }
  virtual bool onNodeClicked(Node const* node, int mouseButton) { return true; }
  virtual void onNodeHovered(Node const* node) {}
  virtual bool onNodeDoubleClicked(Node const* node, int mouseButton) { return true; }
  virtual void onPinHovered(Node const* node, NodePin const& pin) {}
  virtual bool onNodeMovedTo(Node* node, glm::vec2 const& pos) { return true; }
  virtual bool nodeCanBeDeleted(Node* node) { return true; }
  virtual void beforeDeleteNode(Node* node) {}
  virtual void beforeDeleteGraph(Graph* host) {}
  virtual bool linkCanBeAttached(Node* source, int srcOutputPin, Node* dest, int destInputPin)
  {
    return true;
  }
  virtual void onLinkAttached(Node* source, int srcOutputPin, Node* dest, int destInputPin) {}
  virtual void onLinkDetached(Node* source, int srcOutputPin, Node* dest, int destInputPin) {}
  virtual std::vector<std::string> nodeClassList() { return {}; }
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
                //  (TODO: anchors)
  };

private:
  friend class Graph;
  Type           type_        = Type::NORMAL;
  std::string    initialName_ = "";
  std::string    displayName_ = "";
  int            numInputs_   = 4;
  int            numOutputs_  = 1;
  glm::vec2      pos_         = {0, 0};
  glm::vec4      color_       = DEFAULT_NODE_COLOR;
  void*          payload_     = nullptr;
  NodeGraphHook* hook_        = nullptr;

public:
  void setHook(NodeGraphHook* hook) { hook_ = hook; }

  void setPayload(void* payload) { payload_ = payload; }

  void* payload() const { return payload_; }

  std::string const& initialName() const { return initialName_; }

  std::string const& displayName() const { return displayName_; }

  void setDisplayName(std::string name)
  {
    if (hook_ ? hook_->onNodeNameChanged(this, displayName_, name) : true)
      displayName_ = std::move(name);
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

  void onInspectData(GraphView const& gv)
  {
    if (hook_)
      hook_->onInspectNodeData(this, gv);
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
  glm::vec2 canvasSize   = {0, 0};
  float     canvasScale  = 1;
  glm::mat3 canvasToScreen = { 1,0,0, 0,1,0, 0,0,1 };
  glm::mat3 screenToCanvas = { 1,0,0, 0,1,0, 0,0,1 };
  bool      drawGrid     = true;
  bool      drawName     = true;
  bool      showNetwork  = true;
  bool      showInspector = true;
  bool      showDatasheet = true;
  size_t    hoveredNode  = -1;
  size_t    activeNode   = -1;
  NodePin   hoveredPin   = {NodePin::NONE, size_t(-1), -1};
  NodePin   activePin    = {NodePin::NONE, size_t(-1), -1};

  UIState     uiState           = UIState::VIEWING;
  glm::vec2   selectionBoxStart = {0, 0};
  glm::vec2   selectionBoxEnd   = {0, 0};
  Link        pendingLink       = {{NodePin::NONE, size_t(-1), -1}, {NodePin::NONE, size_t(-1), -1}};
  std::string pendingNodeClass  = "node";

  glm::vec2 pendingLinkPos;
  std::vector<glm::vec2> linkCuttingStroke;
  std::set<size_t> nodeSelection;

  Graph* graph = nullptr;
  size_t id = 0;
  bool   windowSetupDone = false;

  void onGraphChanged(); // callback when graph has changed
  void copy();           // copy selection to clipboard
  bool paste();          // paste content in clipboard to this graph
};

struct CommentBox
{
  glm::vec2   pos = { 0,0 };
  glm::vec2   size = { 100,100 };
  glm::vec4   color = { 1,1,1,1 };
  std::string title = "";
  std::string text = "";
};

class Graph
{
protected:
  std::unordered_map<size_t, Node> nodes_;
  std::unordered_map<NodePin, NodePin>
      links_; // map from destiny to source, because each input pin accepts one
              // source only, but each output pin can be linked to many input pins
  std::unordered_map<NodePin, std::vector<glm::vec2>>
      linkPathes_; // cached link pathes
  std::vector<size_t>  nodeOrder_;
  std::vector<CommentBox> comments_; // TODO: comments
  std::vector<GraphView*> viewers_;
  NodeGraphHook*       hook_    = nullptr;
  void*                payload_ = nullptr;
  size_t               nextViewerId_ = 0;

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
  ~Graph()
  {
    for (auto* v : viewers_)
      delete v;
  }

  auto const& nodes() const { return nodes_; }
  auto&       nodes() { return nodes_; }
  auto const& links() const { return links_; }
  auto const& linkPathes() const { return linkPathes_; }
  auto const& order() const { return nodeOrder_; }
  auto const& viewers() const { return viewers_; }

  auto* hook() const { return hook_; }

  void setHook(NodeGraphHook* hook) { hook_ = hook; }

  void* payload() const { return payload_; }

  void setPayload(void* payload) { payload_ = payload; }

  size_t addNode(std::string const& name, std::string const& desiredName, glm::vec2 const& pos, void* payload=nullptr)
  {
    size_t id = -1;
    std::string dispName = desiredName;
    void* nodepayload = payload
      ? payload
      : hook_
        ? hook_->createNode(this, name, desiredName, dispName)
        : nullptr;
    if (hook_ ? !!nodepayload : true) {
      id = NodeIdAllocator::instance().newId();
      Node node;
      node.initialName_ = name;
      node.displayName_ = dispName;
      node.pos_         = pos;
      node.hook_        = hook_;
      node.setPayload(nodepayload);
      nodes_.insert({id, node});
      nodeOrder_.push_back(id);
    }
    return id;
  }

  Node&       noderef(size_t idx) { return nodes_.at(idx); }
  Node const& noderef(size_t idx) const { return nodes_.at(idx); }
  auto const& linkPath(NodePin const& pin) const { return linkPathes_.at(pin); }

  void addViewer()
  {
    GraphView* view = new GraphView;
    view->graph = this;
    view->onGraphChanged();
    view->id = ++nextViewerId_;
    viewers_.push_back(view);
  }

  void removeViewer(GraphView const* view)
  {
    auto itr = std::find(viewers_.begin(), viewers_.end(), view);
    if (itr != viewers_.end()) {
      viewers_.erase(itr);
      delete view;
    } else {
      // spdlog::error("graphview {} does not belong to graph {}", view, this);
      assert(!"graphview does not belong to this graph");
    }
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
      if (dy < 20 && std::fabs(dx) < avoidenceWidth) {
        xcenter += sign(dx) * avoidenceWidth;
      }
      auto endextend = end + glm::vec2(0, -10);
      dy -= 20;

      path.push_back(start);
      path.push_back(start + glm::vec2(0, 10));
      if (fabs(dx) > fabs(dy) * 2) {
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
      if (dy > fabs(dx) + 42) {
        if (dy < 80) {
          path.emplace_back(start.x, ycenter - fabs(dx) / 2);
          path.emplace_back(end.x, ycenter + fabs(dx) / 2);
        } else {
          path.emplace_back(start.x, end.y - fabs(dx) - 20);
          path.emplace_back(end.x, end.y - 20);
        }
      } else {
        path.emplace_back(start.x, start.y + 20);
        if (dy < fabs(dx) + 40) {
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

  void addLink(size_t srcnode, int srcpin, size_t dstnode, int dstpin, bool bypassHook=false)
  {
    if (nodes_.find(srcnode) != nodes_.end() && nodes_.find(dstnode) != nodes_.end()) {
      if ((hook_ && !bypassHook)
          ? hook_->linkCanBeAttached(&noderef(srcnode), srcpin, &noderef(dstnode), dstpin)
          : true) {
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

  void removeLink(size_t dstnode, int dstpin, bool bypassHook=false)
  {
    auto const np                = NodePin{NodePin::INPUT, dstnode, dstpin};
    auto       originalSourceItr = links_.find(NodePin{NodePin::INPUT, dstnode, dstpin});
    if (originalSourceItr != links_.end()) {
      if (hook_ && !bypassHook)
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

  void removeNode(size_t idx, bool bypassHook=false)
  {
    if (hook_ && !bypassHook && !hook_->nodeCanBeDeleted(&noderef(idx)))
      return;
    for (auto itr = links_.begin(); itr != links_.end();) {
      if (itr->second.nodeIndex == idx || itr->first.nodeIndex == idx) {
        if (hook_ && !bypassHook) {
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
    if (hook_ && !bypassHook) {
      hook_->beforeDeleteNode(&noderef(idx));
    }
    nodes_.erase(idx);
    auto oitr = std::find(nodeOrder_.begin(), nodeOrder_.end(), idx);
    if (oitr != nodeOrder_.end())
      nodeOrder_.erase(oitr);
    notifyViewers();
  }

  template<class Container>
  void removeNodes(Container const& indices, bool bypassHook=false)
  {
    for (auto idx : indices) {
      if (hook_ && !bypassHook && !hook_->nodeCanBeDeleted(&noderef(idx)))
        continue;
      for (auto itr = links_.begin(); itr != links_.end();) {
        if (itr->second.nodeIndex == idx || itr->first.nodeIndex == idx) {
          if (hook_ && !bypassHook)
            hook_->onLinkDetached(&noderef(itr->second.nodeIndex),
                                  itr->second.pinNumber,
                                  &noderef(itr->first.nodeIndex),
                                  itr->first.pinNumber);
          itr = links_.erase(itr);
        } else {
          ++itr;
        }
      }
      if (hook_ && !bypassHook) {
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

  void onNodeHovered(size_t nodeid)
  {
    if (hook_)
      hook_->onNodeHovered(&noderef(nodeid));
  }

  void onPinHovered(NodePin const& pin)
  {
    if (hook_)
      hook_->onPinHovered(&noderef(pin.nodeIndex), pin);
  }

  void onNodeClicked(size_t nodeid, int mouseButton)
  {
    shiftToEnd(nodeid);
    if (hook_)
      hook_->onNodeClicked(&noderef(nodeid), mouseButton);
  }

  void onNodeDoubleClicked(size_t nodeid, int mouseButton)
  {
    shiftToEnd(nodeid);
    if (hook_)
      hook_->onNodeDoubleClicked(&noderef(nodeid), mouseButton);
  }

  void onInspectSummary(GraphView const& gv)
  {
    if (hook_)
      hook_->onInspectGraphSummary(this, gv);
  }

  std::vector<std::string> getNodeClassList() const
  {
    if (hook_)
      return hook_->nodeClassList();
    else
      return {};
  }
  
  // save & load a selection of nodes
  // used for copy / pasting
  // and (maybe) undo / redo
  bool partialSave(nlohmann::json& json, std::set<size_t> const& nodes);
  bool partialLoad(nlohmann::json const& json, std::set<size_t> *outPastedNodes=nullptr);

  bool save(nlohmann::json& section, std::string const& path);
  bool load(nlohmann::json const& section, std::string const& path);
};

void init(); // TODO: config?
void edit(Graph& graph, char const* name);
void deinit();

} // namespace editorui
