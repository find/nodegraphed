#include "nodegraph.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <glm/ext.hpp>
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
static inline ImVec2 toImVec(glm::vec2 const& v) { return {v.x, v.y}; }

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
  // test if the that is contained inside this
  bool contains(AABB const& that) const {
    return min.x <= that.min.x &&
           min.y <= that.min.y &&
           max.x >= that.max.x &&
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
  if (instance_ == nullptr)
    instance_ = s_instance.get();
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
      for (size_t idx : invalidIndices)
        nodeSelection.erase(idx);
    }
    if (activeNode != -1 && graph->nodes().find(activeNode) != graph->nodes().end())
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

void drawGraph(GraphView const& gv, size_t hoveredNode, std::set<size_t> const& unconfirmedNodeSelection) {
  // Draw Nodes
  ImU32 const DEFAULT_NODE_COLOR = IM_COL32(160, 160, 160, 128);
  ImU32 const HOVER_NODE_COLOR = IM_COL32(200, 200, 200, 128);
  ImU32 const SELECTED_NODE_COLOR = IM_COL32(233, 233, 233, 180);
  ImU32 const SELECTING_NODE_COLOR = IM_COL32(233, 233, 233, 233);
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
        glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 2));
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
  AABB const aabb(toImVec(gv.selectionBoxStart), toImVec(gv.selectionBoxEnd));
  if (gv.uiState == GraphView::UIState::BOX_SELECTING) {
    drawList->AddRectFilled(aabb.min, aabb.max,
                            SELECTION_BOX_COLOR);
  } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
    drawList->AddRectFilled(aabb.min, aabb.max,
                            DESELECTION_BOX_COLOR);
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

    if (!visibilityClipingArea.intersects(AABB(topleft, bottomright)))
      continue;

    ImU32 const color =
        unconfirmedNodeSelection.find(idx) != unconfirmedNodeSelection.end()
            ? SELECTING_NODE_COLOR
            : hoveredNode == idx ? HOVER_NODE_COLOR : DEFAULT_NODE_COLOR;

    if (node.type == Node::Type::NORMAL) {
      drawList->AddRectFilled(topleft, bottomright, color, 6.f * canvasScale);

      if (gv.nodeSelection.find(idx) != gv.nodeSelection.end())
        drawList->AddRect(topleft, bottomright, IM_COL32(255, 255, 255, 255),
          6.f * canvasScale);

      for (int i = 0; i < node.numInputs; ++i) {
        drawList->AddCircleFilled(toCanvas*toImVec(node.inputPinPos(i)-glm::vec2(0,3)), 4*canvasScale, color);
      }
      for (int i = 0; i < node.numOutputs; ++i) {
        drawList->AddCircleFilled(toCanvas*toImVec(node.outputPinPos(i)+glm::vec2(0,3)), 4*canvasScale, color);
      }
    } else if (node.type == Node::Type::ANCHOR) {
      drawList->AddCircleFilled(toImVec(center), 8, color);
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
                            6.f * canvasScale);
  }
  // Draw Links
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
  bool const mouseInsideCanvas = canvasArea.contains(mousePos);

  auto calcLocalToCanvasMatrix = [&]() {
    glm::mat3 scaleMat =
        glm::scale(glm::identity<glm::mat3>(),
                   glm::vec2(canvasScale, canvasScale));
    glm::mat3 translateMat =
        glm::translate(glm::identity<glm::mat3>(), canvasOffset);
    glm::mat3 windowTranslate = glm::translate(
        glm::identity<glm::mat3>(),
        glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 2));
    return windowTranslate * scaleMat * translateMat;
  };
  glm::mat3 const toCanvas = calcLocalToCanvasMatrix();
  glm::mat3 const toLocal = glm::inverse(toCanvas);

  size_t hoveredNode = -1;
  size_t clickedNode = -1;
  std::set<size_t> unconfirmedNodeSelection = gv.nodeSelection;
  gv.selectionBoxEnd = glm::vec2(mousePos.x, mousePos.y);
  AABB selectionBox(toImVec(gv.selectionBoxStart), toImVec(gv.selectionBoxEnd));

  for (size_t i = 0; i < graph.order().size(); ++i) {
    size_t const idx = graph.order()[i];
    auto const& node = graph.nodes().at(idx);
    auto const center = toCanvas * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft = {center.x - node.size.x / 2.f * canvasScale,
                            center.y - node.size.y / 2.f * canvasScale};
    ImVec2 const bottomright = {
        center.x + node.size.x / 2.f * canvasScale,
        center.y + node.size.y / 2.f * canvasScale};

    AABB const nodebox(topleft, bottomright);

    if (nodebox.contains(mousePos) && mouseInsideCanvas)
      hoveredNode = idx;

    if (selectionBox.intersects(nodebox)) {
      if (gv.uiState == GraphView::UIState::BOX_SELECTING) {
        unconfirmedNodeSelection.insert(idx);
      } else if (gv.uiState == GraphView::UIState::BOX_DESELECTING) {
        unconfirmedNodeSelection.erase(idx);
      }
    }
  }

  // Mouse action - the dirty part
  if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
     ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
     ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    gv.isActiveView = mouseInsideCanvas;
  }
  if (mouseInsideCanvas) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      clickedNode = hoveredNode;
      gv.activeNode = clickedNode;
      if (clickedNode != -1) {
        graph.shiftToEnd(clickedNode);
        gv.uiState = GraphView::UIState::DRAGGING_NODES;
        if (gv.nodeSelection.find(clickedNode)==gv.nodeSelection.end()) {
          gv.nodeSelection = { clickedNode };
        }
      }

      auto const modkey = ImGui::GetIO().KeyMods;
      if (gv.uiState == GraphView::UIState::VIEWING) {
        gv.selectionBoxStart = glm::vec2(mousePos.x, mousePos.y);
        if (modkey & (ImGuiKeyModFlags_Shift | (~ImGuiKeyModFlags_Ctrl))) {
          gv.uiState = GraphView::UIState::BOX_SELECTING;
        } else if (modkey & (ImGuiKeyModFlags_Ctrl | (~ImGuiKeyModFlags_Shift))) {
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
          glm::distance(gv.selectionBoxStart, gv.selectionBoxEnd) < 4) {
          gv.nodeSelection = { gv.activeNode };
        }
      } else if (gv.uiState == GraphView::UIState::PLACING_NEW_NODE) {
        auto pos = toLocal * mousePos;
        Node newnode;
        newnode.pos = glm::vec2(pos.x, pos.y);
        size_t idx = graph.addNode(newnode);
        gv.activeNode = idx;
        gv.nodeSelection = { idx };
        gv.onGraphChanged();
      }
      gv.uiState = GraphView::UIState::VIEWING;
    }

    // Scaling
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto const srcDelta =
        glm::xy(toLocal * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      gv.canvasOffset += srcDelta;
    }
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
      spdlog::debug("removing nodes [{}] from view {}", fmt::join(gv.nodeSelection, ", "), name);
      graph.removeNodes(gv.nodeSelection);
      gv.onGraphChanged();
      // if (gv.nodeSelection.find(gv.activeNode) != gv.nodeSelection.end())
      //   gv.activeNode = -1;
      // gv.nodeSelection.clear();
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
    if (gv.uiState == GraphView::UIState::VIEWING && gv.isActiveView && mouseInsideCanvas) {
      gv.uiState = GraphView::UIState::BOX_SELECTING;
      gv.nodeSelection.clear();
    } else if (gv.uiState == GraphView::UIState::DRAGGING_NODES) {
      auto const srcDelta =
        glm::xy(toLocal * glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      if (glm::length(srcDelta) > 0) {
        for (size_t idx : gv.nodeSelection)
          graph.noderef(idx).pos += srcDelta;
      }
    }
  }
  if (!mouseInsideCanvas && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
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
  Node* toInspect = gv.nodeSelection.size()==1
                    ? &gv.graph->noderef(*gv.nodeSelection.begin())
                    : nullptr;
  updateInspectorView(toInspect, name);
}

}  // namespace editorui