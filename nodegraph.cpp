#include "nodegraph.h"

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <nfd.h>

#include <glm/ext.hpp>
#include <glm/gtx/color_space.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/vec_swizzle.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <cstdlib>
#include <memory>

// --------------------------------------------------------------------
//                        T O D O   L I S T :
// --------------------------------------------------------------------
// [~] host real(logical) nodes & graphs
// [ ] dive in nested network
// [~] serialization
// [~] edit common params in inspector when multiple nodes are selected
// [ ] long input list
// [X] data inspector
// [ ] bypass flag
// [ ] output flag
// [X] display names inside network
// [X] name font scale
// [X] drag link body to re-route
// [X] highlight hovering pin
// [X] optimize link routing
// [X] focus to selected nodes / frame all nodes
// [X] copy / paste
// [~] undo / redo / edit history tree
// [ ] node shape
// [ ] read-only view
// [ ] editing policy / limited allowed action
// [ ] syntax-highlighting text editor
// [~] window management
// [ ] grid snapping
// [ ] config-able appearance
// [X] tab menu filter & completion
// [X] link swaping
// [ ] node swaping
// [X] drag link head/tail to empty space creates new node
// [ ] comment box
// [ ] group box
// [ ] anchor node

// helpers {{{
static inline ImVec2 operator*(const glm::mat3& m, const ImVec2& v)
{
  auto r = m * glm::vec3(v.x, v.y, 1.0f);
  return ImVec2(r.x, r.y);
}
template<class Vec2>
static inline bool ccw(const Vec2& a, const Vec2& b, const Vec2& c)
{
  auto const ab = b - a, ac = c - a;
  return glm::cross(glm::vec3(ab.x, ab.y, 0.f), glm::vec3(ac.x, ac.y, 0.f)).z > 0;
}
template<class Vec2>
static inline float dot(const Vec2& a, const Vec2& b)
{
  return a.x * b.x + a.y * b.y;
}
static inline float length(const ImVec2& v)
{
  return sqrt(v.x * v.x + v.y * v.y);
}
static inline float cornerRounding(float r)
{
  return r > 2 ? r : 0.f;
}
static inline glm::vec4 highlight(glm::vec4 const& color,
                                  float            dSat   = 0.1f,
                                  float            dLum   = 0.1f,
                                  float            dAlpha = 0.1f)
{
  auto hsv = glm::hsvColor(glm::xyz(color));
  return glm::vec4(
      glm::rgbColor(glm::clamp(glm::vec3(hsv.x, hsv.y * (1.0f + dSat), hsv.z * (1.0f + dLum)),
                               glm::vec3{0, 0, 0},
                               glm::vec3{360, 1, 1})),
      glm::clamp(color.a * (1.0f + dAlpha)));
}
static inline ImVec2 imvec(glm::vec2 const& v)
{
  return {v.x, v.y};
}
static inline glm::vec2 glmvec(ImVec2 const& v)
{
  return {v.x, v.y};
}
static inline ImU32 imcolor(glm::vec4 const& color)
{
  return ImGui::ColorConvertFloat4ToU32(ImVec4{color.r, color.g, color.b, color.a});
}

template<class Vec2 = ImVec2>
struct AABB
{
  Vec2 min, max;

  AABB(Vec2 const& a) { min = max = a; }
  AABB(Vec2 const& a, Vec2 const& b) : AABB(a) { merge(b); }
  static AABB fromCenterAndSize(Vec2 const& center, Vec2 const& size)
  {
    return AABB(center - size * 0.5f, center + size * 0.5f);
  }
  void merge(Vec2 const& v)
  {
    min.x = std::min(min.x, v.x);
    min.y = std::min(min.y, v.y);
    max.x = std::max(max.x, v.x);
    max.y = std::max(max.y, v.y);
  }
  void merge(AABB const& aabb)
  {
    min.x = std::min(min.x, aabb.min.x);
    min.y = std::min(min.y, aabb.min.y);
    max.x = std::max(max.x, aabb.max.x);
    max.y = std::max(max.y, aabb.max.y);
  }
  void expand(float amount)
  {
    min.x -= amount;
    min.y -= amount;
    max.x += amount;
    max.y += amount;
  }
  AABB expanded(float amount) const
  {
    AABB ex = *this;
    ex.expand(amount);
    return ex;
  }
  Vec2 center() const
  {
    return Vec2((min.x + max.x) / 2, (min.y + max.y) / 2);
  }
  Vec2 size() const
  {
    return Vec2(max.x - min.x, max.y - min.y);
  }
  // test if the that is contained inside this
  bool contains(AABB const& that) const
  {
    return min.x <= that.min.x && min.y <= that.min.y && max.x >= that.max.x &&
           max.y >= that.max.y;
  }
  // test if point is inside this
  bool contains(Vec2 const& pt) const
  {
    return pt.x <= max.x && pt.y <= max.y && pt.x >= min.x && pt.y >= min.y;
  }
  // test if the two AABBs has intersection
  bool intersects(AABB const& that) const
  {
    return !(max.x < that.min.x || that.max.x < min.x || max.y < that.min.y || that.max.y < min.y);
  }
};

static std::vector<ImVec2> transform(std::vector<glm::vec2> const& src, glm::mat3 const& mat)
{
  std::vector<ImVec2> result(src.size());
  std::transform(src.begin(), src.end(), result.begin(), [&mat](glm::vec2 const& v) {
    auto t = mat * glm::vec3(v, 1);
    return ImVec2(t.x, t.y);
  });
  return result;
}

template<class Vec2>
static bool strokeIntersects(std::vector<Vec2> const& a, std::vector<Vec2> const& b)
{
  // TODO: implement sweep line algorithm
  for (size_t i = 1; i < a.size(); ++i) {
    for (size_t j = 1; j < b.size(); ++j) {
      if (AABB<Vec2>(a[i - 1], a[i]).intersects(AABB<Vec2>(b[j - 1], b[j]))) {
        if (ccw(a[i - 1], b[j - 1], b[j]) == ccw(a[i], b[j - 1], b[j]))
          continue;
        else if (ccw(a[i - 1], a[i], b[j - 1]) == ccw(a[i - 1], a[i], b[j]))
          continue;
        else
          return true;
      }
    }
  }
  return false;
}

template<class Vec2>
static float pointSegmentDistance(Vec2 const& pt,
                                  Vec2 const& segStart,
                                  Vec2 const& segEnd,
                                  Vec2*       outClosestPoint = nullptr)
{
  auto  direction = segEnd - segStart;
  auto  diff      = pt - segEnd;
  float t         = dot(direction, diff);
  Vec2  closept;
  if (t >= 0.f) {
    closept = segEnd;
  } else {
    diff = pt - segStart;
    t    = dot(direction, diff);
    if (t <= 0.f) {
      closept = segStart;
    } else {
      auto sqrLength = dot(direction, direction);
      if (sqrLength > 0.f) {
        t /= sqrLength;
        closept = segStart + direction * t;
      } else {
        closept = segStart;
      }
    }
  }
  diff = pt - closept;
  if (outClosestPoint)
    *outClosestPoint = closept;
  return length(diff);
}

static ptrdiff_t longestCommonSequenceLength(std::string const& a, std::string const& b)
{
  std::string const& from = a.length() > b.length() ? b : a;
  std::string const& to = a.length() > b.length() ? a : b;
  std::vector<ptrdiff_t> buf1(from.length()+1, 0);
  std::vector<ptrdiff_t> buf2(from.length()+1, 0);
  for (size_t i=1; i<=to.length(); ++i) {
    for (size_t j=1, n=from.length(); j<=n; ++j) {
      buf2[j] = (from[j-1] == to[i-1] ? buf1[j-1]+1 : std::max(buf1[j], buf2[j - 1]));
    }
    buf1.swap(buf2);
  }
  return buf1.back();
}
// helpers }}}

// font data:
const unsigned int  get_roboto_medium_compressed_size();
const unsigned int* get_roboto_medium_compressed_data();
const unsigned int  get_sourcecodepro_compressed_size();
const unsigned int* get_sourcecodepro_compressed_data();

