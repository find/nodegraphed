#include "nodegraph.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <glm/ext.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/vec_swizzle.hpp>

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

namespace editorui {

void draw(Graph& graph) {
  ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Node Graph", nullptr, ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Grid", nullptr, &graph.showGrid);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 25, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  ImGui::BeginChild("Canvas", ImVec2(0, 0), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 const canvasSize = ImGui::GetWindowSize();
  ImVec2 const mousePos = ImGui::GetMousePos();
  ImVec2 const winPos = ImGui::GetCursorScreenPos();
  ImVec2 const mouseDelta = ImGui::GetIO().MouseDelta;

  auto calcViewMatrix = [&]() {
    glm::mat3 viewscale = glm::scale(glm::identity<glm::mat3>(),
                                     glm::vec2(graph.scale, graph.scale));
    glm::mat3 viewtranslate =
        glm::translate(glm::identity<glm::mat3>(), graph.offset);
    glm::mat3 windowtranslate = glm::translate(
        glm::identity<glm::mat3>(),
        glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 2));
    return windowtranslate * viewscale * viewtranslate;
  };
  glm::mat3 viewxform = calcViewMatrix();

  // Display grid
  float const GRID_SZ = 24.0f;
  ImU32 const GRID_COLOR = IM_COL32(80, 80, 80, 40);
  if (graph.showGrid && GRID_SZ * graph.scale >= 8.f) {
    auto gridOffset = viewxform * glm::vec3(0, 0, 1);
    for (float x = fmodf(gridOffset.x - winPos.x, GRID_SZ * graph.scale);
         x < canvasSize.x; x += GRID_SZ * graph.scale)
      draw_list->AddLine(ImVec2(x, 0) + winPos,
                         ImVec2(x, canvasSize.y) + winPos, GRID_COLOR);
    for (float y = fmodf(gridOffset.y - winPos.y, GRID_SZ * graph.scale);
         y < canvasSize.y; y += GRID_SZ * graph.scale)
      draw_list->AddLine(ImVec2(0, y) + winPos,
                         ImVec2(canvasSize.x, y) + winPos, GRID_COLOR);
  }

  // Draw Nodes
  graph.initOrder();
  size_t hoveredNode = -1;
  size_t clickedNode = -1;
  ImU32 const DEFAULT_NODE_COLOR = IM_COL32(160, 160, 160, 128);
  ImU32 const HOVER_NODE_COLOR = IM_COL32(200, 200, 200, 128);
  ImU32 const SELECTED_NODE_COLOR = IM_COL32(233, 233, 233, 180);
  ImU32 const SELECTION_BOX_COLOR = IM_COL32(60, 140, 60, 128);
  ImU32 const DESELECTION_BOX_COLOR = IM_COL32(140, 60, 60, 128);

