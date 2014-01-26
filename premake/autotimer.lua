
local base_dir = (solution().basedir .. "/auto_timer/")
local sizzlib_dir = (solution().basedir .. "/sizzlinglib/")

project "AutoTimer"
    kind "SharedLib"
    language "C++"
    location (_ACTION .. "/" .. project().name)
    
    defines
    {
    }
    includedirs
    {
        sizzlib_dir .. "include",
    }
    files
    {
        base_dir .. "**.h",
        base_dir .. "**.cpp",
    }
    libdirs
    {
    }
    links
    {
        "SizzlingLib"
    }
    
    dofile "hl2sdk-ob-valve.lua"
