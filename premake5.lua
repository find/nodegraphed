newoption({
  trigger='dx11',
  description='using dx11 implement'
})

newoption({
  trigger='dx12',
  description='using dx12 implement'
})

newoption({
  trigger='vulkan',
  description='using vulkan implement'
})

newoption({
  trigger='opengl2',
  description='using opengl2 implement'
})

newoption({
  trigger='opengl3',
  description='using opengl3 implement'
})

local _laterlinks = {}
local linklater = function(libs)
  libs = libs or {}
  for _,v in ipairs(libs) do
    _laterlinks[v] = true
  end
end
local laterlinks = function()
  local result = {}
  for k,v in pairs(_laterlinks) do
    table.insert(result, k)
  end
  return result
end

workspace('imgui-nodegraph')
  language('C++')
  configurations({'Release','Debug'})
  platforms({'native','x64','x86','arm'})
  location('.build/'.._ACTION)
  includedirs({
    'deps/imgui',
    'deps/spdlog/include',
    'deps/glm',
    'deps/json',
  })
  startproject('nodegrapher')
  defines({
    'SPDLOG_COMPILED_LIB'
  })
  filter('configurations:Debug')
    defines('_DEBUG')
    symbols('on')
  filter('system:windows')
    staticruntime('on')

project('imgui')
  kind('StaticLib')
  includedirs({'deps/imgui', 'deps/imgui/backends'})
  files({
    'deps/imgui/*.h', 'deps/imgui/*.cpp'
  })
  if _OPTIONS['dx11'] then
    files({'deps/imgui/backends/imgui_impl_win32.*', 'deps/imgui/backends/imgui_impl_dx11.*'})
    files{'entry/dx11_main.cpp'}
    linklater({'d3d11.lib', 'dxgi.lib', 'd3dcompiler.lib'})
  elseif _OPTIONS['dx12'] then
    files({'deps/imgui/backends/imgui_impl_win32.*', 'deps/imgui/backends/imgui_impl_dx12.*'})
    files{'entry/dx12_main.cpp'}
    linklater({'d3d12.lib', 'dxgi.lib', 'd3dcompiler.lib'})
  elseif _OPTIONS['vulkan'] then
    files({'deps/imgui/backends/imgui_impl_glfw.*', 'deps/imgui/backends/imgui_impl_vulkan.*'})
    files{'entry/vulkan_main.cpp'}
    linklater({'glfw3'})
  elseif _OPTIONS['opengl2'] then
    files({'deps/imgui/backends/imgui_impl_glfw.*', 'deps/imgui/backends/imgui_impl_opengl2.*'})
    files{'entry/gl2_main.cpp'}
    if os.target()=='windows' then
      linklater({'glfw3','opengl32'})
    else
      linklater({'glfw','GL','dl','pthread'})
    end
  elseif _OPTIONS['opengl3'] then
    files({'deps/imgui/backends/imgui_impl_glfw.*', 'deps/imgui/backends/imgui_impl_opengl3.*'})
    files{'entry/gl3_main.cpp'}
    if os.target()=='windows' then
      linklater({'glfw3','opengl32'})
    else
      linklater({'glfw','GL','dl','pthread'})
    end
  else
    files({'deps/imgui/backends/imgui_impl_win32.*', 'deps/imgui/backends/imgui_impl_dx11.*'})
    files{'entry/dx11_main.cpp'}
    linklater({'d3d11.lib', 'dxgi.lib', 'd3dcompiler.lib'})
  end

project('spdlog')
  kind('StaticLib')
  files({
    'deps/spdlog/include/spdlog/**',
    'deps/spdlog/src/*.cpp'
  })

project('nodegrapher')
  kind('ConsoleApp')
  files({'*.h', '*.cpp'})
  links({'imgui', 'spdlog'})
  links(laterlinks())
  filter('system:windows')
    links({ "ole32", "ws2_32", "advapi32", "version"})

  
  