  ImVec2 const NODE_SIZE = {64, 24};
  // first pass: update states
  std::set<size_t> nodeSelection = {};
  graph.selectionBoxEnd = glm::vec2(mousePos.x, mousePos.y);
  float const sbbminx =
      std::min(graph.selectionBoxStart.x, graph.selectionBoxEnd.x);
  float const sbbmaxx =
      std::max(graph.selectionBoxStart.x, graph.selectionBoxEnd.x);
  float const sbbminy =
      std::min(graph.selectionBoxStart.y, graph.selectionBoxEnd.y);
  float const sbbmaxy =
      std::max(graph.selectionBoxStart.y, graph.selectionBoxEnd.y);
  if (graph.operationState == Graph::OperationState::BOX_SELECTING) {
    draw_list->AddRectFilled(ImVec2(sbbminx, sbbminy), ImVec2(sbbmaxx, sbbmaxy),
                             SELECTION_BOX_COLOR);
  } else if (graph.operationState == Graph::OperationState::BOX_DESELECTING) {
    draw_list->AddRectFilled(ImVec2(sbbminx, sbbminy), ImVec2(sbbmaxx, sbbmaxy),
                             DESELECTION_BOX_COLOR);
  }
  for (size_t i = 0; i < graph.nodeorder.size(); ++i) {
    size_t const idx = graph.nodeorder[i];
    auto const& node = graph.nodes[idx];
    auto const center = viewxform * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft = {center.x - NODE_SIZE.x / 2.f * graph.scale,
                            center.y - NODE_SIZE.y / 2.f * graph.scale};
    ImVec2 const bottomright = {center.x + NODE_SIZE.x / 2.f * graph.scale,
                                center.y + NODE_SIZE.y / 2.f * graph.scale};

    if (mousePos.x >= topleft.x && mousePos.y >= topleft.y &&
        mousePos.x <= bottomright.x && mousePos.y <= bottomright.y)
      hoveredNode = idx;

    if (graph.operationState == Graph::OperationState::BOX_SELECTING ||
        graph.operationState == Graph::OperationState::BOX_DESELECTING) {
      if (topleft.x >= sbbminx && bottomright.x <= sbbmaxx &&
          topleft.y >= sbbminy && bottomright.y <= sbbmaxy) {
        nodeSelection.insert(idx);
      }
    }
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && length(mouseDelta) < 10) {
    clickedNode = hoveredNode;
    graph.selectedNode = clickedNode;
    graph.nodeSelection.insert(clickedNode);
    graph.shiftToEnd(clickedNode);

    auto const modkey = ImGui::GetIO().KeyMods;
    if (graph.operationState == Graph::OperationState::VIEWING) {
      if (modkey & (ImGuiKeyModFlags_Shift | (~ImGuiKeyModFlags_Ctrl))) {
        graph.operationState = Graph::OperationState::BOX_SELECTING;
        graph.selectionBoxStart = glm::vec2(mousePos.x, mousePos.y);
      } else if (modkey & (ImGuiKeyModFlags_Ctrl | (~ImGuiKeyModFlags_Shift))) {
        graph.operationState = Graph::OperationState::BOX_DESELECTING;
        graph.selectionBoxStart = glm::vec2(mousePos.x, mousePos.y);
      }
    } else {
      graph.nodeSelection.clear();
    }
  }
  if (graph.operationState == Graph::OperationState::BOX_SELECTING) {
    for (auto idx : nodeSelection) {
      graph.nodeSelection.insert(idx);
    }
  } else if (graph.operationState == Graph::OperationState::BOX_DESELECTING) {
    for (auto idx : nodeSelection) {
      graph.nodeSelection.erase(idx);
    }
  }
  // second pass: do drawing
  for (size_t i = 0; i < graph.nodeorder.size(); ++i) {
    size_t const idx = graph.nodeorder[i];
    auto const& node = graph.nodes[idx];
    auto const center = viewxform * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft = {center.x - NODE_SIZE.x / 2.f * graph.scale,
                            center.y - NODE_SIZE.y / 2.f * graph.scale};
    ImVec2 const bottomright = {center.x + NODE_SIZE.x / 2.f * graph.scale,
                                center.y + NODE_SIZE.y / 2.f * graph.scale};

    ImU32 const color =
        graph.selectedNode == idx
            ? SELECTED_NODE_COLOR
            : hoveredNode == idx ? HOVER_NODE_COLOR : DEFAULT_NODE_COLOR;
    draw_list->AddRectFilled(topleft, bottomright, color, 6.f * graph.scale);

    if (idx == graph.selectedNode)
      draw_list->AddRect(topleft, bottomright, IM_COL32(255, 255, 255, 255),
                         6.f * graph.scale);
  }

  // Draw Links

  // Debug
  // draw_list->AddCircleFilled(viewxform * ImVec2(0, 0), 20*graph.scale,
  //                           IM_COL32(200, 0, 0, 200));
  if (ImGui::IsWindowHovered()) {
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto const srcDelta = glm::xy(glm::inverse(viewxform) *
                                    glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      graph.offset += srcDelta;
    } else if (graph.selectedNode != -1 &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
      auto const srcDelta = glm::xy(glm::inverse(viewxform) *
                                    glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      graph.nodes[graph.selectedNode].pos += srcDelta;
    }
    if (abs(ImGui::GetIO().MouseWheel) > 0.1) {
      graph.scale = glm::clamp(graph.scale + ImGui::GetIO().MouseWheel / 20.f,
                               0.1f, 10.f);
      // cursor as scale center:
      glm::mat3 newView = calcViewMatrix();
      auto const cc = mousePos;
      auto const ccInOldWorld = glm::inverse(viewxform) * cc;
      auto const ccInNewWorld = glm::inverse(newView) * cc;
      graph.offset += glm::vec2(ccInNewWorld.x - ccInOldWorld.x,
                                ccInNewWorld.y - ccInOldWorld.y);
    }
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::End();
}

}  // namespace editorui
