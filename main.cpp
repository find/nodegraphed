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
#include <imgui.h>
#include <math.h> // fmodf

namespace app {

editorui::Graph graph;
editorui::GraphView view;
editorui::GraphView view2;

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

  for (int i = 0; i < 20; ++i) {
    auto node = editorui::Node{};
    node.pos.y = i*80.f;
    node.numInputs = i/3;
    graph.addNode(node);
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
