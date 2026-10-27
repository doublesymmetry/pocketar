#!/usr/bin/env bash
set -euo pipefail

# Self-contained settings require the real executable or app bundle, not a symlink.
godot_home="${DEVENV_STATE:-${XDG_STATE_HOME:-$HOME/.local/state}}/@stateName@"
executable="$godot_home/godot"
if [ -d @out@/libexec/Godot.app ]; then
	executable="$godot_home/Godot.app/Contents/MacOS/Godot"
fi
if [ "$(cat "$godot_home/.source" 2>/dev/null)" != "@out@" ]; then
	mkdir -p "$godot_home/editor_data/export_templates"
	if [ -d @out@/libexec/Godot.app ]; then
		if [ -d "$godot_home/Godot.app" ]; then
			chmod -R u+w "$godot_home/Godot.app"
			rm -rf "$godot_home/Godot.app"
		fi
		cp -R @out@/libexec/Godot.app "$godot_home/Godot.app"
		chmod -R u+w "$godot_home/Godot.app"
	else
		install -m 755 @out@/libexec/godot "$godot_home/godot.tmp"
		mv "$godot_home/godot.tmp" "$godot_home/godot"
	fi
	ln -sfn @out@/share/godot/templates "$godot_home/editor_data/export_templates/$(cat @out@/share/godot/templates/version.txt)"
	touch "$godot_home/._sc_"
	echo "@out@" > "$godot_home/.source"
fi

# Godot saves the SDK and JDK paths it first sees, so give it paths that survive devenv
# updates. The SDK root must also be writable for Gradle and have the
# `cmdline-tools/latest` that Godot 4.8 requires; nixpkgs only installs a numbered one.
sdk="$godot_home/android-sdk"
if [ -d "${ANDROID_HOME:-}" ] && [ "$ANDROID_HOME" != "$sdk" ]; then
	if [ "$(cat "$sdk/.source" 2>/dev/null)" != "$ANDROID_HOME" ]; then
		rm -rf "$sdk"
		mkdir -p "$sdk/cmdline-tools"
		for entry in "$ANDROID_HOME"/*; do
			[ "$(basename "$entry")" = cmdline-tools ] || ln -s "$entry" "$sdk/"
		done
		for tools in "$ANDROID_HOME"/cmdline-tools/*; do
			ln -s "$tools" "$sdk/cmdline-tools/"
			ln -sfn "$tools" "$sdk/cmdline-tools/latest"
		done
		echo "$ANDROID_HOME" > "$sdk/.source"
	fi
	export ANDROID_HOME="$sdk"
	unset ANDROID_SDK_ROOT
fi
if [ -d "${JAVA_HOME:-}" ] && [ "$JAVA_HOME" != "$godot_home/jdk" ]; then
	ln -sfn "$JAVA_HOME" "$godot_home/jdk"
	export JAVA_HOME="$godot_home/jdk"
fi

# Godot generates a debug key per editor data directory, which here is per checkout.
# The standard per-user Android key lets any worktree reinstall over another's build.
if [ -z "${GODOT_ANDROID_KEYSTORE_DEBUG_PATH:-}" ]; then
	keystore="$HOME/.android/debug.keystore"
	if [ ! -f "$keystore" ] && [ -x "${JAVA_HOME:-}/bin/keytool" ]; then
		mkdir -p "$(dirname "$keystore")"
		"$JAVA_HOME/bin/keytool" -genkeypair -keystore "$keystore" -storepass android -alias androiddebugkey \
			-keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
	fi
	export GODOT_ANDROID_KEYSTORE_DEBUG_PATH="$keystore" GODOT_ANDROID_KEYSTORE_DEBUG_USER=androiddebugkey GODOT_ANDROID_KEYSTORE_DEBUG_PASSWORD=android
fi

exec "$executable" "$@"
