{
  stdenvNoCC,
  fetchurl,
  unzip,
}:
let
  version = "4.7.2";
  editor = if stdenvNoCC.isDarwin then {
    name = "macos.universal";
    hash = "sha256-xYok4x1yC+nWL2DLVifE5pX7cvIbDP4bycyqmjs7pj4=";
  } else {
    name = "linux.x86_64";
    hash = "sha256-yt0yBOcoo10/E623/Q15AmNrefa5XEDCZetztsNTKeQ=";
  };
in
stdenvNoCC.mkDerivation {
  pname = "godot-bin";
  inherit version;

  editor = fetchurl {
    url = "https://github.com/godotengine/godot-builds/releases/download/${version}-stable/Godot_v${version}-stable_${editor.name}.zip";
    inherit (editor) hash;
  };
  templates = fetchurl {
    url = "https://github.com/godotengine/godot-builds/releases/download/${version}-stable/Godot_v${version}-stable_export_templates.tpz";
    hash = "sha512-yk1xxNe4HfwV0amLqgdTSqlbA/3aeKAHWwZnLhZI0uX0CYDJrcKNI+G5LnMu5780YZl6qASvdOwvzXqTzLhAeQ==";
  };

  nativeBuildInputs = [ unzip ];
  dontUnpack = true;
  dontFixup = true;
  installPhase = ''
    mkdir -p $out/bin $out/libexec $out/share/godot
    unzip -q $editor
    ${if stdenvNoCC.isDarwin then "cp -R Godot.app $out/libexec/" else "install -m755 Godot_v${version}-stable_linux.x86_64 $out/libexec/godot"}
    unzip -q $templates 'templates/android*' 'templates/ios.zip' 'templates/macos.zip' 'templates/version.txt' -d $out/share/godot
    substitute ${./godot.sh} $out/bin/godot --subst-var out --replace-fail '@stateName@' godot-stock
    chmod +x $out/bin/godot
  '';

  meta.platforms = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" ];
}
