project "Irrlicht"
	kind "StaticLib"
	includedirs "include"
	links "imm32"
	dofile("defines.lua")
	exceptionhandling "Off"
	rtti "Off"
	-- Dichiarato qui e non nel premake5.lua di radice: quello governa anche la
	-- build Linux, che e' verde ed e' l'unico binario che stiamo distribuendo,
	-- e gframe/ocgcore lo dichiarano gia' ognuno per se'. Senza, con MSVC
	-- questo progetto eredita il default C++14 e COSOperatorWindows.cpp non
	-- compila (C2429, init-statement dentro un if). Su Linux non si vedeva:
	-- g++ accetta quella costruzione anche senza il flag esplicito.
	cppdialect "C++17"
	files { "**.cpp", "**.c", "**.cxx", "**.hpp", "**.h" }
	warnings "Default"

	filter "options:no-direct3d"
		defines "NO_IRR_COMPILE_WITH_DIRECT3D_9_"

	if not _OPTIONS["no-direct3d"] then
		filter "options:not no-direct3d"
			defines "IRR_COMPILE_WITH_DX9_DEV_PACK"
			local dxSdkDir = os.getenv("DXSDK_DIR")
			if not dxSdkDir then
				error('DirectX SDK not found (DXSDK_DIR envvar not set). See build prerequisites: https://github.com/edo9300/edopro/wiki')
			end
			includedirs(dxSdkDir.."/Include")
	end

	if not (os.isdir('include') and os.isdir('src')) then
		error('Irrlicht is not installed in the expected location. See dependency installation instructions: https://github.com/edo9300/edopro/wiki')
	end
