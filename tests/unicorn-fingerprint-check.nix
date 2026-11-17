{ self, nixpkgs }:
let
  # The helper's ARM64 check namespace can still run this offline suite on
  # the workstation and build its target libraries with native cross tools.
  pkgs = import nixpkgs { system = "x86_64-linux"; };
  targets = self.legacyPackages.x86_64-linux.devices.unicorn;
  runtime = pkgs.callPackage ../pkgs/unicorn/fingerprint {
    fastrpc = (pkgs.callPackage ../pkgs/mobile/fastrpc { }).overrideAttrs (old: {
      patches = (old.patches or [ ]) ++ [ ../pkgs/unicorn/fingerprint/fastrpc-shell-path.patch ];
    });
    qsee-supplicant = supplicant;
  };
  driver = import ./unicorn-fingerprint-driver.nix {
    inherit pkgs runtime;
    firmware = targets.fingerprint-dsp-firmware;
  };
  daemonTests = import ./unicorn-fprintd.nix { inherit pkgs; };
  supplicant = pkgs.callPackage ../pkgs/unicorn/fingerprint/qsee-supplicant.nix { };
in
pkgs.runCommand "unicorn-fingerprint-offline-tests"
  {
    nativeBuildInputs = [
      pkgs.stdenv.cc
      pkgs.gnumake
      pkgs.python3
    ];
  }
  ''
    cp -r ${supplicant.src} supplicant
    chmod -R u+w supplicant
    make -C supplicant check
    cc -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra -Werror \
      -Isupplicant/include ${./unicorn-fingerprint-storage.c} \
      supplicant/src/path.c supplicant/src/handle_db.c supplicant/src/gpfs.c \
      -pthread -Wl,--wrap=renameat -o storage-test
    ./storage-test
    # These dependencies require the cross-built ARM64 artifacts to exist.
    test -x ${targets.fingerprint-runtime}/bin/unicorn-fingerprint-runtime
    test -x ${targets.qsee-supplicant}/bin/qsee-supplicant
    test -f ${targets.fingerprint-ta}/lib/firmware/gfuni6.mdt
    test -e ${driver}
    test -x ${daemonTests}/libexec/fprintd
    test -x ${targets.fprintd}/bin/fprintd-enroll
    ${runtime}/bin/unicorn-fingerprint-runtime status || test "$?" -eq 3
    mkdir -p "$out"
    cp ${../pkgs/unicorn/fingerprint/evidence.json} "$out/evidence.json"
    python3 - "$out/results.json" <<'PY'
    import json, sys
    from pathlib import Path
    Path(sys.argv[1]).write_text(json.dumps({
      "native_runtime": "${runtime}",
      "target_runtime": "${targets.fingerprint-runtime}",
      "target_supplicant": "${targets.qsee-supplicant}",
      "target_firmware": "${targets.fingerprint-ta}",
      "biometric_qualification": "not performed",
    }, indent=2) + "\n")
    PY
  ''
