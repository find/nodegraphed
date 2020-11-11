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
static inline ImVec2 operator*(const glm::mat3 const& m, const ImVec2& v) {
  auto r = m * glm::vec3(v.x, v.y, 1.0f);
  return ImVec2(r.x, r.y);
}

namespace editorui {

void draw(Graph& graph) {
  if (!ImGui::Begin("NodeEditor")) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("Show")) {
      ImGui::MenuItem("Grid", nullptr, &graph.showGrid);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  glm::mat3 viewscale = glm::scale(glm::identity<glm::mat3>(),
                                   glm::vec2(graph.scale, graph.scale));
  glm::mat3 viewtranslate = glm::translate(glm::identity<glm::mat3>(), graph.offset);

  glm::mat3 viewxform = viewscale * viewtranslate;

  spdlog::debug("viewxform = {}", glm::to_string(viewxform));

  const ImVec2 offset = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 25, 255));
  ImGui::BeginChild("Canvas", ImVec2(0, 0), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);


  // Display grid
  if (graph.showGrid) {
    ImU32 GRID_COLOR = IM_COL32(80, 80, 80, 40);
    float GRID_SZ = 24.0f;
    ImVec2 win_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImGui::GetWindowSize();
    for (float x = fmodf((viewxform * glm::vec3(graph.offset, 1.0)).x, GRID_SZ*graph.scale);
         x < canvas_sz.x; x += GRID_SZ * graph.scale)
      draw_list->AddLine(ImVec2(x, 0.0f) + win_pos,
                         ImVec2(x, canvas_sz.y) + win_pos, GRID_COLOR);
    for (float y = fmodf((viewxform * glm::vec3(graph.offset, 1.0)).y, GRID_SZ*graph.scale);
         y < canvas_sz.y; y += GRID_SZ * graph.scale)
      draw_list->AddLine(ImVec2(0.0f, y) + win_pos,
                         ImVec2(canvas_sz.x, y) + win_pos, GRID_COLOR);
  }

  graph.initOrder();
  for (size_t i = 0; i < graph.nodeorder.size(); ++i) {
  }

  draw_list->AddCircleFilled(viewxform * ImVec2(100, 60), 10*graph.scale,
                             IM_COL32(200, 0, 0, 100));

  if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()) {
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
      auto delta = ImGui::GetIO().MouseDelta;  // ImGui::GetMouseDragDelta();
      spdlog::debug("raw delta = ({},{})", delta.x, delta.y);
      auto srcdelta =
          glm::xy(glm::inverse(viewxform) * glm::vec3(delta.x, delta.y, 0.0));
      graph.offset += srcdelta;
      spdlog::debug("source delta = ({},{})", srcdelta.x, srcdelta.y);
    }
    if (abs(ImGui::GetIO().MouseWheel) > 0.1)
      graph.scale = glm::clamp(graph.scale+ImGui::GetIO().MouseWheel/20.f, 0.1f, 10.f);
  }

  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::End();
}

}  // namespace editorui
