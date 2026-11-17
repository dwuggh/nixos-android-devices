{
  lib,
  stdenv,
  fetchFromGitHub,
}:
stdenv.mkDerivation {
  pname = "qsee-supplicant";
  version = "0.1.1-36e0668";
  src = fetchFromGitHub {
    owner = "wrobelda";
    repo = "qsee-supplicant";
    rev = "36e06680cf7f690fccbdcd07abc2a64c4bb061d8";
    hash = "sha256-UGnO3io7uC22a1hDXpVwliWchkErDRLPcjXV3Xqv868=";
  };
  strictDeps = true;
  doCheck = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
  installPhase = ''
    runHook preInstall
    make install-bin install-doc PREFIX="$out" SBINDIR="$out/bin"
    runHook postInstall
  '';
  meta = {
    description = "Pinned QSEECOM filesystem listeners and separate TA loader";
    license = with lib.licenses; [
      bsd2
      asl20
    ];
    platforms = lib.platforms.linux;
  };
}
