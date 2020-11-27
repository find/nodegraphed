// dear imgui - standalone example application for DirectX 11
// If you are new to dear imgui, see examples/README.txt and documentation at the top of imgui.cpp.

#include "main.h"
#include "nodegraph.h"
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <spdlog/sinks/wincolor_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#else
#include <spdlog/sinks/ansicolor_sink.h>
#endif

#include <nlohmann/json.hpp>

#include <imgui.h>
#include <math.h> // fmodf

namespace app {

class MyTestHook : public editorui::NodeGraphHook
{
  static std::map<std::string, int> typeNumericSuffix;

  struct RealNode
  {
    std::string type;
    std::string name;
  };

  std::vector<std::string> nodeClassList() override {
    std::vector<std::string> clsList = {
      "hello",
      "world",
      "foo",
      "bar",
      "blahblah",
      "read_csv",
      "write_csv",
      "output",
      "join",
      "split",
      "filter"
    };
    return clsList;
  }

  void* createNode( editorui::Graph* parent,
                    std::string const& type,
                    std::string const& name,
                    std::string& outName ) override
  {
    outName = name + '_' + std::to_string(++typeNumericSuffix[type]);
    return new RealNode{ type, outName };
  }

  void beforeDeleteNode(editorui::Node* node) override
  {
    delete static_cast<RealNode*>(node->payload());
  }

  int getNodeOutputCount(editorui::Node const* node) override {
    if (static_cast<RealNode const*>(node->payload())->type == "split")
      return 2;
    else
      return 1;
  }

  bool onSave(editorui::Graph const* graph, nlohmann::json& json, std::string const& path) override {
    auto& section = json["runtimegraph"];
    auto& mapping = section["mapping"];
    for (auto node : graph->nodes()) {
      if (node.second.payload()) {
        mapping[std::to_string(node.first)] = {
          {"type", static_cast<RealNode const*>(node.second.payload())->type},
          {"name", static_cast<RealNode const*>(node.second.payload())->name} };
      } else {
        spdlog::warn("node {}({}) has no payload??", node.first, node.second.name());
      }
    }
    return true;
  }

  bool onLoad(editorui::Graph* graph, nlohmann::json const& json, std::string const& path) override {
    auto const& section = json["runtimegraph"];
    auto const& mapping = section["mapping"];
    for (auto& node : graph->nodes()) {
      std::string id = std::to_string(node.first);
      if (mapping.find(id) != mapping.end()) {
        std::string type = mapping[id]["type"];
        std::string name = mapping[id]["name"];
        node.second.setPayload(new RealNode{ type, name });
        node.second.setHook(this);
      } else {
        spdlog::warn("node {}({}) has no mapping?", id, node.second.name());
      }
    }
    return true;
  }
};
std::map<std::string, int> MyTestHook::typeNumericSuffix;

editorui::Graph graph;
editorui::GraphView view;
editorui::GraphView view2;
MyTestHook hook;

void init()
{
#ifdef _WIN32
  spdlog::set_default_logger(std::make_shared<spdlog::logger>("", std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>()));
  spdlog::default_logger()->sinks().emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#else
  spdlog::set_default_logger(std::make_shared<spdlog::logger>("", std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>()));
#endif
  spdlog::set_level(spdlog::level::debug);
  ImGui::StyleColorsDark();
  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_FrameBg] = ImVec4(0.28f, 0.28f, 0.28f, 0.54f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.68f, 0.67f, 0.64f, 0.40f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.67f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.67f, 0.67f, 0.67f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.89f, 0.89f, 0.89f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.47f, 0.46f, 0.45f, 0.40f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.33f, 0.31f, 0.28f, 0.31f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.26f, 0.26f, 0.80f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.57f, 0.59f, 0.61f, 0.78f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.58f, 0.58f, 0.58f, 1.00f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.48f, 0.48f, 0.48f, 0.25f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.60f, 0.60f, 0.60f, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.82f, 0.82f, 0.82f, 0.95f);
  colors[ImGuiCol_Tab] = ImVec4(0.23f, 0.23f, 0.23f, 0.86f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.28f, 0.80f);
  colors[ImGuiCol_TabActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.05f, 0.97f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
  colors[ImGuiCol_DockingPreview] = ImVec4(0.61f, 0.61f, 0.61f, 0.70f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);

  graph.setHook(&hook);
  for (int i = 0; i < 20; ++i) {
    graph.addNode("node", glm::vec2(0, i*80.f));
  }
  view.graph = &graph;
  view.onGraphChanged();

  view2.graph = &graph;
  view2.onGraphChanged();

  graph.addViewer(&view);
  graph.addViewer(&view2);
}

bool show_demo_window = true;
bool show_another_window = true;
bool show_ng_window = true;
float clear_color[3] = { 0.1f,0.1f,0.1f };

void update() {
  ImGui::DockSpaceOverViewport();
  editorui::updateAndDraw(view, "Node Graph");
  editorui::updateAndDraw(view2, "Node Graph2");
}

void quit() {}

} // namespace app
