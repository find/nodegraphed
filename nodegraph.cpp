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

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}
static inline ImVec2 operator*(const glm::mat3& m, const ImVec2& v) {
  auto r = m * glm::vec3(v.x, v.y, 1.0f);
  return ImVec2(r.x, r.y);
}
static inline float length(const ImVec2& v) {
  return sqrt(v.x * v.x + v.y * v.y);
}
static inline float cornerRounding(float r) { return r > 1 ? r : 0.f; }
static inline glm::vec4 highlight(glm::vec4 const& color, float dSat = 0.1f,
                                  float dLum = 0.1f, float dAlpha = 0.1f) {
  auto hsv = glm::hsvColor(glm::xyz(color));
  return glm::vec4(
      glm::rgbColor(glm::clamp(
          glm::vec3(hsv.x, hsv.y * (1.0f + dSat), hsv.z * (1.0f + dLum)),
          glm::vec3{0, 0, 0}, glm::vec3{360, 1, 1})),
      glm::clamp(color.a * (1.0f + dAlpha)));
}
static inline ImVec2 imvec(glm::vec2 const& v) { return {v.x, v.y}; }
static inline ImU32 imcolor(glm::vec4 const& color) {
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4{color.r, color.g, color.b, color.a});
}

struct AABB {
  ImVec2 min, max;

  AABB(ImVec2 const& a) { min = max = a; }
  AABB(ImVec2 const& a, ImVec2 const& b) : AABB(a) { merge(b); }
  void merge(ImVec2 const& v) {
    min.x = std::min(min.x, v.x);
    min.y = std::min(min.y, v.y);
    max.x = std::max(max.x, v.x);
    max.y = std::max(max.y, v.y);
  }
  void merge(AABB const& aabb) {
    min.x = std::min(min.x, aabb.min.x);
    min.y = std::min(min.y, aabb.min.y);
    max.x = std::max(max.x, aabb.max.x);
    max.y = std::max(max.y, aabb.max.y);
  }
  void expand(float amount) {
    min.x -= amount;
    min.y -= amount;
    max.x += amount;
    max.y += amount;
  }
  AABB expanded(float amount) const {
    AABB ex = *this;
    ex.expand(amount);
    return ex;
  }
  // test if the that is contained inside this
  bool contains(AABB const& that) const {
    return min.x <= that.min.x && min.y <= that.min.y && max.x >= that.max.x &&
           max.y >= that.max.y;
  }
  // test if point is inside this
  bool contains(ImVec2 const& pt) const {
    return pt.x <= max.x && pt.y <= max.y && pt.x >= min.x && pt.y >= min.y;
  }
  // test if the two AABBs has intersection
  bool intersects(AABB const& that) const {
    return !(max.x < that.min.x || that.max.x < min.x || max.y < that.min.y ||
             that.max.y < min.y);
  }
};

namespace editorui {

NodeIdAllocator* NodeIdAllocator::instance_ = nullptr;
NodeIdAllocator& NodeIdAllocator::instance() {
  static std::unique_ptr<NodeIdAllocator> s_instance(new NodeIdAllocator);
  if (instance_ == nullptr) instance_ = s_instance.get();
  return *s_instance;
}

void GraphView::onGraphChanged() {
  if (graph) {
    if (!nodeSelection.empty()) {
      std::vector<size_t> invalidIndices;
      for (size_t idx : nodeSelection) {
        if (graph->nodes().find(idx) == graph->nodes().end()) {
          invalidIndices.push_back(idx);
        }
      }
      for (size_t idx : invalidIndices) nodeSelection.erase(idx);
    }
    if (activeNode != -1 &&
        graph->nodes().find(activeNode) != graph->nodes().end())
      activeNode = -1;
  }
}

void updateInspectorView(Node* node, char const* name) {
  auto title = fmt::format("Param Inspector##inspector{}", name);
  if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }

  if (node) {
    ImGui::Text(node->name.c_str());
    ImGui::SliderInt("Number of Inputs", &node->numInputs, 0, 10);
    ImGui::SliderInt("Number of Outputs", &node->numOutputs, 0, 10);
    ImGui::ColorEdit4("Color", &node->color.r);
  } else {
    ImGui::Text("nothing selected");
  }

