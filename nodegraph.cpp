#include "nodegraph.h"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <glm/ext.hpp>
#include <glm/gtx/color_space.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/vec_swizzle.hpp>
#include <memory>

// --------------------------------------------------------------------
//                        T O D O   L I S T :
// --------------------------------------------------------------------
// [ ] host real(logical) nodes & graphs
// [ ] dive in nested network
// [ ] serialization
// [ ] edit common params in inspector when multiple nodes are selected
// [ ] data inspector
// [ ] bypass flag
// [ ] output flag
// [X] display names inside network
// [ ] name font scale (2 levels?)
// [ ] drag link body to re-route
// [X] highlight hovering pin
// [ ] focus to selected nodes / frame all nodes
// [ ] copy / paste
// [ ] undo / redo
// [ ] window management
// [ ] grid snapping
// [ ] config-able appearance

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
  return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
{
  return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}
static inline ImVec2 operator*(const glm::mat3& m, const ImVec2& v)
{
  auto r = m * glm::vec3(v.x, v.y, 1.0f);
  return ImVec2(r.x, r.y);
}
static inline bool ccw(const ImVec2& a, const ImVec2& b, const ImVec2& c)
{
  auto const ab = b - a, ac = c - a;
  return glm::cross(glm::vec3(ab.x, ab.y, 0.f), glm::vec3(ac.x, ac.y, 0.f)).z > 0;
}
static inline float length(const ImVec2& v)
{
  return sqrt(v.x * v.x + v.y * v.y);
}
static inline float cornerRounding(float r)
{
  return r > 1 ? r : 0.f;
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
static inline ImU32 imcolor(glm::vec4 const& color)
{
  return ImGui::ColorConvertFloat4ToU32(ImVec4{color.r, color.g, color.b, color.a});
}

struct AABB
{
  ImVec2 min, max;

  AABB(ImVec2 const& a) { min = max = a; }
  AABB(ImVec2 const& a, ImVec2 const& b) : AABB(a) { merge(b); }
  void merge(ImVec2 const& v)
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
  // test if the that is contained inside this
  bool contains(AABB const& that) const
  {
    return min.x <= that.min.x && min.y <= that.min.y && max.x >= that.max.x &&
           max.y >= that.max.y;
  }
  // test if point is inside this
  bool contains(ImVec2 const& pt) const
  {
    return pt.x <= max.x && pt.y <= max.y && pt.x >= min.x && pt.y >= min.y;
  }
  // test if the two AABBs has intersection
  bool intersects(AABB const& that) const
  {
    return !(max.x < that.min.x || that.max.x < min.x || max.y < that.min.y || that.max.y < min.y);
  }
};
static void genLinkPath(std::vector<ImVec2>& buffer,
                        ImVec2 const&        start,
                        ImVec2 const&        end,
                        ImVec2 const&        angledSegmentLengthRange)
{
  // TODO: make this prettier
  /*
  buffer.clear();
  float const xcenter = (start.x + end.x) * 0.5f;
  float const ycenter = (start.y + end.y) * 0.5f;
  float const minAngledLength = angledSegmentLengthRange.x;
  float const maxAngledLength = angledSegmentLengthRange.y;
  */
  buffer = {start,
            ImVec2(start.x, glm::mix(start.y, end.y, 0.33f)),
            ImVec2(end.x, glm::mix(start.y, end.y, 0.67f)),
            end};
}
static void drawLink(ImDrawList*   drawList,
                     ImVec2 const& start,
                     ImVec2 const& end,
                     ImU32         color     = IM_COL32(200, 200, 200, 200),
                     float         thickness = 1.0f)
{
  static std::vector<ImVec2> buff;
  genLinkPath(buff, start, end, {10, 100});
  drawList->AddPolyline(buff.data(), buff.size(), color, false, thickness);
}
static bool strokeIntersects(std::vector<ImVec2> const& a, std::vector<ImVec2> const& b)
{
  // TODO: implement sweep line algorithm
  for (size_t i = 1; i < a.size(); ++i) {
    for (size_t j = 1; j < b.size(); ++j) {
      if (AABB(a[i - 1], a[i]).intersects(AABB(b[j - 1], b[j]))) {
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

namespace editorui {

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
  }
}

void updateInspectorView(Node* node, char const* name)
{
  auto title = fmt::format("Param Inspector##inspector{}", name);
  if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }

  if (node) {
    // ImGui::Text(node->name.c_str());
    char namebuf[512] = {0};
    memcpy(namebuf, node->name.c_str(), node->name.size());
    if (ImGui::InputText("##nodename",
                         namebuf,
                         sizeof(namebuf),
                         ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue))
      node->name = namebuf;
    ImGui::SliderInt("Number of Inputs", &node->numInputs, 0, 10);
    ImGui::SliderInt("Number of Outputs", &node->numOutputs, 0, 10);
    ImGui::ColorEdit4("Color", &node->color.r);
  } else {
    ImGui::Text("nothing selected");
  }

  ImGui::End();
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
  auto const   canvasArea         = AABB(winPos, winPos + canvasSize);
  bool const   cursorInsideCanvas = canvasArea.contains(mousePos);

  auto calcLocalToCanvasMatrix = [&]() {
    glm::mat3 scaleMat =
        glm::scale(glm::identity<glm::mat3>(), glm::vec2(canvasScale, canvasScale));
    glm::mat3 translateMat = glm::translate(glm::identity<glm::mat3>(), gv.canvasOffset);
    glm::mat3 windowTranslate =
        glm::translate(glm::identity<glm::mat3>(),
                       glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 4));
    return windowTranslate * scaleMat * translateMat;
  };
  glm::mat3 const toCanvas = calcLocalToCanvasMatrix();

  ImDrawList* drawList = ImGui::GetWindowDrawList();

  // Grid
  float const GRID_SZ    = 24.0f;
  ImU32 const GRID_COLOR = IM_COL32(80, 80, 80, 40);
  if (gv.drawGrid && GRID_SZ * canvasScale >= 8.f) {
    auto gridOffset = toCanvas * glm::vec3(0, 0, 1);
    for (float x = fmodf(gridOffset.x - winPos.x, GRID_SZ * canvasScale); x < canvasSize.x;
         x += GRID_SZ * canvasScale)
      drawList->AddLine(ImVec2(x, 0) + winPos, ImVec2(x, canvasSize.y) + winPos, GRID_COLOR);
    for (float y = fmodf(gridOffset.y - winPos.y, GRID_SZ * canvasScale); y < canvasSize.y;
         y += GRID_SZ * canvasScale)
      drawList->AddLine(ImVec2(0, y) + winPos, ImVec2(canvasSize.x, y) + winPos, GRID_COLOR);
  }

  // Selection Box
  AABB const aabb(imvec(gv.selectionBoxStart), imvec(gv.selectionBoxEnd));
  if (gv.uiState == GraphView::UIState::BOX_SELECTING) {
    drawList->AddRectFilled(aabb.min, aabb.max, SELECTION_BOX_COLOR);
  } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
    drawList->AddRectFilled(aabb.min, aabb.max, DESELECTION_BOX_COLOR);
  }

  // Nodes
  auto visibilityClipingArea = canvasArea;
  visibilityClipingArea.expand(8 * canvasScale);
  for (size_t i = 0; i < gv.graph->order().size(); ++i) {
    size_t const idx         = gv.graph->order()[i];
    auto const&  node        = gv.graph->nodes().at(idx);
    auto const   center      = toCanvas * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft     = {center.x - node.size.x / 2.f * canvasScale,
                            center.y - node.size.y / 2.f * canvasScale};
    ImVec2 const bottomright = {center.x + node.size.x / 2.f * canvasScale,
                                center.y + node.size.y / 2.f * canvasScale};

    if (!visibilityClipingArea.intersects(AABB(topleft, bottomright)))
      continue;

    auto const color = unconfirmedNodeSelection.find(idx) != unconfirmedNodeSelection.end()
                           ? highlight(node.color, 0.1f, 0.5f)
                       : gv.hoveredNode == idx
                           ? highlight(node.color, 0.02f, 0.3f)
                           : (gv.nodeSelection.find(idx) != gv.nodeSelection.end()
                                  ? highlight(node.color, -0.1f, -0.4f)
                                  : node.color);

    float const fontHeight = ImGui::GetFontSize();

    if (node.type == Node::Type::NORMAL) {
      // Node itself
      drawList->AddRectFilled(
          topleft, bottomright, imcolor(color), cornerRounding(6.f * canvasScale));

      // Selected highlight
      if (gv.nodeSelection.find(idx) != gv.nodeSelection.end() && canvasScale > 0.2)
        drawList->AddRect(topleft + ImVec2{-4 * canvasScale, -4 * canvasScale},
                          bottomright + ImVec2{4 * canvasScale, 4 * canvasScale},
                          imcolor(highlight(node.color, 0.1f, 0.6f)),
                          cornerRounding(8.f * canvasScale));

      // Pins
      for (int i = 0; i < node.numInputs; ++i) {
        size_t upnode   = gv.graph->upstreamNodeOf(idx, i);
        auto   pincolor = color;
        if (upnode != -1) {
          pincolor = gv.graph->noderef(upnode).color;
        }
        auto const currentPin = NodePin{NodePin::INPUT, idx, i};
        if (currentPin == gv.hoveredPin || currentPin == gv.activePin) {
          pincolor = highlight(pincolor, 0.1f, 0.4f, 0.5f);
        }
        drawList->AddCircleFilled(
            toCanvas * imvec(node.inputPinPos(i)), 4 * canvasScale, imcolor(pincolor));
      }
      for (int i = 0; i < node.numOutputs; ++i) {
        auto const currentPin = NodePin{NodePin::OUTPUT, idx, i};
        auto       pincolor   = color;
        if (currentPin == gv.hoveredPin || currentPin == gv.activePin) {
          pincolor = highlight(pincolor, 0.1f, 0.4f, 0.5f);
        }
        drawList->AddCircleFilled(
            toCanvas * imvec(node.outputPinPos(i)), 4 * canvasScale, imcolor(pincolor));
      }

      // Name
      if (gv.drawName && canvasScale > 0.33) {
        drawList->AddText(ImVec2{center.x, center.y} +
                              ImVec2{node.size.x / 2.f * canvasScale + 8, -fontHeight / 2.f},
                          imcolor(highlight(color, -0.8f, 0.6f, 0.6f)),
                          node.name.c_str());
      }
    } else if (node.type == Node::Type::ANCHOR) {
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

  // Draw Links
  for (auto const& link : gv.graph->links()) {
    drawLink(
        drawList,
        toCanvas *
            imvec(gv.graph->noderef(link.second.nodeIndex).outputPinPos(link.second.pinNumber)),
        toCanvas *
            imvec(gv.graph->noderef(link.first.nodeIndex).inputPinPos(link.first.pinNumber)),
        imcolor(highlight(gv.graph->noderef(link.second.nodeIndex).color, 0, 0.2f, 1.0f)),
        glm::clamp(1.f * canvasScale, 1.0f, 4.0f));
  }

  // Pending Links ...
  ImVec2 curveStart, curveEnd;
  bool   hasPendingLink = false;
  if (gv.uiState == GraphView::UIState::DRAGGING_LINK_HEAD) {
    curveEnd       = toCanvas * (imvec(gv.graph->noderef(gv.pendingLink.destiny.nodeIndex)
                                     .inputPinPos(gv.pendingLink.destiny.pinNumber)));
    curveStart     = mousePos;
    hasPendingLink = true;
  } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_TAIL) {
    curveStart     = toCanvas * (imvec(gv.graph->noderef(gv.pendingLink.source.nodeIndex)
                                       .outputPinPos(gv.pendingLink.source.pinNumber)));
    curveEnd       = mousePos;
    hasPendingLink = true;
  }
  if (hasPendingLink) {
    drawLink(drawList, curveStart, curveEnd);
  }

  // Link cutting stroke
  if (gv.uiState == GraphView::UIState::CUTING_LINK) {
    std::vector<ImVec2> stroke(gv.linkCuttingStroke.size());
    for (size_t i = 0; i < gv.linkCuttingStroke.size(); ++i)
      stroke[i] = imvec(toCanvas * glm::vec3(gv.linkCuttingStroke[i], 1.0f));
    drawList->AddPolyline(stroke.data(), stroke.size(), IM_COL32(255, 0, 0, 233), false, 2);
  }
}

void nodenetContextMenu(GraphView& gv)
{
  static char nodetype[512] = {0};
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
  ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(800, 1024));
  if (ImGui::BeginPopup("Create Node")) {
    memset(nodetype, 0, sizeof(nodetype));
    ImGui::SetKeyboardFocusHere(0);
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText(
            "##nodetype", nodetype, sizeof(nodetype), ImGuiInputTextFlags_EnterReturnsTrue)) {
      gv.uiState = GraphView::UIState::PLACING_NEW_NODE;
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
      ImGui::CloseCurrentPopup();
    ImGui::Separator();
    if (ImGui::MenuItem(nodetype, nullptr)) {
      gv.uiState = GraphView::UIState::PLACING_NEW_NODE;
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopItemWidth();
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

void updateNetworkView(GraphView& gv, char const* name)
{
  ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Name", nullptr, &gv.drawName);
      ImGui::MenuItem("Grid", nullptr, &gv.drawGrid);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      std::string fps    = fmt::format("FPS = {}", ImGui::GetIO().Framerate);
      std::string vtxcnt = fmt::format("Vertices = {}", ImGui::GetIO().MetricsRenderVertices);
      std::string idxcnt = fmt::format("Indices = {}", ImGui::GetIO().MetricsRenderIndices);
      ImGui::MenuItem(fps.c_str(), nullptr, nullptr);
      ImGui::MenuItem(vtxcnt.c_str(), nullptr, nullptr);
      ImGui::MenuItem(idxcnt.c_str(), nullptr, nullptr);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
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
  auto&        graph             = *gv.graph;
  auto const   canvasScale       = gv.canvasScale;
  auto const   canvasOffset      = gv.canvasOffset;
  auto const   canvasArea        = AABB(winPos, winPos + canvasSize);
  auto const   clipArea          = canvasArea.expanded(8 * canvasScale);
  bool const   mouseInsideCanvas = canvasArea.contains(mousePos);

  auto calcLocalToCanvasMatrix = [&]() {
    glm::mat3 scaleMat =
        glm::scale(glm::identity<glm::mat3>(), glm::vec2(gv.canvasScale, gv.canvasScale));
    glm::mat3 translateMat = glm::translate(glm::identity<glm::mat3>(), canvasOffset);
    glm::mat3 windowTranslate =
        glm::translate(glm::identity<glm::mat3>(),
                       glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 4));
    return windowTranslate * scaleMat * translateMat;
  };
  glm::mat3 const toCanvas = calcLocalToCanvasMatrix();
  glm::mat3 const toLocal  = glm::inverse(toCanvas);

  size_t  hoveredNode                       = -1;
  size_t  clickedNode                       = -1;
  NodePin hoveredPin                        = {NodePin::NONE, size_t(-1), -1},
          clickedPin                        = {NodePin::NONE, size_t(-1), -1};
  std::set<size_t> unconfirmedNodeSelection = gv.nodeSelection;
  gv.selectionBoxEnd                        = glm::vec2(mousePos.x, mousePos.y);
  AABB selectionBox(imvec(gv.selectionBoxStart), imvec(gv.selectionBoxEnd));

  for (size_t i = 0; i < graph.order().size(); ++i) {
    size_t const idx         = graph.order()[i];
    auto const&  node        = graph.nodes().at(idx);
    auto const   center      = toCanvas * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft     = {center.x - node.size.x / 2.f * canvasScale,
                            center.y - node.size.y / 2.f * canvasScale};
    ImVec2 const bottomright = {center.x + node.size.x / 2.f * canvasScale,
                                center.y + node.size.y / 2.f * canvasScale};

    AABB const nodebox(topleft, bottomright);
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
      for (int ipin = 0; ipin < node.numInputs; ++ipin) {
        if (glm::distance2(node.inputPinPos(ipin),
                           glm::xy(toLocal * glm::vec3{mousePos.x, mousePos.y, 1})) < 25) {
          hoveredPin = {NodePin::INPUT, idx, ipin};
        }
      }
      for (int opin = 0; opin < node.numOutputs; ++opin) {
        if (glm::distance2(node.outputPinPos(opin),
                           glm::xy(toLocal * glm::vec3{mousePos.x, mousePos.y, 1})) < 25) {
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
        graph.shiftToEnd(clickedNode);
        gv.uiState = GraphView::UIState::DRAGGING_NODES;
        if (gv.nodeSelection.find(clickedNode) == gv.nodeSelection.end()) {
          gv.nodeSelection = {clickedNode};
        }
      } else if (clickedPin.nodeIndex != -1) {
        if (clickedPin.type == NodePin::OUTPUT) {
          gv.uiState     = GraphView::UIState::DRAGGING_LINK_TAIL;
          gv.pendingLink = {{NodePin::OUTPUT, clickedPin.nodeIndex, clickedPin.pinNumber},
                            {NodePin::INPUT, size_t(-1), -1}};
        } else if (clickedPin.type == NodePin::INPUT) {
          gv.uiState     = GraphView::UIState::DRAGGING_LINK_HEAD;
          gv.pendingLink = {{NodePin::OUTPUT, size_t(-1), -1},
                            {NodePin::INPUT, clickedPin.nodeIndex, clickedPin.pinNumber}};
        }
      }

      auto const modkey = ImGui::GetIO().KeyMods;
      if (gv.uiState == GraphView::UIState::VIEWING) {
        gv.selectionBoxStart = {mousePos.x, mousePos.y};
        if (modkey & ImGuiKeyModFlags_Shift) {
          gv.uiState = GraphView::UIState::BOX_SELECTING;
        } else if (modkey & ImGuiKeyModFlags_Ctrl) {
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
      if (gv.uiState == GraphView::UIState::BOX_SELECTING ||
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
        auto pos = toLocal * mousePos;
        Node newnode;
        newnode.pos      = glm::vec2(pos.x, pos.y);
        size_t idx       = graph.addNode(newnode);
        gv.activeNode    = idx;
        gv.nodeSelection = {idx};
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_HEAD) {
        if (hoveredPin.type == NodePin::OUTPUT) {
          gv.graph->addLink(hoveredPin.nodeIndex,
                            hoveredPin.pinNumber,
                            gv.pendingLink.destiny.nodeIndex,
                            gv.pendingLink.destiny.pinNumber);
        } else if (hoveredPin.type == NodePin::NONE && hoveredNode != -1 &&
                   graph.noderef(hoveredNode).numOutputs == 1) {
          gv.graph->addLink(
              hoveredNode, 0, gv.pendingLink.destiny.nodeIndex, gv.pendingLink.destiny.pinNumber);
        }
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_TAIL) {
        if (hoveredPin.type == NodePin::INPUT) {
          gv.graph->addLink(gv.pendingLink.source.nodeIndex,
                            gv.pendingLink.source.pinNumber,
                            hoveredPin.nodeIndex,
                            hoveredPin.pinNumber);
        } else if (hoveredPin.type == NodePin::NONE && hoveredNode != -1 &&
                   graph.noderef(hoveredNode).numInputs == 1) {
          gv.graph->addLink(
              gv.pendingLink.source.nodeIndex, gv.pendingLink.source.pinNumber, hoveredNode, 0);
        }
      }
      gv.uiState = GraphView::UIState::VIEWING;
    }
    gv.hoveredNode = hoveredNode;
    gv.hoveredPin  = hoveredPin;
    gv.activePin   = clickedPin;

    // Paning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto const srcDelta = glm::xy(toLocal * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      gv.canvasOffset += srcDelta;
    }
    // Scaling
    if (abs(ImGui::GetIO().MouseWheel) > 0.1) {
      gv.canvasScale = glm::clamp(gv.canvasScale + ImGui::GetIO().MouseWheel / 20.f, 0.1f, 10.f);
      // cursor as scale center:
      glm::mat3  newXform     = calcLocalToCanvasMatrix();
      auto const cc           = mousePos;
      auto const ccInOldLocal = toLocal * cc;
      auto const ccInNewLocal = glm::inverse(newXform) * cc;
      gv.canvasOffset +=
          glm::vec2(ccInNewLocal.x - ccInOldLocal.x, ccInNewLocal.y - ccInOldLocal.y);
    }

    // Handle Keydown
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) {
      spdlog::debug("removing nodes [{}] from view {}", fmt::join(gv.nodeSelection, ", "), name);
      graph.removeNodes(gv.nodeSelection);
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)) &&
               gv.uiState == GraphView::UIState::VIEWING) {
      ImGui::OpenPopup("Create Node");
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)) &&
               gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
      gv.uiState = GraphView::UIState::VIEWING;
    }
  } // Mouse inside canvas?

  // Dragging can go beyond canvas
  if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10)) {
    if (gv.uiState == GraphView::UIState::VIEWING && ImGui::IsWindowHovered() &&
        mouseInsideCanvas) {
      gv.uiState = GraphView::UIState::BOX_SELECTING;
      gv.nodeSelection.clear();
    } else if (gv.uiState == GraphView::UIState::DRAGGING_NODES) { // drag nodes
      auto const srcDelta = glm::xy(toLocal * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      if (glm::length(srcDelta) > 0) {
        for (size_t idx : gv.nodeSelection)
          graph.noderef(idx).pos += srcDelta;
      }
    } else if (ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Y))) { // cut links
      gv.uiState = GraphView::UIState::CUTING_LINK;
      auto mp    = toLocal * mousePos;
      gv.linkCuttingStroke.push_back(glm::vec2(mp.x, mp.y));
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    // confirm node selection
    if (gv.uiState == GraphView::UIState::BOX_SELECTING ||
        gv.uiState == GraphView::UIState::BOX_DESELECTING) {
      if (!ImGui::IsWindowHovered() || !mouseInsideCanvas)
        gv.nodeSelection = unconfirmedNodeSelection;
    }
    // confirm link cutting
    if (!gv.linkCuttingStroke.empty()) {
      std::vector<ImVec2> cutStroke(gv.linkCuttingStroke.size());
      cutStroke[0] = imvec(gv.linkCuttingStroke.front());
      AABB cutterbox(cutStroke[0]);
      for (size_t i = 1; i < gv.linkCuttingStroke.size(); ++i) {
        auto const& pos = imvec(gv.linkCuttingStroke[i]);
        cutterbox.merge(pos);
        cutStroke[i] = pos;
      }
      cutterbox.expand(100);
      std::vector<NodePin> dstPinsToDelete;
      for (auto const& link : graph.links()) {
        auto const linkStart = imvec(graph.noderef(link.second.nodeIndex).pos);
        auto const linkEnd   = imvec(graph.noderef(link.first.nodeIndex).pos);
        if (cutterbox.intersects(AABB(linkStart, linkEnd))) {
          std::vector<ImVec2> linkPath;
          genLinkPath(linkPath, linkStart, linkEnd, {10, 100});
          if (strokeIntersects(linkPath, cutStroke)) {
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

  nodenetContextMenu(gv);

  ImGui::EndChild();
  ImGui::PopStyleVar();   // frame padding
  ImGui::PopStyleVar();   // window padding
  ImGui::PopStyleColor(); // child bg
  ImGui::End();
}

void updateAndDraw(GraphView& gv, char const* name)
{
  updateNetworkView(gv, name);

  // node inspector
  Node* toInspect =
      gv.nodeSelection.size() == 1 ? &gv.graph->noderef(*gv.nodeSelection.begin()) : nullptr;
  updateInspectorView(toInspect, name);
}

} // namespace editorui
