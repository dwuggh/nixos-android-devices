# Mobile sensor gain and properties for the simple software ISP pipeline.
{ libcamera, stdenv, gst_all_1, orc }:
let
  # ORC's documentation generator runs a target executable during configuration.
  # Cross builds need the runtime library, but cannot run that generator.
  camera = if stdenv.buildPlatform.canExecute stdenv.hostPlatform then libcamera else
    libcamera.override {
      gst_all_1 = gst_all_1 // {
        gst-plugins-base = gst_all_1.gst-plugins-base.override {
          orc = orc.override { buildDevDoc = false; };
        };
      };
    };
in
camera.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [
    ./imx363.patch
    ./asphalt.patch
    ./sheng.patch
    ./unicorn.patch
  ];
})
