workspace "GmodLoadingOverlay"
    configurations { "Debug", "Release" }
    language "C++"
    cppdialect "C++17"

    location ("projects/" .. os.host() .. "/" .. _ACTION)

    platforms { "x86_64" }
    
    defines { 
        "_CRT_SECURE_NO_WARNINGS", 
    }

    filter {"configurations:Debug*"}
        defines { "DEBUG", "_DEBUG" }
        symbols "On"
        runtime "Debug"

    filter {"configurations:Release*"}
        defines { "NDEBUG" }
        runtime "Release"
        optimize "Speed"

    project "GmodLoadingOverlay"
        kind "SharedLib"

        targetname "gmsv_gmodoverlay_win64"

        includedirs {
            "src",
            "external/glad/include",
            "external/gmod-module-base-development/include",
            "external/gmod-html-master/html",
            "external/glfw-3.4.bin.WIN64/include",
        }

        files {
            "src/**.h", 
            "src/**.hpp", 
            "src/**.cpp",
            "src/**.c",
            "external/glad/src/**.c"
        }

        links { "glfw3" }

        filter { "architecture:x86_64" }
            targetdir "out/x86_64/%{cfg.buildcfg}"
            includedirs { "external/glfw-3.4.bin.WIN64/include" }

            libdirs { 
                "external/glfw-3.4.bin.WIN64/lib-vc2022",
            }