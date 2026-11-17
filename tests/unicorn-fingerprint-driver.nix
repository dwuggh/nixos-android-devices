{
  pkgs,
  runtime,
  firmware,
}:
let
  fake = pkgs.runCommandCC "unicorn-fingerprint-fake-worker" { } ''
    mkdir -p "$out/bin" "$out/lib"
    $CC -std=c11 -O2 -Wall -Wextra -Werror -I${../pkgs/unicorn/fingerprint} \
      ${../pkgs/unicorn/fingerprint/tests/fake-worker.c} -o "$out/bin/unicorn-fingerprint-runtime"
    ln -s ${runtime}/lib/libunicorn-fingerprint.so "$out/lib/"
  '';
  backend = pkgs.callPackage ../pkgs/unicorn/fingerprint/libfprint.nix {
    fingerprint-runtime = fake;
    fingerprint-dsp-firmware = firmware;
  };
in
pkgs.runCommandCC "unicorn-fingerprint-libfprint-tests"
  {
    nativeBuildInputs = [ pkgs.pkg-config ];
    buildInputs = [
      backend
      pkgs.glib
    ];
  }
  ''
    $CC -std=c11 -O2 -Wall -Wextra -Werror \
      ${../pkgs/unicorn/fingerprint/tests/driver.c} \
      $(pkg-config --cflags --libs libfprint-2 gio-2.0) -o driver-test
    G_DEBUG=fatal-warnings ./driver-test
    touch "$out"
  ''
