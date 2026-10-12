{ pkgs, ... }:
let
  # Godot master plus PocketAR's engine patches (doublesymmetry/godot, PATCHES.md).
  godot = pkgs.callPackage ./nix/godot-pocketar.nix { };
  # Stock Godot 4.7.2, the oldest engine the library supports.
  godot-stock = pkgs.callPackage ./nix/godot-bin.nix { };
in
{
  # Xcode's own toolchain builds the Apple libraries; nixpkgs' Apple SDK or wrapped clang breaks that.
  apple.sdk = null;
  stdenv = pkgs.stdenvNoCC;

  packages = [
    godot
    pkgs.scons
    pkgs.python3
    pkgs.jq
    pkgs.ripgrep
  ];

  # Godot 4.7 builds Android with JDK 17; the android module enables Java with a newer default.
  languages.java.jdk.package = pkgs.jdk17;

  scripts.build.exec = ''exec "$DEVENV_ROOT/tools/build.sh" "$@"'';
  scripts.export.exec = ''exec "$DEVENV_ROOT/tools/export.sh" "$@"'';
  scripts.harness.exec = ''exec "$DEVENV_ROOT/tools/harness.sh" "$@"'';
  # Runs a command against stock Godot 4.7.2, for example `stock harness desktop`.
  scripts.stock.exec = ''PATH="${godot-stock}/bin:$PATH" exec "$@"'';

  android = {
    enable = true;
    platforms.version = [ "36" ];
    # Godot 4.8's Gradle template builds with 35.0.0; 36.1.0 serves the 4.7.2 lane.
    buildTools.version = [ "35.0.0" "36.1.0" ];
    ndk.enable = true;
    ndk.version = [ "29.0.14206865" ];
    emulator.enable = false;
    systemImages.enable = false;
    googleAPIs.enable = false;
    sources.enable = false;
    android-studio.enable = false;
  };
}
