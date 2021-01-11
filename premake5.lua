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

local projectndf = function(root_dir)
  project "nfd"
    kind "StaticLib"

    -- common files
    files {root_dir.."src/*.h",
           root_dir.."src/include/*.h",
           root_dir.."src/nfd_common.c",
    }

    includedirs {root_dir.."src/include/"}

    warnings "extra"

    -- system build filters
    filter "system:windows"
      language "C++"
      files {root_dir.."src/nfd_win.cpp"}

    filter {"action:gmake or action:xcode4"}
      buildoptions {"-fno-exceptions"}

    filter "system:macosx"
      language "C"
      files {root_dir.."src/nfd_cocoa.m"}

    filter {"system:linux"}
      language "C"
      files {root_dir.."src/nfd_zenity.c"}

    -- visual studio filters
    filter "action:vs*"
      defines { "_CRT_SECURE_NO_WARNINGS" }      
end

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
    linklater({'d3d11.lib', 'dxgi.lib', 'd3dcompiler.lib','ole32','uuid'})
  elseif _OPTIONS['dx12'] then
    files({'deps/imgui/backends/imgui_impl_win32.*', 'deps/imgui/backends/imgui_impl_dx12.*'})
    files{'entry/dx12_main.cpp'}
    linklater({'d3d12.lib', 'dxgi.lib', 'd3dcompiler.lib','ole32','uuid'})
  elseif _OPTIONS['vulkan'] then
    files({'deps/imgui/backends/imgui_impl_glfw.*', 'deps/imgui/backends/imgui_impl_vulkan.*'})
    files{'entry/vulkan_main.cpp'}
    linklater({'glfw3'})
  elseif _OPTIONS['opengl2'] then
    files({'deps/imgui/backends/imgui_impl_glfw.*', 'deps/imgui/backends/imgui_impl_opengl2.*'})
    files{'entry/gl2_main.cpp'}
    if os.target()=='windows' then
      linklater({'glfw3','opengl32','ole32','uuid'})
    else
      linklater({'glfw','GL','dl','pthread'})
    end
  elseif _OPTIONS['opengl3'] then
    files({'deps/imgui/backends/imgui_impl_glfw.*', 'deps/imgui/backends/imgui_impl_opengl3.*'})
    files{'entry/gl3_main.cpp'}
    if os.target()=='windows' then
      linklater({'glfw3','opengl32','ole32','uuid'})
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
  includedirs({
    'deps/spdlog/include',
  })
  files({
    'deps/spdlog/include/spdlog/**',
    'deps/spdlog/src/*.cpp'
  })

projectndf('deps/nativefiledialog/')

project('nodegrapher')
  kind('ConsoleApp')
  includedirs({
    'deps/imgui',
    'deps/spdlog/include',
    'deps/glm',
    'deps/json',
    'deps/nativefiledialog/src/include',
  })
  files({'*.h', '*.cpp'})
  links({'imgui', 'spdlog', 'nfd'})
  links(laterlinks())
  filter('system:windows')
    links({ "ole32", "ws2_32", "advapi32", "version"})
  filter({'action:vs*'})
    buildoptions({'/std:c++17'})
  filter({'toolset:clang or gcc'})
    buildoptions({'-std=c++17'})

  
  