  ImGui::End();
}

static void genLinkPath(std::vector<ImVec2> &buffer, ImVec2 const& start, ImVec2 const& end, ImVec2 const& angledSegmentLengthRange) {
  buffer.clear();
  float const xcenter = (start.x + end.x) * 0.5f;
  float const ycenter = (start.y + end.y) * 0.5f;
  float const minAngledLength = angledSegmentLengthRange.x;
  float const maxAngledLength = angledSegmentLengthRange.y;

}
static void drawLink(ImDrawList* drawList, ImVec2 const& start,
                     ImVec2 const& end,
                     ImU32 color = IM_COL32(200, 200, 200, 200),
                     float thickness = 1.0f) {
  ImVec2 const pts[] = {start, ImVec2(start.x, glm::mix(start.y, end.y, 0.33f)),
                        ImVec2(end.x, glm::mix(start.y, end.y, 0.67f)), end};
  drawList->AddPolyline(pts, 4, color, false, thickness);
}

void drawGraph(GraphView const& gv, size_t hoveredNode,
               std::set<size_t> const& unconfirmedNodeSelection) {
  // Draw Nodes
  ImU32 const PENDING_PLACE_NODE_COLOR = IM_COL32(160, 160, 160, 64);
  ImU32 const SELECTION_BOX_COLOR = IM_COL32(60, 110, 60, 128);
  ImU32 const DESELECTION_BOX_COLOR = IM_COL32(140, 60, 60, 128);

  ImVec2 const canvasSize = ImGui::GetWindowSize();
  ImVec2 const mousePos = ImGui::GetMousePos();
  ImVec2 const winPos = ImGui::GetCursorScreenPos();
  ImVec2 const mouseDelta = ImGui::GetIO().MouseDelta;
  auto const canvasScale = gv.canvasScale;
  auto const canvasOffset = gv.canvasOffset;
  auto const canvasArea = AABB(winPos, winPos + canvasSize);
  bool const cursorInsideCanvas = canvasArea.contains(mousePos);

  auto calcLocalToCanvasMatrix = [&]() {
    glm::mat3 scaleMat = glm::scale(glm::identity<glm::mat3>(),
                                    glm::vec2(canvasScale, canvasScale));
    glm::mat3 translateMat =
        glm::translate(glm::identity<glm::mat3>(), gv.canvasOffset);
    glm::mat3 windowTranslate = glm::translate(
        glm::identity<glm::mat3>(),
        glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 4));
    return windowTranslate * scaleMat * translateMat;
  };
  glm::mat3 const toCanvas = calcLocalToCanvasMatrix();

  ImDrawList* drawList = ImGui::GetWindowDrawList();

  // Grid
  float const GRID_SZ = 24.0f;
  ImU32 const GRID_COLOR = IM_COL32(80, 80, 80, 40);
  if (gv.drawGrid && GRID_SZ * canvasScale >= 8.f) {
    auto gridOffset = toCanvas * glm::vec3(0, 0, 1);
    for (float x = fmodf(gridOffset.x - winPos.x, GRID_SZ * canvasScale);
         x < canvasSize.x; x += GRID_SZ * canvasScale)
      drawList->AddLine(ImVec2(x, 0) + winPos, ImVec2(x, canvasSize.y) + winPos,
                        GRID_COLOR);
    for (float y = fmodf(gridOffset.y - winPos.y, GRID_SZ * canvasScale);
         y < canvasSize.y; y += GRID_SZ * canvasScale)
      drawList->AddLine(ImVec2(0, y) + winPos, ImVec2(canvasSize.x, y) + winPos,
                        GRID_COLOR);
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
    size_t const idx = gv.graph->order()[i];
    auto const& node = gv.graph->nodes().at(idx);
    auto const center = toCanvas * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft = {center.x - node.size.x / 2.f * canvasScale,
                            center.y - node.size.y / 2.f * canvasScale};
    ImVec2 const bottomright = {center.x + node.size.x / 2.f * canvasScale,
                                center.y + node.size.y / 2.f * canvasScale};

    if (!visibilityClipingArea.intersects(AABB(topleft, bottomright))) continue;

    ImU32 const color =
        unconfirmedNodeSelection.find(idx) != unconfirmedNodeSelection.end()
            ? imcolor(highlight(node.color, 0.1f, 0.5f))
            : hoveredNode == idx ? imcolor(highlight(node.color, 0.02f, 0.3f))
                                 : imcolor(node.color);

    if (node.type == Node::Type::NORMAL) {
      drawList->AddRectFilled(topleft, bottomright, color,
                              cornerRounding(6.f * canvasScale));

      if (gv.nodeSelection.find(idx) != gv.nodeSelection.end() && canvasScale > 0.2)
        drawList->AddRect(
            topleft + ImVec2{-4 * canvasScale, -4 * canvasScale},
            bottomright + ImVec2{4 * canvasScale, 4 * canvasScale},
            imcolor(highlight(node.color, 0.1f, 0.6f)),
            cornerRounding(8.f * canvasScale));

      for (int i = 0; i < node.numInputs; ++i) {
        drawList->AddCircleFilled(toCanvas * imvec(node.inputPinPos(i)),
                                  4 * canvasScale, color);
      }
      for (int i = 0; i < node.numOutputs; ++i) {
        drawList->AddCircleFilled(toCanvas * imvec(node.outputPinPos(i)),
                                  4 * canvasScale, color);
      }
    } else if (node.type == Node::Type::ANCHOR) {
      drawList->AddCircleFilled(imvec(center), 8, color);
    }
  }

  // Pending node
  if (gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
    auto const center = mousePos;
    ImVec2 const topleft = {center.x - DEFAULT_NODE_SIZE.x / 2.f * canvasScale,
                            center.y - DEFAULT_NODE_SIZE.y / 2.f * canvasScale};
    ImVec2 const bottomright = {
        center.x + DEFAULT_NODE_SIZE.x / 2.f * canvasScale,
        center.y + DEFAULT_NODE_SIZE.y / 2.f * canvasScale};
    drawList->AddRectFilled(topleft, bottomright, PENDING_PLACE_NODE_COLOR,
                            cornerRounding(6.f * canvasScale));
  }

  // Draw Links
  for (auto const& link: gv.graph->links()) {
    drawLink(
        drawList,
        toCanvas *
            imvec(gv.graph->noderef(link.srcNode).outputPinPos(link.srcPin)),
        toCanvas *
            imvec(gv.graph->noderef(link.dstNode).inputPinPos(link.dstPin)),
        imcolor(highlight(gv.graph->noderef(link.srcNode).color, 0, 0.2f, 1.0f))
      );
  }

  // Pending Links ...
  ImVec2 curveStart, curveEnd;
  bool hasPendingLink = false;
  if (gv.uiState == GraphView::UIState::DRAGGING_LINK_HEAD) {
    curveEnd = toCanvas * (imvec(gv.graph->noderef(gv.pendingLink.dstNode)
      .inputPinPos(gv.pendingLink.dstPin)));
    curveStart = mousePos;
    hasPendingLink = true;
  }
  else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_TAIL) {
    curveStart = toCanvas * (imvec(gv.graph->noderef(gv.pendingLink.srcNode)
                              .outputPinPos(gv.pendingLink.srcPin)));
    curveEnd = mousePos;
    hasPendingLink = true;
  }
  if (hasPendingLink) {
    drawLink(drawList, curveStart, curveEnd);
  }
}