namespace editorui {

static auto& globalConfig()
{
  static struct {
    struct {
      ImFont* defaultFont = nullptr;
      ImFont* strongFont = nullptr;
      ImFont* largeFont = nullptr;
      ImFont* largeStrongFont = nullptr;
      ImFont* monoFont = nullptr;
      ImFont* stdIconFont = nullptr;
      ImFont* largeIconFont = nullptr;
    } fonts;
  } config;
  return config;
}

#include "fa_solid.hpp"
#include "fa_icondef.h"

void init()
{
  auto* atlas = ImGui::GetIO().Fonts;
  auto* font  = atlas->AddFontFromMemoryCompressedTTF(get_roboto_medium_compressed_data(), get_roboto_medium_compressed_size(), 14, nullptr, atlas->GetGlyphRangesCyrillic());

  globalConfig().fonts.defaultFont = font;
  globalConfig().fonts.strongFont = font;

  auto* largeFont = font = atlas->AddFontFromMemoryCompressedTTF(get_roboto_medium_compressed_data(), get_roboto_medium_compressed_size(), 28, nullptr, atlas->GetGlyphRangesCyrillic());

  globalConfig().fonts.largeFont = largeFont;
  globalConfig().fonts.largeStrongFont = largeFont;

  static const ImWchar rangesIcons[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
  globalConfig().fonts.stdIconFont = atlas->AddFontFromMemoryCompressedTTF(FontAwesomeSolid_compressed_data, FontAwesomeSolid_compressed_size, 16.8, nullptr, rangesIcons);
  globalConfig().fonts.largeIconFont = atlas->AddFontFromMemoryCompressedTTF(FontAwesomeSolid_compressed_data, FontAwesomeSolid_compressed_size, 42, nullptr, rangesIcons);

  // TODO: read config file
  std::ifstream test_file_readable("res/sarasa-mono-sc-regular.ttf");
  if (test_file_readable.good())
    globalConfig().fonts.monoFont = atlas->AddFontFromFileTTF("res/sarasa-mono-sc-regular.ttf", 14, nullptr, atlas->GetGlyphRangesChineseSimplifiedCommon());
  if (!globalConfig().fonts.monoFont) {
    globalConfig().fonts.monoFont = atlas->AddFontFromMemoryCompressedTTF(get_sourcecodepro_compressed_data(), get_sourcecodepro_compressed_size(), 14, nullptr, atlas->GetGlyphRangesChineseSimplifiedCommon());
  }
}

void deinit()
{
  // TODO: deinit
}

FontScope::FontScope(FontScope::Font f)
{
  switch (f) {
  case FontScope::MONOSPACE:
    ImGui::PushFont(globalConfig().fonts.monoFont);
    break;
  case FontScope::LARGE:
    ImGui::PushFont(globalConfig().fonts.largeFont);
    break;
  case FontScope::LARGESTRONG:
    ImGui::PushFont(globalConfig().fonts.largeStrongFont);
    break;
  case FontScope::STRONG:
    ImGui::PushFont(globalConfig().fonts.strongFont);
    break;
  case FontScope::ICON:
    ImGui::PushFont(globalConfig().fonts.stdIconFont);
    break;
  case FontScope::LARGEICON:
    ImGui::PushFont(globalConfig().fonts.largeIconFont);
    break;
  case FontScope::REGULAR:
  default:
    ImGui::PushFont(globalConfig().fonts.defaultFont);
    break;
  }
}

FontScope::~FontScope()
{
  ImGui::PopFont();
}

NodeIdAllocator* NodeIdAllocator::instance_ = nullptr;
NodeIdAllocator& NodeIdAllocator::instance()
{
  static std::unique_ptr<NodeIdAllocator> s_instance(new NodeIdAllocator);
  if (instance_ == nullptr)
    instance_ = s_instance.get();
  return *s_instance;
}

void GraphView::onGraphChanged()
{
  if (graph) {
    if (!nodeSelection.empty()) {
      std::vector<size_t> invalidIndices;
      for (size_t idx : nodeSelection) {
        if (graph->nodes().find(idx) == graph->nodes().end()) {
          invalidIndices.push_back(idx);
        }
      }
      for (size_t idx : invalidIndices)
        nodeSelection.erase(idx);
    }
    if (activeNode != -1 && graph->nodes().find(activeNode) != graph->nodes().end())
      activeNode = -1;
    if (graph->nodes().find(focusingNode) == graph->nodes().end()) {
      if (kind == Kind::INSPECTOR)
        showInspector = false;
      if (kind == Kind::DATASHEET)
        showDatasheet = false;
    }
  }
}

void GraphView::copy()
{
  if (!nodeSelection.empty()) {
    nlohmann::json json;
    if (graph && graph->partialSave(json, nodeSelection)) {
      ImGui::SetClipboardText(json.dump().c_str());
    }
  }
}

bool GraphView::paste()
{
  auto cb = ImGui::GetClipboardText();
  if (!cb) {
    spdlog::warn("nothing to paste");
    return false;
  }
  try {
    auto json = nlohmann::json::parse(cb);
    if (!json.is_object())
      return false;
    return graph && graph->partialLoad(json, &nodeSelection);
  } catch(nlohmann::json::parse_error const& e) {
    spdlog::warn("json parse error: {}", e.what());
    return false;
  }
}

// TODO: too naive, refactor this
class UndoStackImpl : public UndoStack
{
  std::vector<nlohmann::json> history_;
  ptrdiff_t                   cursor_=-1;
public:
  bool stash(Graph const& g) override
  {
    if (cursor_ + 1 < ptrdiff_t(history_.size())) // 0 reserved
      history_.resize(cursor_ + 1);
    if (g.save(history_.emplace_back(), "")) {
      ++cursor_;
      return true;
    }
    return false;
  }
  bool undo(Graph& g) override
  {
    if (history_.empty() || cursor_ < 1)
      return false;
    --cursor_;
    assert(cursor_<history_.size());
    return g.load(history_[cursor_], "");
  }
  bool redo(Graph& g) override
  {
    if (history_.empty() || cursor_ + 1 >= ptrdiff_t(history_.size()))
      return false;
    ++cursor_;
    return g.load(history_[cursor_], "");
  }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::vec4, x, y, z, w);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::vec3, x, y, z);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::vec2, x, y);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NodePin, type, nodeIndex, pinNumber);

bool Graph::partialSave(nlohmann::json& json, std::set<size_t> const& nodes)
{
  auto& uigraph = json["uigraph"];
  auto& nodesection = uigraph["nodes"];
  for (size_t id : nodes) {
    auto const& node = noderef(id);
    nlohmann::json nodedef;
    nodedef["id"] = id;
    nodedef["initialName"] = node.initialName();
    nodedef["displayName"] = node.displayName();
    nodedef["minInputs"] = node.minInputCount();
    nodedef["maxInputs"] = node.maxInputCount();
    nodedef["nOutputs"] = node.outputCount();
    to_json(nodedef["color"], node.color());
    to_json(nodedef["pos"], node.pos());
    nodesection.push_back(nodedef);
  }
  auto& linksection = uigraph["links"];
  for (auto const& link : links_) {
    if (nodes.find(link.first.nodeIndex) != nodes.end()/* || nodes.find(link.second.nodeIndex) != nodes.end()*/) {
      nlohmann::json linkdef;
      linkdef["from"] = link.second;
      linkdef["fromname"] = noderef(link.second.nodeIndex).displayName();
      linkdef["to"] = link.first;
      linksection.push_back(linkdef);
    }
  }
  if (hook_)
    return hook_->onPartialSave(this, json, nodes);
  return true;
}

bool Graph::partialLoad(nlohmann::json const& json, std::set<size_t> *outPastedNodes)
{
  if (!json.is_object() || json.find("uigraph") == json.end())
    return false;
  auto const& uigraph = json["uigraph"];
  std::unordered_map<size_t, size_t> idMap;
  for (auto const& nodedef: uigraph["nodes"]) {
    glm::vec2 pos;
    from_json(nodedef["pos"], pos);
    auto newid = addNode(nodedef["initialName"], nodedef["displayName"], pos + glm::vec2(100, 100));
    Node& node = noderef(newid);
    node.numInputs_ = nodedef["maxInputs"];
    node.numOutputs_ = nodedef["nOutputs"];
    from_json(nodedef["color"], node.color_);

    idMap[nodedef["id"]] = newid;
  }
  auto transpin = [&idMap](NodePin const& pin) {
    auto itr = idMap.find(pin.nodeIndex);
    return NodePin{ pin.type, itr == idMap.end() ? pin.nodeIndex : itr->second, pin.pinNumber };
  };
  for (auto const& linkdef: uigraph["links"]) {
    auto to = transpin(linkdef["to"]);
    NodePin from = linkdef["from"];
    // auto from = transpin(linkdef["from"]);
    if (auto itr = idMap.find(from.nodeIndex); itr!=idMap.end())
      from.nodeIndex = itr->second;
    // resolve link by display name - for copy-pasting between totally different graphs
    // where ids makes no sense
    else if (linkdef.find("fromname")!=linkdef.end()) {
      if (auto sourceitr=std::find_if(
            nodes_.begin(), nodes_.end(),
            [name=std::string(linkdef["fromname"])](auto const& pair) {
              return pair.second.displayName()==name;
            });
          sourceitr!=nodes_.end())
      {
        from.nodeIndex = sourceitr->first;
      }
    }
    if (nodes_.find(to.nodeIndex) != nodes_.end() && nodes_.find(from.nodeIndex) != nodes_.end())
      addLink(from.nodeIndex, from.pinNumber, to.nodeIndex, to.pinNumber);
  }
  for (auto const& newitem: idMap) {
    updateLinkPath(newitem.second);
  }

  std::set<size_t> newNodes;
  newNodes.clear();
  for (auto const& newitem : idMap) {
    newNodes.insert(newitem.second);
  }
  if (outPastedNodes) {
    *outPastedNodes = newNodes;
  }
  bool succeed = true;
  if (hook_)
    succeed &= hook_->onPartialLoad(this, json, newNodes, idMap);

  this->notifyViewers();
  stash();
  return succeed;
}

