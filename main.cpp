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
    graph.nodes.emplace_back();
    graph.nodes.back().pos.y = i*80.f;
    graph.nodes.back().numInputs = i/3;
  }
}

bool show_demo_window = true;
bool show_another_window = true;
bool show_ng_window = true;
float clear_color[3] = { 0.1f,0.1f,0.1f };

void update() {
  ImGui::DockSpaceOverViewport();
  editorui::updateAndDraw(graph);
}

void quit() {}

} // namespace app
