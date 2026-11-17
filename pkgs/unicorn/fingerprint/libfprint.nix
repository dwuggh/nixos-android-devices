{
  lib,
  libfprint,
  stdenv,
  buildPackages,
  fingerprint-runtime,
  fingerprint-dsp-firmware,
}:
(libfprint.override { inherit stdenv; }).overrideAttrs (old: {
  pname = "unicorn-libfprint";
  outputs = [ "out" ];
  patches = (old.patches or [ ]) ++ [ ./libfprint-misc.patch ];
  nativeBuildInputs = [
    buildPackages.pkg-config
    buildPackages.meson
    buildPackages.ninja
    buildPackages.glib
  ]
  ++ lib.optionals (!stdenv.buildPlatform.canExecute stdenv.hostPlatform) [
    buildPackages.mesonEmulatorHook
  ];
  buildInputs = (old.buildInputs or [ ]) ++ [ fingerprint-runtime ];
  mesonFlags = [
    "-Ddrivers=unicorn"
    "-Dintrospection=false"
    "-Ddoc=false"
    "-Dinstalled-tests=false"
    "-Dgtk-examples=false"
    "-Dudev_rules=disabled"
    "-Dudev_hwdb=disabled"
  ];
  postPatch = (old.postPatch or "") + ''
    cp ${./libfprint-unicorn.c} libfprint/drivers/unicorn.c
    install -m0644 ${./ipc.h} libfprint/drivers/ipc.h
    install -m0644 ${./protocol.h} libfprint/drivers/protocol.h
    cat > tests/fpi-test-emulation.h <<'HEADER'
    #define FPI_EMULATION_ENV_VAR "FP_DEVICE_EMULATION"
    #define FPI_EMULATION_HELPER_BUILDDIR ""
    #define FPI_EMULATION_HELPER_INSTALLDIR ""
    #define FPI_EMULATION_HELPER_MODULE ""
    HEADER
    substituteInPlace meson.build \
      --replace-fail "drivers_info = {" "drivers_info = { 'unicorn': { 'helper': ['udev'] }," \
      --replace-fail "subdir('tests')" "" \
      --replace-fail "subdir('examples')" "" \
      --replace-fail "optional_deps = []" "optional_deps = [cc.find_library('unicorn-fingerprint', dirs: ['${fingerprint-runtime}/lib'])]"
    substituteInPlace libfprint/meson.build \
      --replace-fail "driver_sources = {" "driver_sources = { 'unicorn': files('drivers/unicorn.c'),"
  '';
  NIX_CFLAGS_COMPILE = (old.NIX_CFLAGS_COMPILE or "") + ''
    -DUF_RUNTIME="${fingerprint-runtime}/bin/unicorn-fingerprint-runtime"
    -DUF_CALLBACK_PATH="${fingerprint-runtime}/lib${
      lib.optionalString (fingerprint-runtime ? fastrpc) ":${lib.getLib fingerprint-runtime.fastrpc}/lib"
    }"
    -DUF_DSP_PATH="${fingerprint-dsp-firmware}/lib/firmware/qcom/sm8475/unicorn/cdsp"
  '';
  doCheck = false;
  doInstallCheck = false;
  meta = (old.meta or { }) // {
    description = "libfprint with the native Unicorn optical fingerprint backend";
    platforms = [
      "aarch64-linux"
      "x86_64-linux"
    ];
  };
})
