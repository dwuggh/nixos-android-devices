{
  lib,
  fprintd,
  libfprint,
  buildPackages,
  stdenv,
}:
(fprintd.override { inherit libfprint stdenv; }).overrideAttrs (old: {
  outputs = [ "out" ];
  patches = (old.patches or [ ]) ++ [ ./fprintd-template-protection.patch ];
  nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ buildPackages.glib ];
  mesonFlags = (lib.filter (flag: !(lib.hasPrefix "-Dgtk_doc=" flag)) (old.mesonFlags or [ ])) ++ [
    "-Dgtk_doc=false"
  ];
  # The daemon's hardware tests cannot run during cross compilation. The
  # Unicorn IPC/driver and listener suites are separate offline checks.
  doCheck = false;
})
