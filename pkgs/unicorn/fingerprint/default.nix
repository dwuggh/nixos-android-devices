{
  lib,
  stdenv,
  pkg-config,
  wayland-scanner,
  wayland-protocols,
  wayland,
  cairo,
  systemd,
  openssl,
  pam,
  python3,
  fastrpc,
  qsee-supplicant ? null,
}:
stdenv.mkDerivation {
  pname = "unicorn-fingerprint-runtime";
  version = "0.2.0";
  src = lib.cleanSourceWith {
    src = ./.;
    filter =
      path: type:
      if type == "directory" then
        baseNameOf path != "generated"
      else
        lib.hasSuffix ".c" path
        || lib.hasSuffix ".h" path
        || lib.hasSuffix ".xml" path
        || lib.hasSuffix ".py" path
        || baseNameOf path == "Makefile";
  };
  nativeBuildInputs = [
    pkg-config
    wayland-scanner
  ];
  nativeCheckInputs = [ python3 ];
  buildInputs = [
    wayland-protocols
    wayland
    cairo
    systemd
    openssl
    pam
  ];
  CPPFLAGS =
    ''-DUF_FASTRPC_LIBRARY='"${lib.getLib fastrpc}/lib/libadsprpc.so"' ''
    + lib.optionalString (
      qsee-supplicant != null
    ) ''-DUF_APP_LOADER='"${qsee-supplicant}/bin/qsee-app-loader"' '';
  passthru = { inherit fastrpc; };
  strictDeps = true;
  doCheck = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
  meta = {
    description = "Native Unicorn optical fingerprint runtime and Wayland capture hint";
    license = [
      lib.licenses.mit
      lib.licenses.bsd3
    ];
    platforms = lib.platforms.linux;
    mainProgram = "unicorn-fingerprint-runtime";
  };
}