bool Graph::save(nlohmann::json& section, std::string const& path) const
{
  auto& uigraph = section["uigraph"];
  auto& nodesection = uigraph["nodes"];
  for (auto const& n : nodes_) {
    nlohmann::json nodedef;
    nodedef["id"] = n.first;
    nodedef["initialName"] = n.second.initialName();
    nodedef["displayName"] = n.second.displayName();
    nodedef["minInputs"] = n.second.minInputCount();
    nodedef["maxInputs"] = n.second.maxInputCount();
    nodedef["nOutputs"] = n.second.outputCount();
    to_json(nodedef["color"], n.second.color());
    to_json(nodedef["pos"],   n.second.pos());

    nodesection.push_back(nodedef);
  }
  auto& linksection = uigraph["links"];
  for (auto const& link: links_) {
    nlohmann::json linkdef;
    linkdef["from"] = { {"node", link.second.nodeIndex}, {"pin", link.second.pinNumber} };
    linkdef["to"] = { {"node", link.first.nodeIndex}, {"pin", link.first.pinNumber} };

    linksection.push_back(linkdef);
  }
  uigraph["order"] = nodeOrder_;

  if (hook_) {
    hook_->onSave(this, section, path);
  }
  if (!path.empty())
    savePath_ = path;

  return true;
}

static void focusSelected(GraphView& gv);
bool Graph::load(nlohmann::json const& section, std::string const& path)
{
  if (hook_) {
    for (auto& n : nodes_) {
      hook_->beforeDeleteNode(&n.second);
    }
  }
  nodes_.clear();
  links_.clear();
  linkPathes_.clear();
  nodeOrder_.clear();

  auto const& uigraph = section["uigraph"];
  size_t maxNodeId = 0;
  for (auto const& n: uigraph["nodes"]) {
    Node node;
    node.initialName_ = n["initialName"];
    node.displayName_ = n["displayName"];
    node.numInputs_ = n["maxInputs"];
    node.numOutputs_ = n["nOutputs"];
    node.hook_ = nullptr; // Hooks are processed later
    from_json(n["color"], node.color_);
    from_json(n["pos"], node.pos_);

    size_t id = n["id"];
    nodes_[id] = node;

    maxNodeId = std::max(id, maxNodeId);
  }
  NodeIdAllocator::instance().setInitialId(maxNodeId+1);
  for (auto const& link: uigraph["links"]) {
    links_[NodePin{ NodePin::INPUT,link["to"]["node"], link["to"]["pin"] }] = NodePin{ NodePin::OUTPUT, link["from"]["node"], link["from"]["pin"] };
  }
  if (uigraph.find("order")!=uigraph.end()) {
    for (size_t id : uigraph["order"]) {
      nodeOrder_.push_back(id);
    }
  } else {
    for (auto const& n : nodes_)
      nodeOrder_.push_back(n.first);
  }

  for(auto const& n: nodes_)
    updateLinkPath(n.first);

  bool succeed = true;
  if(hook_) {
    succeed &= hook_->onLoad(this, section, path);
  }
  this->notifyViewers();
  if (!path.empty()) {
    undoStack_.reset(nullptr);
    stash();
    savePath_ = path;
    for (auto *v: viewers_) {
      focusSelected(*v);
    }
  }
  return succeed;
}

bool Graph::stash()
{
  if (!undoStack_)
    undoStack_.reset(new UndoStackImpl());
  return undoStack_->stash(*this);
}

bool Graph::undo()
{
  if (!undoStack_)
    return false;
  return undoStack_->undo(*this);
}

bool Graph::redo()
{
  if (!undoStack_)
    return false;
  return undoStack_->redo(*this);
}

////////////////////////////////////////////////////////////////////////////////////////


void updateInspectorView(GraphView& gv, char const* name)
{
  if (!gv.showInspector)
    return;
  ImGui::SetNextWindowSize(ImVec2{320, 480}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(name, &gv.showInspector)) {
    ImGui::End();
    return;
  }
  auto inspect = [&gv](size_t id)
  {
    auto& node = gv.graph->noderef(id);
    // ImGui::Text(node->name.c_str());
    char namebuf[512] = { 0 };
    memcpy(namebuf, node.displayName().c_str(), std::min(sizeof(namebuf), node.displayName().size()));
    if (ImGui::InputText("Name##nodename",
      namebuf,
      sizeof(namebuf),
      ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue))
      node.setDisplayName(namebuf);
    // if (ImGui::SliderInt("Number of Inputs", &node.maxInputCount(), 0, 20))
    //  gv.graph->updateLinkPath(id);
    // if (ImGui::SliderInt("Number of Outputs", &node.outputCount(), 0, 20))
    //  gv.graph->updateLinkPath(id);
    auto color = node.color();
    if (ImGui::ColorEdit4("Color", &color.r, ImGuiColorEditFlags_PickerHueWheel))
      node.setColor(color);

    ImGui::Separator();

    if(node.onInspect(gv))
      gv.graph->stash();
  };
  if (gv.focusingNode != -1)
    inspect(gv.focusingNode);
  else {
    if (gv.nodeSelection.empty()) {
      ImGui::Text("Nothing selected");
    } else if (gv.nodeSelection.size() == 1) {
      auto  id = *gv.nodeSelection.begin();
      if (id != -1)
        inspect(id);
    } else {
      glm::vec4 avgColor = { 0, 0, 0, 0 };
      for (auto id : gv.nodeSelection) {
        avgColor += gv.graph->noderef(id).color();
      }
      avgColor /= float(gv.nodeSelection.size());
      if (ImGui::ColorPicker4("Color", &avgColor.r, ImGuiColorEditFlags_PickerHueWheel)) {
        for (auto id : gv.nodeSelection) {
          gv.graph->noderef(id).setColor(avgColor);
        }
        gv.graph->stash();
      }
      // TODO: multi-editing
    }
  }

  ImGui::End();
}

static glm::mat3 calcToScreenMatrix(GraphView const& gv, AABB<ImVec2> const& scrArea)
{
  glm::mat3 scaleMat =
      glm::scale(glm::identity<glm::mat3>(), glm::vec2(gv.canvasScale, gv.canvasScale));
  glm::mat3 translateMat = glm::translate(glm::identity<glm::mat3>(), gv.canvasOffset);
  glm::mat3 windowTranslate =
      glm::translate(glm::identity<glm::mat3>(),
                     glm::vec2(scrArea.center().x, scrArea.center().y));
  return windowTranslate * scaleMat * translateMat;
}

static int circleSegs(float radius)
{
  static const int lut[] = {
  //0, 1, 2, 3, 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
    4, 4, 6, 6, 7,  8,  9,  9,  9, 10, 10, 12, 12, 13, 13, 14
  };
  if (int(radius)<sizeof(lut)/sizeof(lut[0])) {
    return lut[int(radius)];
  } else {
    return std::max(36, int(radius));
  }
}

