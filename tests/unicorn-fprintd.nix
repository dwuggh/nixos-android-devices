{ pkgs }:
let
  # Same public libfprint/storage API as production; no fingerprint hardware.
  backend = pkgs.libfprint.overrideAttrs (old: {
    postPatch = (old.postPatch or "") + ''
      substituteInPlace libfprint/drivers/virtual-device-storage.c \
        --replace-fail 'dev_class->id = FP_COMPONENT;' 'dev_class->id = "unicorn";'
    '';
    nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ (old.nativeInstallCheckInputs or [ ]);
    doCheck = false;
    doInstallCheck = false;
  });
  daemon = pkgs.callPackage ../pkgs/unicorn/fingerprint/fprintd.nix { libfprint = backend; };
in
daemon.overrideAttrs (old: {
  pname = "unicorn-fprintd-template-tests";
  doCheck = true;
  postPatch = (old.postPatch or "") + ''
    cp ${./unicorn-fprintd.py} tests/unicorn_protection.py
  '';
  checkPhase = ''
    runHook preCheck
    export FPRINT_BUILD_DIR="$PWD/src"
    export PYTHONPATH="$PWD/../tests:$PYTHONPATH"
    ${pkgs.python3.interpreter} ../tests/unicorn_protection.py Protection -v
    ${pkgs.python3.interpreter} ../tests/unicorn_protection.py FirstEnrollment -v
    runHook postCheck
  '';
})
