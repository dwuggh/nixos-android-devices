{
  lib,
  runCommand,
  coreutils,
  gnutar,
  xz,
}:
assert
  builtins.hashFile "sha256" ./inputs/dsp-firmware.tar.xz
  == "3a34531f88451c482875409abc60184cd722742e1fca8e450ad44f731976d16e";
runCommand "unicorn-fingerprint-dsp-firmware"
  {
    nativeBuildInputs = [
      coreutils
      gnutar
      xz
    ];
    meta.license = lib.licenses.unfree;
  }
  ''
    tar -xJf ${./inputs/dsp-firmware.tar.xz}
    echo '050dab21f7ba7d59008df52a67fd3aa5cadc4374373c0227614299fcdc5f52ef  libproxy_skel.so' | sha256sum -c -
    echo '66e7d69be88b71bc5824e15ea31bd1e6b780d2de13505ba294eb36164ac1d496  fastrpc_shell_3' | sha256sum -c -
    install -Dm0644 fastrpc_shell_3 "$out/lib/firmware/qcom/sm8475/unicorn/cdsp/fastrpc_shell_3"
    install -Dm0644 libproxy_skel.so "$out/lib/firmware/qcom/sm8475/unicorn/cdsp/libproxy_skel.so"
  ''
