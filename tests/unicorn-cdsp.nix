{ flake }:
let
  device = flake.legacyPackages.x86_64-linux.devices.unicorn;
  pkgs = (flake.lib.mkBuildEnvironment { buildSystem = "x86_64-linux"; }).buildPackages;
  # Compile the actual board DT with workstation tools and the evaluated queue.
  tree = device.kernel.overrideAttrs (_: {
    pname = "unicorn-cdsp-device-tree";
    outputs = [ "out" ];
    buildPhase = ''
      runHook preBuild
      make "''${makeFlags[@]}" -j"$NIX_BUILD_CORES" qcom/sm8475-xiaomi-unicorn.dtb
      runHook postBuild
    '';
    installPhase = ''
      mkdir -p "$out"
      cp arch/arm64/boot/dts/qcom/sm8475-xiaomi-unicorn.dtb "$out/"
    '';
    postInstall = "";
    dontFixup = true;
  });
in
pkgs.runCommand "unicorn-cdsp-tests"
  {
    nativeBuildInputs = [
      pkgs.python3
      pkgs.dtc
    ];
  }
  ''
    python3 ${./unicorn-cdsp.py} \
      ${tree}/sm8475-xiaomi-unicorn.dtb \
      ${device.firmware}/lib/firmware/qcom/sm8475/xiaomi/unicorn \
      ${../pkgs/unicorn/assemble-firmware.py}
    touch "$out"
  ''
