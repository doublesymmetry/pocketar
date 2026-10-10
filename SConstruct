import subprocess

ARGUMENTS.setdefault("platform", "macos")
ARGUMENTS.setdefault("target", "template_debug")
ARGUMENTS.setdefault("arch", "arm64")
ARGUMENTS.setdefault("api_version", "4.7")
if ARGUMENTS["platform"] == "android":
    ARGUMENTS.setdefault("ndk_version", "29.0.14206865")
    ARGUMENTS.setdefault("android_api_level", "29")
if ARGUMENTS["platform"] == "ios":
    ARGUMENTS.setdefault("ios_min_version", "16.0")
    developer = subprocess.check_output(["xcode-select", "-p"], text=True).strip()
    ARGUMENTS.setdefault("IOS_TOOLCHAIN_PATH", developer + "/Toolchains/XcodeDefault.xctoolchain")

env = SConscript("godot-cpp/SConstruct", {"api_version": "4.7"})
env.Append(CPPPATH=["src"])
sources = Glob("src/*.cpp") + Glob("src/core/*.cpp")
platform = env["platform"]
if platform == "android":
    env.Append(LINKFLAGS=["-static-libstdc++", "-Wl,-z,max-page-size=16384"])
if platform not in ("macos", "ios", "android"):
    Exit("Supported platforms: macos, ios, android")
if platform in ("ios", "android"):
    sources += Glob("src/platform/" + platform + "/*.cpp")
output = "demo/addons/handheld_ar/bin/{}/{}/{}".format(platform, env["target"], env["arch"])
objects = [env.SharedObject("build/" + env["suffix"] + "/" + str(s) + ".os", s) for s in sources]
if platform == "ios":
    # StaticLibrary does not absorb LIBS; the XCFramework must also contain godot-cpp.
    archive = env.StaticLibrary(output + "/libhandheld_ar_objects", objects)
    cpp_archive = next(lib for lib in env["LIBS"] if "godot-cpp" in str(lib))
    library = env.Command(output + "/libhandheld_ar.a", [archive, cpp_archive],
                          "xcrun libtool -static -o $TARGET $SOURCES")
else:
    library = env.SharedLibrary(output + "/libhandheld_ar", objects)
Default(library)
