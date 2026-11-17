{ flake, pkgs, buildSystem }:
let
  kernel = flake.legacyPackages.${buildSystem}.devices.unicorn.kernel;
in
pkgs.stdenv.mkDerivation {
  pname = "unicorn-display-lifecycle-tests";
  version = "1";
  inherit (kernel) src patches;
  nativeBuildInputs = [ pkgs.python3 ];
  phases = [ "unpackPhase" "patchPhase" "buildPhase" "installPhase" ];
  # Apply the evaluated queue, including the kernel's own patches, to real source.
  patchFlags = [ "-p1" "--fuzz=0" ];
  buildPhase = ''
    export PYTHONDONTWRITEBYTECODE=1
    cp ${./unicorn-display-lifecycle.py} "$TMPDIR/unicorn-display-lifecycle.py"
    cp ${./unicorn-display-lifecycle.c} "$TMPDIR/unicorn-display-lifecycle.c"
    python3 "$TMPDIR/unicorn-display-lifecycle.py" --source "$PWD"
    cp ${./unicorn-fingerprint-lease.py} "$TMPDIR/unicorn-fingerprint-lease.py"
    cp ${./unicorn-fingerprint-lease.c} "$TMPDIR/unicorn-fingerprint-lease.c"
    python3 "$TMPDIR/unicorn-fingerprint-lease.py" --source "$PWD"
    cp ${./unicorn-qsee-listener.py} "$TMPDIR/unicorn-qsee-listener.py"
    cp ${./unicorn-qsee-listener.c} "$TMPDIR/unicorn-qsee-listener.c"
    python3 "$TMPDIR/unicorn-qsee-listener.py" --source "$PWD"
    cp ${./unicorn-qsee-staging.py} "$TMPDIR/unicorn-qsee-staging.py"
    cp ${./unicorn-qsee-staging.c} "$TMPDIR/unicorn-qsee-staging.c"
    python3 "$TMPDIR/unicorn-qsee-staging.py" --source "$PWD"
  '';
  installPhase = ''touch "$out"'';
}
