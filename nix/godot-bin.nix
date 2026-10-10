{
  stdenv,
  fetchurl,
  unzip,
}:
let
  version = "4.7.2";
in
stdenv.mkDerivation {
  pname = "godot-bin";
  inherit version;

  src = fetchurl {
    url = "https://github.com/godotengine/godot-builds/releases/download/${version}-stable/Godot_v${version}-stable_macos.universal.zip";
    hash = "sha256-xYok4x1yC+nWL2DLVifE5pX7cvIbDP4bycyqmjs7pj4=";
  };

  nativeBuildInputs = [ unzip ];
  unpackPhase = "unzip $src";
  installPhase = ''
    mkdir -p $out/Applications $out/bin
    cp -r Godot.app $out/Applications/
    ln -s "$out/Applications/Godot.app/Contents/MacOS/Godot" $out/bin/godot
  '';

  meta.platforms = [ "aarch64-darwin" "x86_64-darwin" ];
}
