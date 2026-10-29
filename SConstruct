import subprocess
import sys

ARGUMENTS.setdefault("platform", "linux" if sys.platform == "linux" else "macos")
ARGUMENTS.setdefault("target", "template_debug")
ARGUMENTS.setdefault("arch", "x86_64" if ARGUMENTS["platform"] == "linux" else "arm64")
ARGUMENTS.setdefault("api_version", "4.7")
if ARGUMENTS["platform"] == "macos":
    ARGUMENTS.setdefault("macos_deployment_target", "13.0")
if ARGUMENTS["platform"] == "android":
    ARGUMENTS.setdefault("ndk_version", "29.0.14206865")
    ARGUMENTS.setdefault("android_api_level", "29")
if ARGUMENTS["platform"] == "ios":
    ARGUMENTS.setdefault("ios_min_version", "16.0")
    developer = subprocess.check_output(["xcode-select", "-p"], text=True).strip()
    ARGUMENTS.setdefault("IOS_TOOLCHAIN_PATH", developer + "/Toolchains/XcodeDefault.xctoolchain")

env = Environment(tools=["default"], PLATFORM="")
if ARGUMENTS["platform"] == "linux":
    # GNU2 TLS requires a newer glibc than the desktop addon otherwise needs.
    env.Append(CCFLAGS=["-mtls-dialect=gnu"], LINKFLAGS=["-mtls-dialect=gnu"])
env = SConscript("godot-cpp/SConstruct", {"env": env, "api_version": "4.7"})
env.Append(CPPPATH=["src"])
sources = Glob("src/*.cpp") + Glob("src/core/*.cpp")
if env["target"] in ("editor", "template_debug"):
    sources += env.GodotCPPDocData("build/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
platform = env["platform"]
if platform == "android":
    env.Append(LINKFLAGS=["-static-libstdc++", "-Wl,-z,max-page-size=16384"],
               CPPPATH=["thirdparty/arcore/include"], LIBS=["dl", "EGL", "android", "log"])
if platform not in ("macos", "linux", "ios", "android"):
    Exit("Supported platforms: macos, linux, ios, android")
if platform in ("ios", "android"):
    sources += Glob("src/platform/" + platform + "/*.cpp")
if platform == "ios":
    sources += Glob("src/platform/ios/*.mm")
    objc_env = env.Clone()
    objc_env.Append(CXXFLAGS=["-fobjc-arc", "-fmodules", "-fcxx-modules",
                             "-fmodules-cache-path=" + Dir("build/clang-module-cache").abspath])
output = "harness/addons/pocketar/bin/{}/{}/{}".format(platform, env["target"], env["arch"])
objects = [(objc_env if str(s).endswith(".mm") else env).SharedObject(
    "build/" + env["suffix"] + "/" + str(s) + ".os", s) for s in sources]
if platform == "ios":
    # StaticLibrary does not absorb LIBS; the XCFramework must also contain godot-cpp.
    archive = env.StaticLibrary(output + "/libpocketar_objects", objects)
    cpp_archive = next(lib for lib in env["LIBS"] if "godot-cpp" in str(lib))
    library = env.Command(output + "/libpocketar.a", [archive, cpp_archive],
                          "xcrun libtool -static -o $TARGET $SOURCES")
else:
    library = env.SharedLibrary(output + "/libpocketar", objects)
Default(library)
