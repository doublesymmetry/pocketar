{
  stdenvNoCC,
  fetchurl,
  unzip,
}:
let
  release = "https://github.com/doublesymmetry/godot/releases/download/pocketar-master-20261009";
in
stdenvNoCC.mkDerivation {
  pname = "godot-pocketar";
  version = "4.8.dev-20261009";

  editor = fetchurl {
    url = "${release}/godot-pocketar-editor-macos-arm64.zip";
    hash = "sha256-CkmrPTFFSKfWRGGOPqPD6WK8r1GY1H8UY5FKIyaxeXI=";
  };
  templates = fetchurl {
    url = "${release}/godot-pocketar-templates.tpz";
    hash = "sha256-DXVKgzehS1ZIJDoB1H96DZ74e8pPdOY4C1ak7vZCAQQ=";
  };

  nativeBuildInputs = [ unzip ];
  dontUnpack = true;
  # Stripping would invalidate the editor's ad-hoc code signature.
  dontFixup = true;
  installPhase = ''
    mkdir -p $out/bin $out/libexec $out/share/godot
    unzip -q $editor -d $out/libexec
    unzip -q $templates -d $out/share/godot
    substitute ${./godot-pocketar.sh} $out/bin/godot --subst-var out
    chmod +x $out/bin/godot
  '';

  meta.platforms = [ "aarch64-darwin" ];
}
