plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

val arcoreVersion = "1.56.0"
val arcoreNative by configurations.creating {
    isCanBeConsumed = false
    isTransitive = false
}

android {
    namespace = "org.doublesymmetry.handheldar"
    compileSdk = 36
    buildToolsVersion = "36.1.0"
    ndkVersion = "29.0.14206865"
    defaultConfig {
        minSdk = 29
        ndk { abiFilters += "arm64-v8a" }
        consumerProguardFiles("consumer-rules.pro")
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    sourceSets {
        getByName("main").assets.srcDir(layout.buildDirectory.dir("generated/assets"))
        getByName("debug").jniLibs.srcDir(layout.buildDirectory.dir("generated/jni/debug"))
        getByName("release").jniLibs.srcDir(layout.buildDirectory.dir("generated/jni/release"))
    }
}

dependencies {
    compileOnly("org.godotengine:godot:4.7.2.stable")
    implementation("com.google.ar:core:$arcoreVersion")
    add(arcoreNative.name, "com.google.ar:core:$arcoreVersion@aar")
}

val extractArcoreNative by tasks.registering(Sync::class) {
    from({ zipTree(arcoreNative.singleFile) })
    include("jni/arm64-v8a/libarcore_sdk_c.so", "**/arcore_c_api.h")
    into(layout.buildDirectory.dir("arcore-native/$arcoreVersion"))
    doLast {
        check(destinationDir.resolve("jni/arm64-v8a/libarcore_sdk_c.so").isFile) {
            "ARCore $arcoreVersion does not contain the arm64 C API shim"
        }
    }
}

val copyExtension by tasks.registering(Copy::class) {
    from("../../demo/addons/handheld_ar/handheld_ar.gdextension")
    into(layout.buildDirectory.dir("generated/assets/addons/handheld_ar"))
}

tasks.named("preBuild") { dependsOn(copyExtension, extractArcoreNative) }
for ((variant, target) in listOf("debug" to "template_debug", "release" to "template_release")) {
    val capitalized = variant.replaceFirstChar { it.uppercase() }
    val library = file("../../demo/addons/handheld_ar/bin/android/$target/arm64/libhandheld_ar.so")
    val copyNative = tasks.register<Copy>("copy${capitalized}Native") {
        from(library)
        into(layout.buildDirectory.dir("generated/jni/$variant/arm64-v8a"))
        doFirst { check(library.isFile) { "Build the Android $target extension with tools/build.sh first" } }
    }
    tasks.matching { it.name == "pre${capitalized}Build" }.configureEach { dependsOn(copyNative) }
    val copyAar = tasks.register("copy${capitalized}Aar") {
        val source = layout.buildDirectory.file("outputs/aar/plugin-$variant.aar")
        val destination = file("../../demo/addons/handheld_ar/bin/android/handheld_ar-$variant.aar")
        inputs.file(source)
        outputs.file(destination)
        doLast {
            destination.parentFile.mkdirs()
            source.get().asFile.copyTo(destination, overwrite = true)
        }
    }
    tasks.matching { it.name == "assemble$capitalized" }.configureEach { finalizedBy(copyAar) }
}
