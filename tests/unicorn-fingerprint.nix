{ flake }:
let
  lib = flake.inputs.nixpkgs.lib;
in
lib.genAttrs [ "x86_64-linux" "aarch64-linux" ] (
  buildSystem:
  let
    make =
      modules:
      flake.lib.mkSystem {
        device = "unicorn";
        inherit buildSystem modules;
        nixpkgsConfig.allowUnfree = true;
      };
    disabled = (make [ ]).config;
    system = make [ { services.fprintd.enable = true; } ];
    c = system.config;
    recovery =
      (make [
        {
          services.fprintd.enable = true;
          androidDevices.recovery.enable = true;
        }
      ]).config;
    replacement = system.pkgs.fprintd.overrideAttrs (_: {
      pname = "fprintd-override-test";
    });
    overridden =
      (make [
        {
          services.fprintd.enable = true;
          services.fprintd.package = replacement;
        }
      ]).config;
    packages = system.pkgs.androidDevicePackages.devices.unicorn;
  in
  assert !(disabled.systemd.services ? qsee-supplicant);
  assert !(disabled.systemd.services ? "qsee-app-loader@");
  assert !recovery.services.fprintd.enable;
  assert !(recovery.systemd.services ? fprintd);
  assert !(recovery.systemd.services ? qsee-supplicant);
  assert !(recovery.systemd.services ? "qsee-app-loader@");
  assert
    !(lib.any (p: lib.getName p == "unicorn-fingerprint-runtime") recovery.environment.systemPackages);
  assert
    !(lib.any (
      p: lib.getName p == "unicorn-fingerprint-ta-candidates"
    ) recovery.hardware.firmware.paths);
  assert !(disabled.systemd.services ? unicorn-fingerprint-select);
  assert !(recovery.systemd.services ? unicorn-fingerprint-select);
  assert !(recovery.systemd.user.services ? unicorn-fingerprint-hint);
  assert !(recovery.systemd.sockets ? unicorn-fingerprint-display);
  assert c.services.fprintd.package.drvPath == packages.fprintd.drvPath;
  assert c.systemd.services.unicorn-fingerprint-select.requires == [ "qsee-supplicant.service" ];
  assert lib.hasPrefix "${packages.fingerprint-dsp-firmware}/"
    c.systemd.services.unicorn-fingerprint-select.environment.ADSP_LIBRARY_PATH;
  assert lib.hasPrefix "${packages.fingerprint-runtime}/lib:"
    c.systemd.services.unicorn-fingerprint-select.environment.LD_LIBRARY_PATH;
  assert
    c.systemd.sockets.unicorn-fingerprint-display.socketConfig.ListenSequentialPacket
    == "/run/unicorn-fingerprint-display.sock";
  assert c.systemd.services.unicorn-fingerprint-display.serviceConfig.CapabilityBoundingSet == "";
  assert !(c.systemd.user.services.unicorn-fingerprint-hint.serviceConfig ? ProtectHome);
  assert !(c.systemd.user.services.unicorn-fingerprint-hint.serviceConfig ? ProtectSystem);
  assert !(c.systemd.user.services.unicorn-fingerprint-hint.serviceConfig ? PrivateTmp);
  assert c.systemd.services.unicorn-fingerprint-select.serviceConfig.RestartPreventExitStatus == [ 78 ];
  assert c.systemd.services.qsee-supplicant.serviceConfig.Type == "notify";
  assert c.systemd.services.qsee-supplicant.serviceConfig.StateDirectory == "unicorn-fingerprint";
  assert c.systemd.services.qsee-supplicant.serviceConfig.StateDirectoryMode == "0700";
  assert c.systemd.services.qsee-supplicant.wantedBy == [ ];
  assert !(c.systemd.services ? "qsee-app-loader@");
  assert lib.hasInfix "--state-dir /var/lib/unicorn-fingerprint"
    c.systemd.services.qsee-supplicant.serviceConfig.ExecStart;
  assert c.systemd.services.fprintd.requires == [ "unicorn-fingerprint-select.service" ];
  assert c.systemd.services.fprintd.after == [ "unicorn-fingerprint-select.service" ];
  assert c.systemd.services.fprintd.serviceConfig.CapabilityBoundingSet == "";
  assert lib.hasInfix "CapabilityBoundingSet=\n" c.systemd.units."fprintd.service".text;
  assert lib.hasInfix "CapabilityBoundingSet=\n"
    c.systemd.units."unicorn-fingerprint-display.service".text;
  assert lib.all (device: lib.elem device c.systemd.services.fprintd.serviceConfig.DeviceAllow) [
    "char-tee rw"
    "char-input r"
    "/dev/unicorn-fingerprint rw"
    "/dev/unicorn-fingerprint-illumination rw"
    "/dev/fastrpc-adsp rw"
    "/dev/fastrpc-cdsp rw"
    "/dev/dma_heap/system rw"
  ];
  assert c.systemd.services.fprintd.serviceConfig.NoNewPrivileges;
  assert
    c.systemd.services.qsee-supplicant.serviceConfig.CapabilityBoundingSet == [ "CAP_SYS_ADMIN" ];
  assert c.systemd.services.unicorn-fingerprint-select.serviceConfig.StateDirectory == "unicorn-fingerprint-selector";
  assert c.systemd.services.unicorn-fingerprint-select.serviceConfig.StateDirectoryMode == "0700";
  assert overridden.services.fprintd.package.drvPath == replacement.drvPath;
  assert lib.any (p: p.drvPath == replacement.drvPath) overridden.systemd.packages;
  assert lib.any (p: p.drvPath == replacement.drvPath) overridden.services.dbus.packages;
  assert lib.all (p: p.stdenv.hostPlatform.system == "aarch64-linux") [
    packages.fingerprint-runtime
    packages.qsee-supplicant
    packages.libfprint
    packages.fprintd
  ];
  assert packages.fingerprint-runtime.stdenv.buildPlatform.system == buildSystem;
  assert lib.all (a: a.assertion) c.assertions;
  {
    runtime = packages.fingerprint-runtime.drvPath;
    supplicant = packages.qsee-supplicant.drvPath;
    firmware = packages.fingerprint-ta.drvPath;
  }
)