void drawGraph(GraphView const& gv, std::set<size_t> const& unconfirmedNodeSelection)
{
  // Draw Nodes
  ImU32 const PENDING_PLACE_NODE_COLOR = IM_COL32(160, 160, 160, 64);
  ImU32 const SELECTION_BOX_COLOR      = IM_COL32(60, 110, 60, 128);
  ImU32 const DESELECTION_BOX_COLOR    = IM_COL32(140, 60, 60, 128);

  ImVec2 const canvasSize         = ImGui::GetWindowSize();
  ImVec2 const mousePos           = ImGui::GetMousePos();
  ImVec2 const winPos             = ImGui::GetCursorScreenPos();
  ImVec2 const mouseDelta         = ImGui::GetIO().MouseDelta;
  auto const   canvasScale        = gv.canvasScale;
  auto const   canvasOffset       = gv.canvasOffset;
  auto const   canvasArea         = AABB<ImVec2>(winPos, winPos + canvasSize);
  bool const   cursorInsideCanvas = canvasArea.contains(mousePos);

  glm::mat3 const& toScreen = gv.canvasToScreen;
  glm::mat3 const& toCanvas = gv.screenToCanvas;

  ImDrawList* drawList = ImGui::GetWindowDrawList();

  // Grid
  float const GRID_SZ    = 32.0f;
  ImU32 const GRID_COLOR = IM_COL32(80, 80, 80, 40);
  if (gv.drawGrid && GRID_SZ * canvasScale >= 8.f) {
    auto gridOffset = toScreen * glm::vec3(0, 0, 1);
    for (float x = fmodf(gridOffset.x - winPos.x, GRID_SZ * canvasScale); x < canvasSize.x;
         x += GRID_SZ * canvasScale)
      drawList->AddLine(ImVec2(x, 0) + winPos, ImVec2(x, canvasSize.y) + winPos, GRID_COLOR);
    for (float y = fmodf(gridOffset.y - winPos.y, GRID_SZ * canvasScale); y < canvasSize.y;
         y += GRID_SZ * canvasScale)
      drawList->AddLine(ImVec2(0, y) + winPos, ImVec2(canvasSize.x, y) + winPos, GRID_COLOR);
  }

  // Selection Box
  AABB<ImVec2> const aabb(imvec(gv.selectionBoxStart), imvec(gv.selectionBoxEnd));
  if (gv.uiState == GraphView::UIState::BOX_SELECTING) {
    drawList->AddRectFilled(aabb.min, aabb.max, SELECTION_BOX_COLOR);
  } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
    drawList->AddRectFilled(aabb.min, aabb.max, DESELECTION_BOX_COLOR);
  }

  // Draw Links
  for (auto const& link : gv.graph->links()) {
    if (gv.pendingLink.destiny == link.first)
      continue;
    auto path = transform(gv.graph->linkPath(link.first), toScreen);
    drawList->AddPolyline(
        path.data(),
        int(path.size()),
        imcolor(highlight(gv.graph->noderef(link.second.nodeIndex).color(), 0, 0.2f, 1.0f)),
        false,
        glm::clamp(1.f * canvasScale, 1.0f, 4.0f));
  }

  // Nodes
  auto visibilityClipingArea = canvasArea;
  visibilityClipingArea.expand(8 * canvasScale);
  for (size_t i = 0; i < gv.graph->order().size(); ++i) {
    size_t const idx         = gv.graph->order()[i];
    auto const&  node        = gv.graph->nodes().at(idx);
    auto const   center      = toScreen * glm::vec3(node.pos(), 1.0);
    auto const   size        = node.size();
    float const  pinRadius   = 4 * canvasScale;
    int const    pinSegs     = circleSegs(pinRadius);
    ImVec2 const topleft     = {center.x - size.x / 2.f * canvasScale,
                                center.y - size.y / 2.f * canvasScale};
    ImVec2 const bottomright = {center.x + size.x / 2.f * canvasScale,
                                center.y + size.y / 2.f * canvasScale};

    if (!visibilityClipingArea.intersects(AABB<ImVec2>(topleft, bottomright)))
      continue;

    auto const color = unconfirmedNodeSelection.find(idx) != unconfirmedNodeSelection.end()
                           ? highlight(node.color(), 0.1f, 0.5f)
                           : gv.hoveredNode == idx
                                 ? highlight(node.color(), 0.02f, 0.3f)
                                 : (gv.nodeSelection.find(idx) != gv.nodeSelection.end()
                                        ? highlight(node.color(), -0.1f, -0.4f)
                                        : node.color());

    if (node.type() == Node::Type::NORMAL) {
      // Node itself
      drawList->AddRectFilled(
          topleft, bottomright, imcolor(color), cornerRounding(6.f * canvasScale));

      // Selected highlight
      if (gv.nodeSelection.find(idx) != gv.nodeSelection.end() && canvasScale > 0.2)
        drawList->AddRect(topleft + ImVec2{-4 * canvasScale, -4 * canvasScale},
                          bottomright + ImVec2{4 * canvasScale, 4 * canvasScale},
                          imcolor(highlight(node.color(), 0.1f, 0.6f)),
                          cornerRounding(8.f * canvasScale));

      // Pins
      int icount = node.maxInputCount();
      if (icount < 8) {
        for (int i = 0; i < node.maxInputCount(); ++i) {
          size_t upnode = gv.graph->upstreamNodeOf(idx, i);
          auto   pincolor = color;
          if (upnode != -1) {
            pincolor = gv.graph->noderef(upnode).color();
          }
          auto const currentPin = NodePin{ NodePin::INPUT, idx, i };
          if (currentPin == gv.hoveredPin || currentPin == gv.activePin) {
            pincolor = highlight(pincolor, 0.1f, 0.4f, 0.5f);
          }
          drawList->AddCircleFilled(
            toScreen * imvec(node.inputPinPos(i)), pinRadius, imcolor(pincolor), pinSegs);
        }
      } else {
        auto left = node.inputPinPos(0);
        auto right = node.inputPinPos(icount - 1);
        drawList->AddRectFilled(toScreen * imvec(left + glm::vec2{ 6,-6 }), toScreen * imvec(right + glm::vec2{ -6,0 }), imcolor(color), 6);
      }
      for (int i = 0; i < node.outputCount(); ++i) {
        auto const currentPin = NodePin{NodePin::OUTPUT, idx, i};
        auto       pincolor   = color;
        if (currentPin == gv.hoveredPin || currentPin == gv.activePin) {
          pincolor = highlight(pincolor, 0.1f, 0.4f, 0.5f);
        }
        drawList->AddCircleFilled(
            toScreen * imvec(node.outputPinPos(i)), pinRadius, imcolor(pincolor), pinSegs);
      }

      // Name
      if (canvasScale >= 1.5f && globalConfig().fonts.largeFont)
        ImGui::PushFont(globalConfig().fonts.largeFont);
      float const fontHeight = ImGui::GetFontSize();
      if (gv.drawName && canvasScale > 0.33) {
        drawList->AddText(ImVec2{center.x, center.y} +
                              ImVec2{size.x / 2.f * canvasScale + 8, -fontHeight / 2.f},
                          imcolor(highlight(color, -0.8f, 0.6f, 0.6f)),
                          node.displayName().c_str());
      }
      if (canvasScale >= 1.5f && globalConfig().fonts.largeFont)
        ImGui::PopFont();

      // Icon
      if (char const* icon = node.icon()) {
        ImFont* font = nullptr;
        if (canvasScale >= 1.5f && globalConfig().fonts.largeIconFont)
          font = globalConfig().fonts.largeIconFont;
        else
          font = globalConfig().fonts.stdIconFont;

        if (font) {
          float const iconHeight = size.y * canvasScale * 0.7f;
          auto textSize = font->CalcTextSizeA(iconHeight, 8192, 0, icon);
          drawList->AddText(font, iconHeight, imvec(center) - textSize / 2, imcolor(highlight(color, -0.8f, -0.7f, 1.0f)), icon);
        }
      }

      node.onDraw(gv);
    } else if (node.type() == Node::Type::ANCHOR) {
      drawList->AddCircleFilled(imvec(center), 8, imcolor(color));
    }
  }

  // Pending node
  if (gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
    auto const   center      = mousePos;
    ImVec2 const topleft     = {center.x - DEFAULT_NODE_SIZE.x / 2.f * canvasScale,
                                center.y - DEFAULT_NODE_SIZE.y / 2.f * canvasScale};
    ImVec2 const bottomright = {center.x + DEFAULT_NODE_SIZE.x / 2.f * canvasScale,
                                center.y + DEFAULT_NODE_SIZE.y / 2.f * canvasScale};
    drawList->AddRectFilled(
        topleft, bottomright, PENDING_PLACE_NODE_COLOR, cornerRounding(6.f * canvasScale));
  }

  // Pending Links ...
  auto drawLink = [&gv, drawList, &toScreen, &toCanvas](glm::vec2 const& start,
                                                       glm::vec2 const& end) {
    auto path = transform(gv.graph->genLinkPath(start, end), toScreen);
    drawList->AddPolyline(path.data(),
                          int(path.size()),
                          IM_COL32(233, 233, 233, 233),
                          false,
                          glm::clamp(1.f * gv.canvasScale, 1.0f, 4.0f));
  };
  if (gv.pendingLink.source.type == NodePin::NONE && gv.pendingLink.destiny.type != NodePin::NONE) {
    glm::vec2 curveEnd = gv.graph->noderef(gv.pendingLink.destiny.nodeIndex)
                             .inputPinPos(gv.pendingLink.destiny.pinNumber);
    glm::vec2 curveStart = glmvec(toCanvas * mousePos);
    drawLink(curveStart, curveEnd);
  } else if (gv.pendingLink.destiny.type == NodePin::NONE && gv.pendingLink.source.type != NodePin::NONE) {
    glm::vec2 curveStart = gv.graph->noderef(gv.pendingLink.source.nodeIndex)
                               .outputPinPos(gv.pendingLink.source.pinNumber);
    glm::vec2 curveEnd = glmvec(toCanvas * mousePos);
    drawLink(curveStart, curveEnd);
  } else if (gv.pendingLink.source.type != NodePin::NONE && gv.pendingLink.destiny.type != NodePin::NONE) {
    glm::vec2 curveStart = gv.graph->noderef(gv.pendingLink.source.nodeIndex)
                               .outputPinPos(gv.pendingLink.source.pinNumber);
    glm::vec2 curveEnd = gv.graph->noderef(gv.pendingLink.destiny.nodeIndex)
                             .inputPinPos(gv.pendingLink.destiny.pinNumber);
    glm::vec2 curveCenter = glmvec(toCanvas * mousePos);
    if (gv.hoveredPin.type != NodePin::OUTPUT)
      drawLink(curveStart, curveCenter);
    if (gv.hoveredPin.type != NodePin::INPUT)
      drawLink(curveCenter, curveEnd);
    drawList->AddCircleFilled(mousePos, 4 * canvasScale, IM_COL32(233, 233, 233, 128));
  }

  // Link cutting stroke
  if (gv.uiState == GraphView::UIState::CUTING_LINK) {
    std::vector<ImVec2> stroke(gv.linkCuttingStroke.size());
    for (size_t i = 0; i < gv.linkCuttingStroke.size(); ++i)
      stroke[i] = imvec(toScreen * glm::vec3(gv.linkCuttingStroke[i], 1.0f));
    drawList->AddPolyline(stroke.data(), int(stroke.size()), IM_COL32(255, 0, 0, 233), false, 2);
  }
}

