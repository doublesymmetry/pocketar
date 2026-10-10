{ pkgs, ... }:
let
  godot = pkgs.callPackage ./nix/godot-bin.nix { };
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
  ];

  # Godot 4.7 builds Android with JDK 17; the android module enables Java with a newer default.
  languages.java.jdk.package = pkgs.jdk17;

  android = {
    enable = true;
    platforms.version = [ "36" ];
    buildTools.version = [ "36.1.0" ];
    ndk.enable = true;
    ndk.version = [ "29.0.14206865" ];
    emulator.enable = false;
    systemImages.enable = false;
    googleAPIs.enable = false;
    sources.enable = false;
    android-studio.enable = false;
  };
}