void nodenetContextMenu(GraphView& gv) {
  static char nodetype[512] = {0};
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
  ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(800, 1024));
  if (ImGui::BeginPopup("Create Node")) {
    memset(nodetype, 0, sizeof(nodetype));
    ImGui::SetKeyboardFocusHere(0);
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##nodetype", nodetype, sizeof(nodetype),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
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

void updateNetworkView(GraphView& gv, char const* name) {
  ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Grid", nullptr, &gv.drawGrid);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 25, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  ImGui::BeginChild("Canvas", ImVec2(0, 0), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 const canvasSize = ImGui::GetWindowSize();
  ImVec2 const mousePos = ImGui::GetMousePos();
  ImVec2 const winPos = ImGui::GetCursorScreenPos();
  ImVec2 const mouseDelta = ImGui::GetIO().MouseDelta;
  auto& graph = *gv.graph;
  auto const canvasScale = gv.canvasScale;
  auto const canvasOffset = gv.canvasOffset;
  auto const canvasArea = AABB(winPos, winPos + canvasSize);
  auto const clipArea = canvasArea.expanded(8 * canvasScale);
  bool const mouseInsideCanvas = canvasArea.contains(mousePos);

  auto calcLocalToCanvasMatrix = [&]() {
    glm::mat3 scaleMat = glm::scale(glm::identity<glm::mat3>(),
                                    glm::vec2(gv.canvasScale, gv.canvasScale));
    glm::mat3 translateMat =
        glm::translate(glm::identity<glm::mat3>(), canvasOffset);
    glm::mat3 windowTranslate = glm::translate(
        glm::identity<glm::mat3>(),
        glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 4));
    return windowTranslate * scaleMat * translateMat;
  };
  glm::mat3 const toCanvas = calcLocalToCanvasMatrix();
  glm::mat3 const toLocal = glm::inverse(toCanvas);

  size_t hoveredNode = -1;
  size_t clickedNode = -1;
  enum PinType { INPUT, OUTPUT, NONE };
  struct {
    size_t node;
    int pin;
    PinType type;
  } hoveredPin = {size_t(-1), -1, NONE}, clickedPin = {size_t(-1), -1, NONE};
  std::set<size_t> unconfirmedNodeSelection = gv.nodeSelection;
  gv.selectionBoxEnd = glm::vec2(mousePos.x, mousePos.y);
  AABB selectionBox(imvec(gv.selectionBoxStart), imvec(gv.selectionBoxEnd));

  for (size_t i = 0; i < graph.order().size(); ++i) {
    size_t const idx = graph.order()[i];
    auto const& node = graph.nodes().at(idx);
    auto const center = toCanvas * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft = {center.x - node.size.x / 2.f * canvasScale,
                            center.y - node.size.y / 2.f * canvasScale};
    ImVec2 const bottomright = {center.x + node.size.x / 2.f * canvasScale,
                                center.y + node.size.y / 2.f * canvasScale};

    AABB const nodebox(topleft, bottomright);
    if (!clipArea.intersects(nodebox)) continue;

    if (nodebox.contains(mousePos) && mouseInsideCanvas) hoveredNode = idx;

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
                           glm::xy(toLocal*glm::vec3{mousePos.x, mousePos.y,1})) < 25) {
          hoveredPin = {idx, ipin, INPUT};
        }
      }
      for (int opin = 0; opin < node.numOutputs; ++opin) {
        if (glm::distance2(node.outputPinPos(opin),
                           glm::xy(toLocal*glm::vec3{mousePos.x, mousePos.y, 1})) < 25) {
          hoveredPin = {idx, opin, OUTPUT};
        }
      }
    }
  }

  // Mouse action - the dirty part
  if (mouseInsideCanvas && ImGui::IsWindowHovered()) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      clickedNode = hoveredNode;
      clickedPin = hoveredPin;
      gv.activeNode = clickedNode;
      if (clickedNode != -1) {
        graph.shiftToEnd(clickedNode);
        gv.uiState = GraphView::UIState::DRAGGING_NODES;
        if (gv.nodeSelection.find(clickedNode) == gv.nodeSelection.end()) {
          gv.nodeSelection = {clickedNode};
        }
      } else if (clickedPin.node != -1) {
        if (clickedPin.type == OUTPUT) {
          gv.uiState = GraphView::UIState::DRAGGING_LINK_TAIL;
          gv.pendingLink = {clickedPin.node, clickedPin.pin, size_t(-1), -1};
        } else if (clickedPin.type == INPUT) {
          gv.uiState = GraphView::UIState::DRAGGING_LINK_HEAD;
          gv.pendingLink = {size_t(-1), -1, clickedPin.node, clickedPin.pin};
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
          if (clickedNode != -1) gv.nodeSelection.insert(clickedNode);
        } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
          if (clickedNode != -1) gv.nodeSelection.erase(clickedNode);
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
                   glm::distance(gv.selectionBoxStart, gv.selectionBoxEnd) <
                       4) {
          gv.nodeSelection = {gv.activeNode};
        }
      } else if (gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
        auto pos = toLocal * mousePos;
        Node newnode;
        newnode.pos = glm::vec2(pos.x, pos.y);
        size_t idx = graph.addNode(newnode);
        gv.activeNode = idx;
        gv.nodeSelection = {idx};
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_HEAD) {
        if (hoveredPin.type == OUTPUT) {
          gv.graph->addLink(hoveredPin.node, hoveredPin.pin,
                            gv.pendingLink.dstNode, gv.pendingLink.dstPin);
        } else if (hoveredPin.type == NONE && hoveredNode != -1 &&
                   graph.noderef(hoveredNode).numOutputs == 1) {
          gv.graph->addLink(hoveredNode, 0,
                            gv.pendingLink.dstNode,
                            gv.pendingLink.dstPin);
        }
      } else if (gv.uiState == GraphView::UIState::DRAGGING_LINK_TAIL) {
        if (hoveredPin.type == INPUT) {
          gv.graph->addLink(gv.pendingLink.srcNode, gv.pendingLink.srcPin,
                            hoveredPin.node, hoveredPin.pin);
        } else if (hoveredPin.type == NONE && hoveredNode != -1 &&
                   graph.noderef(hoveredNode).numInputs == 1) {
          gv.graph->addLink(gv.pendingLink.srcNode, gv.pendingLink.srcPin,
                            hoveredNode, 0);
        }
      }
      gv.uiState = GraphView::UIState::VIEWING;
    }

    // Paning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto const srcDelta =
          glm::xy(toLocal * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      gv.canvasOffset += srcDelta;
    }
    // Scaling
    if (abs(ImGui::GetIO().MouseWheel) > 0.1) {
      gv.canvasScale = glm::clamp(
          gv.canvasScale + ImGui::GetIO().MouseWheel / 20.f, 0.1f, 10.f);
      // cursor as scale center:
      glm::mat3 newXform = calcLocalToCanvasMatrix();
      auto const cc = mousePos;
      auto const ccInOldLocal = toLocal * cc;
      auto const ccInNewLocal = glm::inverse(newXform) * cc;
      gv.canvasOffset += glm::vec2(ccInNewLocal.x - ccInOldLocal.x,
                                   ccInNewLocal.y - ccInOldLocal.y);
    }

    // Handle Keydown
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) {
      spdlog::debug("removing nodes [{}] from view {}",
                    fmt::join(gv.nodeSelection, ", "), name);
      graph.removeNodes(gv.nodeSelection);
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)) &&
               gv.uiState == GraphView::UIState::VIEWING) {
      ImGui::OpenPopup("Create Node");
    } else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)) &&
               gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
      gv.uiState = GraphView::UIState::VIEWING;
    }
  }  // Mouse inside canvas?

  // Dragging can go beyond canvas
  if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10)) {
    if (gv.uiState == GraphView::UIState::VIEWING && ImGui::IsWindowHovered() &&
        mouseInsideCanvas) {
      gv.uiState = GraphView::UIState::BOX_SELECTING;
      gv.nodeSelection.clear();
    } else if (gv.uiState == GraphView::UIState::DRAGGING_NODES) {
      auto const srcDelta =
          glm::xy(toLocal * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      if (glm::length(srcDelta) > 0) {
        for (size_t idx : gv.nodeSelection) graph.noderef(idx).pos += srcDelta;
      }
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (gv.uiState == GraphView::UIState::BOX_SELECTING ||
        gv.uiState == GraphView::UIState::BOX_DESELECTING) {
      if (!ImGui::IsWindowHovered() || !mouseInsideCanvas)
        gv.nodeSelection = unconfirmedNodeSelection;
    }
    gv.uiState = GraphView::UIState::VIEWING;
  }

  drawGraph(gv, hoveredNode, unconfirmedNodeSelection);

  nodenetContextMenu(gv);

  ImGui::EndChild();
  ImGui::PopStyleVar();    // frame padding
  ImGui::PopStyleVar();    // window padding
  ImGui::PopStyleColor();  // child bg
  ImGui::End();
}

void updateAndDraw(GraphView& gv, char const* name) {
  updateNetworkView(gv, name);

  // node inspector
  Node* toInspect = gv.nodeSelection.size() == 1
                        ? &gv.graph->noderef(*gv.nodeSelection.begin())
                        : nullptr;
  updateInspectorView(toInspect, name);
}

}  // namespace editorui