void updateContextMenu(GraphView& gv)
{
  static char nodeClass[512] = {0};
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
  ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(800, 1024));

  if (ImGui::BeginPopup("Create Node")) {
    auto confirmNodeClass = [&gv](std::string const& nodeClass) {
      gv.uiState = GraphView::UIState::PLACING_NEW_NODE;
      gv.pendingNodeClass = nodeClass;
      ImGui::CloseCurrentPopup();
    };
    auto const& classList = gv.graph->getNodeClassList();
    std::multimap<ptrdiff_t, std::string> orderedMatches; // edit distance -> string
   
    memset(nodeClass, 0, sizeof(nodeClass));
    ImGui::SetKeyboardFocusHere(0);
    ImGui::PushItemWidth(-1);
    bool confirmed = ImGui::InputText(
      "##nodeClass", nodeClass, sizeof(nodeClass), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
      gv.pendingLink = {};
      ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    if (nodeClass[0] != 0) {
      for (auto const& clsname : classList) {
        orderedMatches.insert({ longestCommonSequenceLength(nodeClass, clsname), clsname });
      }
      if (confirmed && !orderedMatches.empty())
        confirmNodeClass((++orderedMatches.rend())->second);
    }
    for (auto itr = orderedMatches.rbegin(); itr != orderedMatches.rend(); ++itr) {
      if (ImGui::MenuItem(itr->second.c_str(), nullptr)) {
        confirmNodeClass(itr->second);
      }
    }
    ImGui::PopItemWidth();
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

static void focusSelected(GraphView& gv)
{
  if (!gv.nodeSelection.empty()) {
    auto itr = gv.nodeSelection.begin();
    AABB<glm::vec2> aabb(gv.graph->noderef(*itr).pos());
    for (++itr; itr!=gv.nodeSelection.end(); ++itr) {
      aabb.merge(gv.graph->noderef(*itr).pos());
    }

    gv.canvasOffset = -glm::vec2((aabb.min.x + aabb.max.x) / 2.f, (aabb.min.y + aabb.max.y) / 2.f);
    gv.canvasScale = 1.f;
  } else if (!gv.graph->nodes().empty()) { // if nothing is selected, view the whole graph
    auto itr = gv.graph->nodes().begin();
    AABB<glm::vec2> aabb(gv.graph->nodes().begin()->second.pos());
    for (++itr; itr!=gv.graph->nodes().end(); ++itr) {
      aabb.merge(itr->second.pos());
    }
    
    gv.canvasOffset = -glm::vec2((aabb.min.x + aabb.max.x) / 2.f, (aabb.min.y + aabb.max.y) / 2.f);
    aabb.expand(20);
    gv.canvasScale = std::min(1.f, std::min(gv.canvasSize.x / aabb.size().x, gv.canvasSize.y / aabb.size().y));
  }
}

static void confirmNewNodePlacing(GraphView& gv, ImVec2 const& pos)
{
  size_t idx = gv.graph->addNode(gv.pendingNodeClass, gv.pendingNodeClass, glm::vec2(pos.x, pos.y));
  gv.activeNode = idx;
  gv.nodeSelection = { idx };

  if (gv.pendingLink.source.type == NodePin::OUTPUT &&
    gv.pendingLink.source.nodeIndex != -1 &&
    gv.pendingLink.source.pinNumber >= 0 &&
    gv.graph->noderef(idx).maxInputCount() > 0) {
    gv.graph->addLink(gv.pendingLink.source.nodeIndex, gv.pendingLink.source.pinNumber, idx, 0);
  }
  if (gv.pendingLink.destiny.type == NodePin::INPUT &&
    gv.pendingLink.destiny.nodeIndex != -1 &&
    gv.pendingLink.destiny.pinNumber >= 0 &&
    gv.graph->noderef(idx).outputCount() > 0) {
    gv.graph->addLink(idx, 0, gv.pendingLink.destiny.nodeIndex, gv.pendingLink.destiny.pinNumber);
  }
  gv.pendingLink = {};
  gv.uiState = GraphView::UIState::VIEWING;
}

void updateNetworkView(GraphView& gv, char const* name)
{
  ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(name, gv.kind==GraphView::Kind::EVERYTHING ? nullptr : &gv.showNetwork)) {
    ImGui::End();
    return;
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 25, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

  ImGui::BeginChild(
      "Canvas", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 const canvasSize        = ImGui::GetWindowSize();
  ImVec2 const mousePos          = ImGui::GetMousePos();
  ImVec2 const winPos            = ImGui::GetCursorScreenPos();
  ImVec2 const mouseDelta        = ImGui::GetIO().MouseDelta;
  auto const   modKey            = ImGui::GetIO().KeyMods;
  auto&        graph             = *gv.graph;
  auto const   canvasScale       = gv.canvasScale;
  auto const   canvasOffset      = gv.canvasOffset;
  auto const   canvasArea        = AABB<ImVec2>(winPos, winPos + canvasSize);
  auto const   clipArea          = canvasArea.expanded(8 * canvasScale);
  bool const   mouseInsideCanvas = canvasArea.contains(mousePos);

  glm::mat3 const toScreen = calcToScreenMatrix(gv, canvasArea);
  glm::mat3 const toCanvas = glm::inverse(toScreen);
  gv.canvasToScreen = toScreen;
  gv.screenToCanvas = toCanvas;

  size_t  hoveredNode = -1;
  size_t  clickedNode = -1;
  NodePin hoveredPin  = {NodePin::NONE, size_t(-1), -1},
          clickedPin  = {NodePin::NONE, size_t(-1), -1};
  gv.canvasSize       = glm::vec2(canvasSize.x, canvasSize.y);
  gv.selectionBoxEnd  = glm::vec2(mousePos.x, mousePos.y);

  std::set<size_t> unconfirmedNodeSelection = gv.nodeSelection;
  AABB<ImVec2> selectionBox(imvec(gv.selectionBoxStart), imvec(gv.selectionBoxEnd));

  // Check hovering node & pin
  for (size_t i = 0; i < graph.order().size(); ++i) {
    size_t const idx     = graph.order()[i];
    auto const&  node    = graph.nodes().at(idx);
    auto const   center  = toScreen * glm::vec3(node.pos(), 1.0f);
    auto const   size    = node.size();

    ImVec2 const topleft = {center.x - size.x / 2.f * canvasScale,
                            center.y - size.y / 2.f * canvasScale};

    ImVec2 const bottomright = {center.x + size.x / 2.f * canvasScale,
                                center.y + size.y / 2.f * canvasScale};

    AABB<ImVec2> const nodebox(topleft, bottomright);
    if (!clipArea.intersects(nodebox))
      continue;

    if (nodebox.contains(mousePos) && mouseInsideCanvas)
      hoveredNode = idx;

    if (selectionBox.intersects(nodebox)) {
      if (gv.uiState == GraphView::UIState::BOX_SELECTING) {
        unconfirmedNodeSelection.insert(idx);
      } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
        unconfirmedNodeSelection.erase(idx);
      }
    }

    if (nodebox.expanded(8 * canvasScale).contains(mousePos)) {
      for (int ipin = 0; ipin < node.maxInputCount(); ++ipin) {
        if (glm::distance2(node.inputPinPos(ipin),
                           glm::xy(toCanvas * glm::vec3{mousePos.x, mousePos.y, 1})) < 25) {
          hoveredPin = {NodePin::INPUT, idx, ipin};
        }
      }
      for (int opin = 0; opin < node.outputCount(); ++opin) {
        if (glm::distance2(node.outputPinPos(opin),
                           glm::xy(toCanvas * glm::vec3{mousePos.x, mousePos.y, 1})) < 25) {
          hoveredPin = {NodePin::OUTPUT, idx, opin};
        }
      }
    }
  }
  // Mouse action - the dirty part
  if (mouseInsideCanvas && ImGui::IsWindowHovered()) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      clickedNode   = hoveredNode;
      clickedPin    = hoveredPin;
      gv.activeNode = clickedNode;
      if (clickedNode != -1) {
        graph.onNodeClicked(clickedNode, 0);
        gv.uiState = GraphView::UIState::DRAGGING_NODES;
        if (gv.nodeSelection.find(clickedNode) == gv.nodeSelection.end()) {
          gv.nodeSelection = {clickedNode};
        }
      } else if (clickedPin.nodeIndex != -1) {
        if (clickedPin.type == NodePin::OUTPUT) {
          gv.uiState     = GraphView::UIState::DRAGGING_LINK_TAIL;
          gv.pendingLink = {{NodePin::OUTPUT, clickedPin.nodeIndex, clickedPin.pinNumber},
                            {NodePin::NONE, size_t(-1), -1}};
        } else if (clickedPin.type == NodePin::INPUT) {
          gv.uiState     = GraphView::UIState::DRAGGING_LINK_HEAD;
          gv.pendingLink = {{NodePin::NONE, size_t(-1), -1},
                            {NodePin::INPUT, clickedPin.nodeIndex, clickedPin.pinNumber}};
        }
      } else {
        glm::vec2 const mouseInLocal = glmvec(toCanvas * mousePos);
        for (auto& link : graph.links()) {
          auto const linkStart =
              graph.noderef(link.second.nodeIndex).outputPinPos(link.second.pinNumber);
          auto const linkEnd =
              graph.noderef(link.first.nodeIndex).inputPinPos(link.first.pinNumber);
          if (AABB<glm::vec2>(linkStart, linkEnd).expanded(12).contains(mouseInLocal)) {
            auto const& linkPath = graph.linkPath(link.first);
            for (size_t i = 1; i < linkPath.size(); ++i) {
              if (pointSegmentDistance(mouseInLocal, linkPath[i - 1], linkPath[i]) <
                  3 * canvasScale) {
                gv.uiState     = GraphView::UIState::DRAGGING_LINK_BODY;
                gv.pendingLink = {{NodePin::OUTPUT, link.second.nodeIndex, link.second.pinNumber},
                                  {NodePin::INPUT, link.first.nodeIndex, link.first.pinNumber}};
                spdlog::debug("dragging link body from node({}).pin({}) to node({}).pin({})",
                              link.second.nodeIndex,
                              link.second.pinNumber,
                              link.first.nodeIndex,
                              link.first.pinNumber);
                break;
              }
            }
          }
        }
      }
      if (hoveredNode != -1) {
        graph.onNodeHovered(hoveredNode);
      }
      if (hoveredPin.type != NodePin::NONE) {
        graph.onPinHovered(hoveredPin);
      }
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (clickedNode != -1)
          graph.onNodeDoubleClicked(clickedNode, 1);
      }
      if (gv.uiState == GraphView::UIState::VIEWING) {
        gv.selectionBoxStart = {mousePos.x, mousePos.y};
        if (modKey == ImGuiKeyModFlags_Shift) {
          gv.uiState = GraphView::UIState::BOX_SELECTING;
        } else if (modKey == ImGuiKeyModFlags_Ctrl) {
          gv.uiState = GraphView::UIState::BOX_DESELECTING;
        }

        if (gv.uiState == GraphView::UIState::BOX_SELECTING) {
          if (clickedNode != -1)
            gv.nodeSelection.insert(clickedNode);
        } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
          if (clickedNode != -1)
            gv.nodeSelection.erase(clickedNode);
        }
      }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      if (gv.uiState == GraphView::UIState::DRAGGING_NODES) {
        graph.stash();
      } else if (gv.uiState == GraphView::UIState::BOX_SELECTING ||
          gv.uiState == GraphView::UIState::BOX_DESELECTING) {
        gv.nodeSelection = unconfirmedNodeSelection;
      } else if (gv.uiState == GraphView::UIState::VIEWING) {
        if (hoveredNode == -1 && clickedNode == -1 && gv.activeNode == -1) {
          gv.nodeSelection.clear();
        } else if (gv.activeNode != -1 &&
                   glm::distance(gv.selectionBoxStart, gv.selectionBoxEnd) < 4) {
          gv.nodeSelection = {gv.activeNode};
        }
      } else if (gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
        confirmNewNodePlacing(gv, toCanvas * mousePos);
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_HEAD) {
        if (hoveredPin.type == NodePin::OUTPUT) {
          graph.addLink(hoveredPin.nodeIndex,
                        hoveredPin.pinNumber,
                        gv.pendingLink.destiny.nodeIndex,
                        gv.pendingLink.destiny.pinNumber);
          gv.pendingLink = {};
        } else if (hoveredPin.type == NodePin::NONE && hoveredNode != -1) {
          graph.addLink(
              hoveredNode, 0, gv.pendingLink.destiny.nodeIndex, gv.pendingLink.destiny.pinNumber);
          gv.pendingLink = {};
        } else if (hoveredNode == -1 && hoveredPin.type == NodePin::NONE) {
          ImGui::OpenPopup("Create Node");
        } else {
          gv.pendingLink = {};
        }
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_TAIL) {
        if (hoveredPin.type == NodePin::INPUT) {
          graph.addLink(gv.pendingLink.source.nodeIndex,
                        gv.pendingLink.source.pinNumber,
                        hoveredPin.nodeIndex,
                        hoveredPin.pinNumber);
          gv.pendingLink = {};
        } else if (hoveredPin.type == NodePin::NONE && hoveredNode != -1) {
          graph.addLink(
              gv.pendingLink.source.nodeIndex, gv.pendingLink.source.pinNumber, hoveredNode, 0);
          gv.pendingLink = {};
        } else if (hoveredNode == -1 && hoveredPin.type == NodePin::NONE) {
          ImGui::OpenPopup("Create Node");
        } else {
          gv.pendingLink = {};
        }
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_BODY) {
        if (hoveredPin.type == NodePin::INPUT &&
            hoveredPin.nodeIndex != gv.pendingLink.source.nodeIndex) {
          graph.removeLink(gv.pendingLink.destiny.nodeIndex, gv.pendingLink.destiny.pinNumber);
          if (modKey == ImGuiKeyModFlags_Alt) { // swap link
            auto const& pendingSrc = gv.pendingLink.source;
            auto const& pendingDst = gv.pendingLink.destiny;
            auto hvitr = gv.graph->links().find(hoveredPin);
            if (hvitr != gv.graph->links().end()) {
              auto const& hoverSource = hvitr->second;
              graph.addLink(hoverSource.nodeIndex, hoverSource.pinNumber, pendingDst.nodeIndex, pendingDst.pinNumber);
            }
          }
          graph.addLink(gv.pendingLink.source.nodeIndex,
                        gv.pendingLink.source.pinNumber,
                        hoveredPin.nodeIndex,
                        hoveredPin.pinNumber);
          gv.pendingLink = {};
        } else if (hoveredPin.type == NodePin::OUTPUT &&
                   hoveredPin.nodeIndex != gv.pendingLink.destiny.nodeIndex) {
          if (modKey == ImGuiKeyModFlags_Alt) { // swap link
            auto cnt = std::count_if(graph.links().begin(), graph.links().end(),
              [hoveredPin](std::pair<NodePin, NodePin> const& rec) {
                return rec.second == hoveredPin;
              });
            if (cnt == 1) { // do swaping only when there is only one dest connection
              auto hvitr = std::find_if(graph.links().begin(), graph.links().end(),
                [hoveredPin](std::pair<NodePin, NodePin> const& rec) {
                  return rec.second == hoveredPin;
                });
              if (hvitr != graph.links().end()) {
                auto pditr = graph.links().find(gv.pendingLink.destiny);
                if (pditr != graph.links().end()) {
                  // add link from pending link's source to hovered pin's destiny
                  graph.addLink(pditr->second.nodeIndex, pditr->second.pinNumber, hvitr->first.nodeIndex, hvitr->first.pinNumber);
                }
              }
            }
          }
          graph.removeLink(gv.pendingLink.destiny.nodeIndex, gv.pendingLink.destiny.pinNumber);
          graph.addLink(hoveredPin.nodeIndex,
                        hoveredPin.pinNumber,
                        gv.pendingLink.destiny.nodeIndex,
                        gv.pendingLink.destiny.pinNumber);
          gv.pendingLink = {};
        } else if (hoveredNode != -1) {
          graph.removeLink(gv.pendingLink.destiny.nodeIndex, gv.pendingLink.destiny.pinNumber);
          auto const& node = gv.graph->noderef(hoveredNode);
          if (node.maxInputCount() > 0 && hoveredNode != gv.pendingLink.source.nodeIndex) {
            graph.addLink(
                gv.pendingLink.source.nodeIndex, gv.pendingLink.source.pinNumber, hoveredNode, 0);
          }
          if (node.outputCount() > 0 && hoveredNode != gv.pendingLink.destiny.nodeIndex) {
            graph.addLink(hoveredNode,
                          0,
                          gv.pendingLink.destiny.nodeIndex,
                          gv.pendingLink.destiny.pinNumber);
          }
          gv.pendingLink = {};
        } else {
          ImGui::OpenPopup("Create Node");
        }
      }
      gv.uiState = GraphView::UIState::VIEWING;
    }
    gv.hoveredNode = hoveredNode;
    gv.hoveredPin  = hoveredPin;
    gv.activePin   = clickedPin;

    // Paning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto const srcDelta = glm::xy(toCanvas * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      gv.canvasOffset += srcDelta;
    }
    // Scaling
    if (abs(ImGui::GetIO().MouseWheel) > 0.1) {
      gv.canvasScale = glm::clamp(gv.canvasScale + ImGui::GetIO().MouseWheel / 20.f, 0.1f, 10.f);
      // cursor as scale center:
      auto canvasToScreen      = calcToScreenMatrix(gv, canvasArea);
      auto screenToCanvas      = glm::inverse(canvasToScreen);
      auto const cc            = mousePos;
      auto const ccInOldCanvas = toCanvas * cc;
      auto const ccInNewCanvas = screenToCanvas * cc;

      gv.canvasOffset +=
          glm::vec2(ccInNewCanvas.x - ccInOldCanvas.x, ccInNewCanvas.y - ccInOldCanvas.y);
    }

    // Handle Keydown
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter))) { // confirm
      if (gv.uiState == GraphView::UIState::PLACING_NEW_NODE)
        confirmNewNodePlacing(gv, toCanvas * mousePos);
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) { // delete
      spdlog::debug("removing nodes [{}] from view {}", fmt::join(gv.nodeSelection, ", "), name);
      graph.removeNodes(gv.nodeSelection);
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)) &&
               gv.uiState == GraphView::UIState::VIEWING) { // new node
      ImGui::OpenPopup("Create Node");
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)) &&
               gv.uiState == GraphView::UIState::PLACING_NEW_NODE) { // reset
      gv.uiState = GraphView::UIState::VIEWING;
      gv.pendingLink = {};
    } else if (ImGui::IsKeyPressed('F')) { // focus
      focusSelected(gv);
    } else if (ImGui::IsKeyPressed('C') && modKey == ImGuiKeyModFlags_Ctrl) { // copy
      gv.copy();
    } else if (ImGui::IsKeyPressed('X') && modKey == ImGuiKeyModFlags_Ctrl) { // cut
      gv.copy();
      graph.removeNodes(gv.nodeSelection);
    } else if (ImGui::IsKeyPressed('V') && modKey == ImGuiKeyModFlags_Ctrl) { // paste
      gv.paste();
      gv.uiState = GraphView::UIState::VIEWING;
    } else if (ImGui::IsKeyPressed('A') && modKey == ImGuiKeyModFlags_Ctrl) { // Select All
      gv.nodeSelection.clear();
      for (auto const& n : gv.graph->nodes())
        gv.nodeSelection.insert(n.first);
    } else if (ImGui::IsKeyPressed('Z') && modKey == ImGuiKeyModFlags_Ctrl) { // Undo
      gv.graph->undo();
    } else if (ImGui::IsKeyPressed('R') && modKey == ImGuiKeyModFlags_Ctrl) { // Redo
      gv.graph->redo();
    }
  } // Mouse inside canvas?

  // Dragging can go beyond canvas
  if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10)) {
    if (gv.uiState == GraphView::UIState::VIEWING && ImGui::IsWindowHovered() &&
        mouseInsideCanvas) {
      gv.uiState = GraphView::UIState::BOX_SELECTING;
      gv.nodeSelection.clear();
    } else if (gv.uiState == GraphView::UIState::DRAGGING_NODES) { // drag nodes
      auto const srcDelta = glm::xy(toCanvas * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      if (glm::length(srcDelta) > 0) {
        graph.moveNodes(gv.nodeSelection, srcDelta);
      }
    } else if (ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Y))) { // cut links
      gv.uiState = GraphView::UIState::CUTING_LINK;
      auto mp    = toCanvas * mousePos;
      gv.linkCuttingStroke.push_back(glm::vec2(mp.x, mp.y));
    }
  }
  
  // Reset states on mouse release
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    // was dragging ...
    if (gv.uiState == GraphView::UIState::DRAGGING_NODES) {
      graph.stash();
    }
    // confirm node selection
    if (gv.uiState == GraphView::UIState::BOX_SELECTING ||
        gv.uiState == GraphView::UIState::BOX_DESELECTING) {
      if (!ImGui::IsWindowHovered() || !mouseInsideCanvas)
        gv.nodeSelection = unconfirmedNodeSelection;
    }
    // confirm link cutting
    if (!gv.linkCuttingStroke.empty()) {
      AABB<glm::vec2> cutterbox(gv.linkCuttingStroke.front());
      for (size_t i = 1; i < gv.linkCuttingStroke.size(); ++i) {
        cutterbox.merge(gv.linkCuttingStroke[i]);
      }
      cutterbox.expand(100);
      std::vector<NodePin> dstPinsToDelete;
      for (auto const& link : graph.links()) {
        auto const linkStart = graph.noderef(link.second.nodeIndex).pos();
        auto const linkEnd   = graph.noderef(link.first.nodeIndex).pos();
        if (cutterbox.intersects(AABB<glm::vec2>(linkStart, linkEnd))) {
          std::vector<glm::vec2> const& linkPath = graph.linkPath(link.first);
          if (strokeIntersects(linkPath, gv.linkCuttingStroke)) {
            dstPinsToDelete.push_back(
                {NodePin::INPUT, link.first.nodeIndex, link.first.pinNumber});
          }
        }
      }
      for (auto const& pin : dstPinsToDelete) {
        gv.graph->removeLink(pin.nodeIndex, pin.pinNumber);
      }
      gv.linkCuttingStroke.clear();
    }
    gv.uiState = GraphView::UIState::VIEWING;
  }

  drawGraph(gv, unconfirmedNodeSelection);

  updateContextMenu(gv);

  ImGui::EndChild();

  ImGui::PopStyleVar();   // frame padding
  ImGui::PopStyleVar();   // window padding
  ImGui::PopStyleColor(); // child bg
  ImGui::End();
}

