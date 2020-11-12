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

  ImDrawList* drawList = ImGui::GetWindowDrawList();
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
      drawList->AddLine(ImVec2(x, 0) + winPos,
                         ImVec2(x, canvasSize.y) + winPos, GRID_COLOR);
    for (float y = fmodf(gridOffset.y - winPos.y, GRID_SZ * graph.scale);
         y < canvasSize.y; y += GRID_SZ * graph.scale)
      drawList->AddLine(ImVec2(0, y) + winPos,
                         ImVec2(canvasSize.x, y) + winPos, GRID_COLOR);
  }

  // Draw Nodes
  ImU32 const DEFAULT_NODE_COLOR = IM_COL32(160, 160, 160, 128);
  ImU32 const HOVER_NODE_COLOR = IM_COL32(200, 200, 200, 128);
  ImU32 const SELECTED_NODE_COLOR = IM_COL32(233, 233, 233, 180);
  ImU32 const SELECTING_NODE_COLOR = IM_COL32(233, 233, 233, 233);
  ImU32 const SELECTION_BOX_COLOR = IM_COL32(60, 110, 60, 128);
  ImU32 const DESELECTION_BOX_COLOR = IM_COL32(140, 60, 60, 128);
  ImVec2 const NODE_SIZE = {64, 24};

  // first pass: update states
  graph.initOrder();
  size_t hoveredNode = -1;
  size_t clickedNode = -1;
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
    drawList->AddRectFilled(ImVec2(sbbminx, sbbminy), ImVec2(sbbmaxx, sbbmaxy),
                             SELECTION_BOX_COLOR);
  } else if (graph.operationState == Graph::OperationState::BOX_DESELECTING) {
    drawList->AddRectFilled(ImVec2(sbbminx, sbbminy), ImVec2(sbbmaxx, sbbmaxy),
                             DESELECTION_BOX_COLOR);
  }
  for (size_t i = 0; i < graph.nodeOrder.size(); ++i) {
    size_t const idx = graph.nodeOrder[i];
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
      if (bottomright.x >= sbbminx && topleft.x <= sbbmaxx &&
          bottomright.y >= sbbminy && topleft.y <= sbbmaxy) {
        nodeSelection.insert(idx);
      }
    }
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    clickedNode = hoveredNode;
    graph.activeNode = clickedNode;
    if (clickedNode != -1 &&
      (glm::distance(graph.selectionBoxStart, graph.selectionBoxEnd) < 4 ||
       graph.nodeSelection.empty())) {
      graph.nodeSelection.insert(clickedNode);
      graph.shiftToEnd(clickedNode);
    }

    auto const modkey = ImGui::GetIO().KeyMods;
    if (graph.operationState == Graph::OperationState::VIEWING) {
      graph.selectionBoxStart = glm::vec2(mousePos.x, mousePos.y);
      if (modkey & (ImGuiKeyModFlags_Shift | (~ImGuiKeyModFlags_Ctrl))) {
        graph.operationState = Graph::OperationState::BOX_SELECTING;
      } else if (modkey & (ImGuiKeyModFlags_Ctrl | (~ImGuiKeyModFlags_Shift))) {
        graph.operationState = Graph::OperationState::BOX_DESELECTING;
      }

      if (graph.operationState == Graph::OperationState::BOX_SELECTING) {
        if (clickedNode != -1)
          graph.nodeSelection.insert(clickedNode);
      } else if (graph.operationState == Graph::OperationState::BOX_DESELECTING) {
        if (clickedNode != -1)
          graph.nodeSelection.erase(clickedNode);
      }
    }
  }
  if (graph.operationState == Graph::OperationState::VIEWING &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10)) {
    if (hoveredNode == -1 &&
        clickedNode == -1 &&
        graph.activeNode == -1) {
      graph.operationState = Graph::OperationState::BOX_SELECTING;
      graph.nodeSelection.clear();
    } else if (graph.activeNode!=-1 && graph.nodeSelection.find(graph.activeNode) != graph.nodeSelection.end()) {
      auto const srcDelta = glm::xy(glm::inverse(viewxform) *
        glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      if (glm::length(srcDelta) > 0) {
        for (size_t idx : graph.nodeSelection)
          graph.nodes[idx].pos += srcDelta;
      }
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (graph.operationState == Graph::OperationState::BOX_SELECTING ||
        graph.operationState == Graph::OperationState::BOX_DESELECTING) {
      if (graph.operationState == Graph::OperationState::BOX_SELECTING) {
        for (auto idx : nodeSelection) {
          graph.nodeSelection.insert(idx);
        }
      } else if (graph.operationState == Graph::OperationState::BOX_DESELECTING) {
        for (auto idx : nodeSelection) {
          graph.nodeSelection.erase(idx);
        }
      }
      graph.operationState = Graph::OperationState::VIEWING;
    } else if (graph.operationState == Graph::OperationState::VIEWING) {
      if (hoveredNode == -1 && clickedNode == -1 && graph.activeNode == -1) {
        graph.nodeSelection.clear();
      }
      else if (graph.activeNode != -1 && glm::distance(graph.selectionBoxStart, graph.selectionBoxEnd) < 4) {
        graph.nodeSelection.clear();
        graph.nodeSelection.insert(graph.activeNode);
      }
    }
  }

  // second pass: do drawing
  for (size_t i = 0; i < graph.nodeOrder.size(); ++i) {
    size_t const idx = graph.nodeOrder[i];
    auto const& node = graph.nodes[idx];
    auto const center = viewxform * glm::vec3(node.pos, 1.0);
    ImVec2 const topleft = {center.x - NODE_SIZE.x / 2.f * graph.scale,
                            center.y - NODE_SIZE.y / 2.f * graph.scale};
    ImVec2 const bottomright = {center.x + NODE_SIZE.x / 2.f * graph.scale,
                                center.y + NODE_SIZE.y / 2.f * graph.scale};

    ImU32 const color =
            nodeSelection.find(idx) != nodeSelection.end()
            ? SELECTING_NODE_COLOR
            : graph.nodeSelection.find(idx) != graph.nodeSelection.end()
              ? SELECTED_NODE_COLOR
              : hoveredNode == idx ? HOVER_NODE_COLOR : DEFAULT_NODE_COLOR;
    drawList->AddRectFilled(topleft, bottomright, color, 6.f * graph.scale);

    if (graph.nodeSelection.find(idx) != graph.nodeSelection.end())
      drawList->AddRect(topleft, bottomright, IM_COL32(255, 255, 255, 255),
                        6.f * graph.scale);
  }

  // Draw Links

  // Handles Scaling
  if (ImGui::IsWindowHovered()) {
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto const srcDelta = glm::xy(glm::inverse(viewxform) *
                                    glm::vec3(mouseDelta.x, mouseDelta.y, 0.0));
      graph.offset += srcDelta;
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
  // Handle Keydown
  if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) {
    graph.removeNodes(graph.nodeSelection);
    graph.nodeSelection.clear();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(); // frame padding
  ImGui::PopStyleVar(); // window padding
  ImGui::PopStyleColor(); // child bg
  ImGui::End();
}

}  // namespace editorui
