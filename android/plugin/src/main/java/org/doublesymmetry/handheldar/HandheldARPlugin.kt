package org.doublesymmetry.handheldar

import org.godotengine.godot.Godot
import org.godotengine.godot.plugin.GodotPlugin

class HandheldARPlugin(godot: Godot) : GodotPlugin(godot) {
    companion object {
        init {
            System.loadLibrary("handheld_ar")
        }
    }

    override fun getPluginName() = "HandheldAR"

    override fun getPluginGDExtensionLibrariesPaths() =
        setOf("res://addons/handheld_ar/handheld_ar.gdextension")
}