void updateDatasheetView(GraphView& gv, char const* name)
{
  if (!gv.showDatasheet)
    return;
  ImGui::SetNextWindowSize(ImVec2{ 320, 480 }, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(name, &gv.showDatasheet)) {
    ImGui::End();
    return;
  }

  {
    FontScope monoscope(FontScope::MONOSPACE);
    if (gv.focusingNode != -1) {
      try {
        gv.graph->noderef(gv.focusingNode).onInspectData(gv);
      } catch (std::exception const& e) {
        ImGui::Text("Error: %s", e.what());
      }
    } else {
      if (ImGui::BeginTabBar("datasheet", ImGuiTabBarFlags_AutoSelectNewTabs)) {
        if (gv.nodeSelection.size() == 1 && *gv.nodeSelection.begin() != -1) {
          if (ImGui::BeginTabItem("datasheet")) {
            try {
              gv.graph->noderef(*gv.nodeSelection.begin()).onInspectData(gv);
            } catch (std::exception const& e) {
              ImGui::Text("Error: %s", e.what());
            }
            ImGui::EndTabItem();
          }
        }
        if (ImGui::BeginTabItem("global state")) {
          gv.graph->onInspectSummary(gv);
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
    }
  }
  ImGui::End();
}

static bool showStyleEditor = false;
void updateAndDraw(GraphView& gv, char const* name, size_t id)
{
  std::string focusing = "";
  if (gv.focusingNode != -1)
    focusing = fmt::format(" ({})", gv.graph->noderef(gv.focusingNode).displayName());
  auto networkName = fmt::format("Network {}##network{}{}", id, name, id);
  auto inspectorName = fmt::format("Inspector {}{}##inspector{}{}", id, focusing, name, id);
  auto datasheetName = fmt::format("Datasheet {}{}##datasheet{}{}", id, focusing, name, id);

  if (gv.kind == GraphView::Kind::EVERYTHING) {
    auto dockWindowName = fmt::format("View {}##dockwindow{}{}", id, name, id);
    auto dockName = fmt::format("Dock_{}", dockWindowName);
    auto dockID = ImGui::GetID(dockName.c_str());

    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin(dockWindowName.c_str(), &gv.showNetwork, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", nullptr, nullptr)) {
          // TODO
        }
        if (ImGui::MenuItem("Open ...", nullptr, nullptr)) {
          nfdchar_t* path = nullptr;
          auto result = NFD_OpenDialog("json;graph", nullptr, &path);
          if (result == NFD_OKAY && path) {
            try {
              std::ifstream ifile(path, std::ios::binary);
              auto json = nlohmann::json::parse(ifile);
              spdlog::info("loading graph from \"{}\"", path);
              spdlog::info("loading {}", gv.graph->load(json, path) ? "succeed" : "failed");
            } catch (std::exception const& e) {
              spdlog::error("failed to load file \"{}\": {}", path, e.what());
            }
            free(path);
          }
        }
        if (ImGui::MenuItem("Save", "Ctrl + S", nullptr) ||
            (ImGui::IsKeyPressed('S') && ImGui::GetMergedKeyModFlags()==ImGuiKeyModFlags_Ctrl)) {
          if (gv.graph->savePath().empty()) {
            nfdchar_t* path = nullptr;
            auto result = NFD_SaveDialog("json;graph", nullptr, &path);
            if (result == NFD_OKAY && path) {
              gv.graph->setSavePath(path);
              free(path);
            }
          }
          std::ofstream ofile(gv.graph->savePath(), std::ios::binary);
          if (!ofile) {
            spdlog::error("cannot open \"{}\" for writing", gv.graph->savePath());
            gv.graph->setSavePath("");
          } else {
            nlohmann::json json;
            spdlog::info("saving graph to \"{}\"", gv.graph->savePath());
            spdlog::info("saving {}", gv.graph->save(json, gv.graph->savePath()) ? "succeed" : "failed");
            auto const& str = json.dump(2);
            ofile.write(str.c_str(), str.length());
          }
        }
        if (ImGui::MenuItem("Save As ...", nullptr, nullptr)) {
          nfdchar_t* path = nullptr;
          auto result = NFD_SaveDialog("json;graph", nullptr, &path);
          if (result == NFD_OKAY && path) {
            std::ofstream ofile(path, std::ios::binary);
            if (!ofile) {
              spdlog::error("cannot open \"{}\" for writing", path);
            } else {
              nlohmann::json json;
              spdlog::info("saving graph to \"{}\"", path);
              spdlog::info("saving {}", gv.graph->save(json, path) ? "succeed" : "failed");
              auto const& str = json.dump(2);
              ofile.write(str.c_str(), str.length());
              free(path);

              gv.graph->setSavePath(path);
            }
          }
        }
        if (ImGui::MenuItem("Quit", nullptr, nullptr)) {
          // TODO
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Name", nullptr, &gv.drawName);
        ImGui::MenuItem("Grid", nullptr, &gv.drawGrid);
        ImGui::MenuItem("Inspector", nullptr, &gv.showInspector);
        ImGui::MenuItem("Datasheet", nullptr, &gv.showDatasheet);
        if (ImGui::BeginMenu("New View")) {
          if (ImGui::MenuItem("Main Window", nullptr, nullptr)) {
            gv.graph->addViewer();
          }
          if (ImGui::MenuItem("Network")) {
            auto* view = gv.graph->addViewer(GraphView::Kind::NETWORK);
            view->nodeSelection = gv.nodeSelection;
            focusSelected(*view);
          }
          if (gv.nodeSelection.size()==1 && ImGui::MenuItem("Inspector")) {
            auto focus = *gv.nodeSelection.begin();
            if (focus!=-1) {
              auto* view = gv.graph->addViewer(GraphView::Kind::INSPECTOR);
              view->focusingNode = focus;
              view->showInspector = true;
            }
          }
          if (gv.nodeSelection.size()==1 && ImGui::MenuItem("Datasheet")) {
            auto focus = *gv.nodeSelection.begin();
            if (focus!=-1) {
              auto* view = gv.graph->addViewer(GraphView::Kind::DATASHEET);
              view->focusingNode = focus;
              view->showDatasheet = true;
            }
          }
          ImGui::EndMenu();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Tools")) {
        if (auto* hook=gv.graph->hook()) {
          hook->onToolMenu(gv.graph, gv);
        }
        ImGui::MenuItem("Style Editor", nullptr, &showStyleEditor);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Help")) {
        if (ImGui::BeginMenu("Performance")) {
          std::string fps = fmt::format("FPS = {}", ImGui::GetIO().Framerate);
          std::string vtxcnt = fmt::format("Vertices = {}", ImGui::GetIO().MetricsRenderVertices);
          std::string idxcnt = fmt::format("Indices = {}", ImGui::GetIO().MetricsRenderIndices);
          std::string nodecnt = fmt::format("Node Count = {}", gv.graph->nodes().size());
          std::string linkcnt = fmt::format("Link Count = {}", gv.graph->links().size());
          ImGui::MenuItem(fps.c_str(),    nullptr, nullptr);
          ImGui::MenuItem(vtxcnt.c_str(), nullptr, nullptr);
          ImGui::MenuItem(idxcnt.c_str(), nullptr, nullptr);
          ImGui::MenuItem(nodecnt.c_str(), nullptr, nullptr);
          ImGui::MenuItem(linkcnt.c_str(), nullptr, nullptr);
          ImGui::EndMenu();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    if (!gv.windowSetupDone) {
      ImGuiID upID = 0, downID = 0, leftID = 0, rightID = 0;
      ImGui::DockBuilderRemoveNode(dockID);
      ImGui::DockBuilderAddNode(dockID, ImGuiDockNodeFlags_PassthruCentralNode|ImGuiDockNodeFlags_HiddenTabBar);
      ImGui::DockBuilderSetNodeSize(dockID, ImGui::GetWindowSize());
      ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Up, 0.7f, &upID, &downID);
      ImGui::DockBuilderSplitNode(upID, ImGuiDir_Left, 0.7f, &leftID, &rightID);
      ImGui::DockBuilderDockWindow(networkName.c_str(), leftID);
      ImGui::DockBuilderDockWindow(inspectorName.c_str(), rightID);
      ImGui::DockBuilderDockWindow(datasheetName.c_str(), downID);
      ImGui::DockBuilderGetNode(leftID)->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar | ImGuiDockNodeFlags_NoCloseButton;
      ImGui::DockBuilderGetNode(rightID)->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar | ImGuiDockNodeFlags_NoCloseButton;
      ImGui::DockBuilderGetNode(downID)->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar | ImGuiDockNodeFlags_NoCloseButton;
      ImGui::DockBuilderFinish(dockID);
      gv.windowSetupDone = true;
    }
    ImGui::DockSpace(dockID);
    ImGui::End();
  }

  if (gv.kind == GraphView::Kind::EVERYTHING || gv.kind == GraphView::Kind::NETWORK)
    updateNetworkView(gv, networkName.c_str());
  if (gv.kind == GraphView::Kind::EVERYTHING || gv.kind == GraphView::Kind::INSPECTOR)
    updateInspectorView(gv, inspectorName.c_str());
  if (gv.kind == GraphView::Kind::EVERYTHING || gv.kind == GraphView::Kind::DATASHEET)
    updateDatasheetView(gv, datasheetName.c_str());
}

void edit(Graph& graph, char const* name)
{
  FontScope regularscope(FontScope::REGULAR);
  std::set<GraphView*> closedViews;
  auto viewers_cpy = graph.viewers();
  for (auto* view: viewers_cpy) {
    if ((view->kind == GraphView::Kind::INSPECTOR && !view->showInspector) ||
        (view->kind == GraphView::Kind::DATASHEET && !view->showDatasheet) ||
        !view->showNetwork) {
      closedViews.insert(view);
      continue;
    }
    updateAndDraw(*view, name, view->id);
  }

  for(auto* view: closedViews) {
    graph.removeViewer(view);
  }
  if (showStyleEditor)
    ImGui::ShowStyleEditor();
}

} // namespace editorui
