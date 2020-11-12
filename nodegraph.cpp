#include "nodegraph.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <glm/ext.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/vec_swizzle.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/io.hpp>

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

namespace editorui {

void draw(Graph& graph) {
  ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("NodeEditor",nullptr,ImGuiWindowFlags_MenuBar)) {
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
  ImGui::BeginChild("Canvas", ImVec2(0, 0), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 winPos = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetWindowSize();

  auto calcViewMatrix = [&]() {
    glm::mat3 viewscale = glm::scale(glm::identity<glm::mat3>(),
      glm::vec2(graph.scale, graph.scale));
    glm::mat3 viewtranslate = glm::translate(glm::identity<glm::mat3>(), graph.offset);
    glm::mat3 windowtranslate = glm::translate(glm::identity<glm::mat3>(),
      glm::vec2(winPos.x + canvasSize.x / 2, winPos.y + canvasSize.y / 2));
    return windowtranslate * viewscale * viewtranslate;
  };
  glm::mat3 viewxform = calcViewMatrix();

  // Display grid
  if (graph.showGrid) {
    ImU32 GRID_COLOR = IM_COL32(80, 80, 80, 40);
    float GRID_SZ = 24.0f;
    auto gridOffset = viewxform* glm::vec3(0, 0, 1);
    for (float x = fmodf(gridOffset.x-winPos.x, GRID_SZ*graph.scale);
         x < canvasSize.x; x += GRID_SZ * graph.scale)
      draw_list->AddLine(ImVec2(x, 0) + winPos,
                         ImVec2(x, canvasSize.y) + winPos,
                         GRID_COLOR);
    for (float y = fmodf(gridOffset.y-winPos.y, GRID_SZ*graph.scale);
         y < canvasSize.y; y += GRID_SZ * graph.scale)
      draw_list->AddLine(ImVec2(0, y) + winPos,
                         ImVec2(canvasSize.x, y) + winPos,
                         GRID_COLOR);
  }

  // Draw Nodes
  graph.initOrder();
  for (size_t i = 0; i < graph.nodeorder.size(); ++i) {
  }

  // Draw Links

  // Debug
  //draw_list->AddCircleFilled(viewxform * ImVec2(0, 0), 20*graph.scale,
  //                           IM_COL32(200, 0, 0, 200));

  if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()) {
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto delta = ImGui::GetIO().MouseDelta;  // ImGui::GetMouseDragDelta();
      auto srcDelta =
          glm::xy(glm::inverse(viewxform) * glm::vec3(delta.x, delta.y, 0.0));
      graph.offset += srcDelta;
    }
    if (abs(ImGui::GetIO().MouseWheel) > 0.1) {
      graph.scale = glm::clamp(graph.scale + ImGui::GetIO().MouseWheel / 20.f, 0.1f, 10.f);
      // cursor as scale center:
      glm::mat3 newView = calcViewMatrix();
      auto cc = ImGui::GetMousePos();
      auto ccInOldWorld = glm::inverse(viewxform) * cc;
      auto ccInNewWorld = glm::inverse(newView) * cc;
      graph.offset += glm::vec2(ccInNewWorld.x - ccInOldWorld.x, ccInNewWorld.y - ccInOldWorld.y);
    }
  }

  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::End();
}

}  // namespace editorui
