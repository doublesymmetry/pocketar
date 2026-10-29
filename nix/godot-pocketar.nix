{
  stdenvNoCC,
  fetchurl,
  unzip,
}:
let
  release = "https://github.com/doublesymmetry/godot/releases/download/pocketar-master-20261009-r3";
  editors = {
    aarch64-darwin = {
      name = "macos-arm64";
      hash = "sha256-oWX6R8YgFp7V80Z8KRb0pTUWDZpVa77x79WaJ+yRtEw=";
    };
    x86_64-linux = {
      name = "linux-x86_64";
      hash = "sha256-D5bp1mDtUn0BxTeiXgkD6Eiu/MFvLi7fuaod1lGuvJs=";
    };
  };
  editor = editors.${stdenvNoCC.hostPlatform.system};
in
stdenvNoCC.mkDerivation {
  pname = "godot-pocketar";
  version = "4.8.dev-20261009-r3";

  editor = fetchurl {
    url = "${release}/godot-pocketar-editor-${editor.name}.zip";
    inherit (editor) hash;
  };
  templates = fetchurl {
    url = "${release}/godot-pocketar-templates.tpz";
    hash = "sha256-uxVQRq9Fmq+R4oCg6dZOQg97CpQfFhhpFVLHKkjCclU=";
  };

  nativeBuildInputs = [ unzip ];
  dontUnpack = true;
  # Stripping would invalidate the editor's ad-hoc code signature.
  dontFixup = true;
  installPhase = ''
    mkdir -p $out/bin $out/libexec $out/share/godot
    unzip -q $editor -d $out/libexec
    unzip -q $templates -d $out/share/godot
    substitute ${./godot.sh} $out/bin/godot --subst-var out --replace-fail '@stateName@' godot-pocketar
    chmod +x $out/bin/godot
  '';

  meta.platforms = builtins.attrNames editors;
}
