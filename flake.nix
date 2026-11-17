{
  description = "NixOS on Android devices: Polaris, Asphalt (Y700 2023), Sheng, Aurora (Xiaomi 14 Ultra), Clover and Unicorn (Xiaomi 12S Pro)";

  inputs.nixpkgs.url = "https://channels.nixos.org/nixos-unstable/nixexprs.tar.zst";

  outputs =
    { self, nixpkgs, ... }:
    let
      nixpkgsLib = nixpkgs.lib;
      deviceLib = import ./lib { inherit nixpkgs; };
      deviceModules = import ./lib/devices.nix;
      recurseArtifacts =
        attrs:
        nixpkgsLib.recurseIntoAttrs (
          nixpkgsLib.mapAttrs (
            _: value: if nixpkgsLib.isDerivation value then value else recurseArtifacts value
          ) attrs
        );
      mkPackages =
        buildSystem:
        let
          buildEnvironment = deviceLib.mkBuildEnvironment {
            inherit buildSystem;
            # These examples opt into proprietary OEM sensor firmware.
            config.allowUnfreePredicate =
              package:
              builtins.elem (nixpkgsLib.getName package) [
                "unicorn-firmware"
                "unicorn-firmware-zstd"
                "unicorn-fingerprint-ta-candidates"
                "unicorn-fingerprint-dsp-firmware"
                "aurora-firmware"
                "aurora-firmware-zstd"
                "aurora-touch"
                "asphalt-sensors"
                "asphalt-sensors-zstd"
                "polaris-sensors"
                "polaris-sensors-zstd"
              ];
          };
          inherit (buildEnvironment) buildPackages nixosSystem;
          devicePackages = deviceLib.mkPackageSet { inherit (buildEnvironment) pkgs; };
          hostPackages = deviceLib.mkPackageSet {
            # Cross buildPackages keeps target-specific wrapper substitutions.
            # This utility and every executable in its wrapper run on the host.
            pkgs = import nixpkgs { system = buildSystem; };
          };
          make =
            device: modules:
            nixosSystem {
              modules = [ deviceModules.${device} ] ++ modules;
            };
          unicornSystem = make "unicorn" [ ./examples/unicorn/system.nix ];
          unicornRecovery = make "unicorn" [ ./examples/unicorn/recovery.nix ];
          polarisSystem = make "polaris" [ ./examples/polaris/system.nix ];
          polarisMinimal = make "polaris" [ ./examples/polaris/minimal.nix ];
          polarisRecovery = make "polaris" [ ./examples/polaris/recovery.nix ];
          asphaltSystem = make "asphalt" [ ./examples/asphalt/system.nix ];
          asphaltRecovery = make "asphalt" [ ./examples/asphalt/recovery.nix ];
          cloverSystem = make "clover" [ ./examples/clover/system.nix ];
          cloverRecovery = make "clover" [ ./examples/clover/recovery.nix ];
          shengSystem = make "sheng" [ ./examples/sheng/system.nix ];
          auroraSystem = make "aurora" [ ./examples/aurora/system.nix ];
          auroraRecovery = make "aurora" [ ./examples/aurora/recovery.nix ];
          shengRecovery = make "sheng" [ ./examples/sheng/recovery.nix ];
        in
        {
          devices.unicorn.system = unicornSystem.config.system.build.toplevel;
          devices.unicorn.boot = unicornSystem.config.system.build.mobileBootImage;
          devices.unicorn.u-boot = devicePackages.mkUBoot { device = "unicorn"; };
          devices.unicorn.boot-files = unicornSystem.config.system.build.mobileBootFiles;
          devices.unicorn.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ unicornSystem.config.system.build.toplevel ];
          };
          devices.unicorn.recovery = unicornRecovery.config.system.build.mobileBootImage;
          devices.unicorn.kernel = devicePackages.devices.unicorn.kernel;
          devices.unicorn.mipps-auth = devicePackages.devices.unicorn.mipps-auth;
          devices.unicorn.fingerprint-runtime = devicePackages.devices.unicorn.fingerprint-runtime;
          devices.unicorn.fingerprint-dsp-firmware = devicePackages.devices.unicorn.fingerprint-dsp-firmware;
          devices.unicorn.libfprint = devicePackages.devices.unicorn.libfprint;
          devices.unicorn.fprintd = devicePackages.devices.unicorn.fprintd;
          devices.unicorn.fingerprint-ta = devicePackages.devices.unicorn.fingerprint-ta;
          devices.unicorn.qsee-supplicant = devicePackages.devices.unicorn.qsee-supplicant;
          devices.unicorn.firmware = devicePackages.devices.unicorn.firmware;
          devices.unicorn.boot-kernel = unicornSystem.config.system.build.mobileDirectBootImage;
          devices.aurora.system = auroraSystem.config.system.build.toplevel;
          devices.aurora.boot = auroraSystem.config.system.build.mobileBootImage;
          devices.aurora.boot-files = auroraSystem.config.system.build.mobileBootFiles;
          devices.aurora.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ auroraSystem.config.system.build.toplevel ];
          };
          devices.aurora.recovery = auroraRecovery.config.system.build.mobileBootImage;
          devices.aurora.u-boot = devicePackages.mkUBoot { device = "aurora"; };
          devices.aurora.kernel = devicePackages.devices.aurora.kernel;
          devices.aurora.firmware = devicePackages.devices.aurora.firmware;
          devices.aurora.touch = devicePackages.devices.aurora.touch;
          devices.polaris.system = polarisSystem.config.system.build.toplevel;
          devices.polaris.minimal.system = polarisMinimal.config.system.build.toplevel;
          devices.polaris.minimal.rootfs = polarisMinimal.config.system.build.mobileSystemImage;
          devices.polaris.minimal.boot-files = polarisMinimal.config.system.build.mobileBootFiles;
          devices.polaris.minimal.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ polarisMinimal.config.system.build.toplevel ];
          };
          devices.polaris.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ polarisSystem.config.system.build.toplevel ];
          };
          devices.polaris.charger = polarisSystem.config.system.build.mobileChargerImage;
          devices.polaris.boot = polarisSystem.config.system.build.mobileBootImage;
          devices.polaris.recovery = polarisRecovery.config.system.build.mobileBootImage;
          devices.polaris.boot-files = polarisSystem.config.system.build.mobileBootFiles;
          devices.polaris.u-boot = devicePackages.mkUBoot { device = "polaris"; };
          devices.polaris.kernel = devicePackages.devices.polaris.kernel;

          usb-signaller = devicePackages.usb-signaller;
          mobile-boot-action = devicePackages.mobile-boot-action;
          mobile-charger = devicePackages.mobile-charger;
          mobile-charger-host = hostPackages.mobile-charger;
          mobile-device = hostPackages.mobile-device;
          android-tools = buildPackages.android-tools;
          fastrpc = devicePackages.fastrpc;
          sensor-rotate = devicePackages.sensor-rotate;
          libva-v4l2 = devicePackages.libva-v4l2;
          minkipc = devicePackages.minkipc;
          mink-modem-opener = devicePackages.mink-modem-opener;
          devices.asphalt.sensors = devicePackages.devices.asphalt.sensors;
          devices.polaris.sensors = devicePackages.devices.polaris.sensors;
          kernel-tools = buildPackages.buildEnv {
            name = "android-kernel-host-tools";
            paths = import ./lib/kernel-tools.nix buildPackages;
          };
          devices.asphalt.u-boot = devicePackages.mkUBoot { device = "asphalt"; };
          devices.asphalt.system = asphaltSystem.config.system.build.toplevel;
          devices.asphalt.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ asphaltSystem.config.system.build.toplevel ];
          };
          devices.asphalt.boot-files = asphaltSystem.config.system.build.mobileBootFiles;
          devices.asphalt.boot = asphaltSystem.config.system.build.mobileBootImage;
          devices.asphalt.charger = asphaltSystem.config.system.build.mobileChargerImage;
          devices.asphalt.recovery = asphaltRecovery.config.system.build.mobileBootImage;
          devices.asphalt.kernel = devicePackages.devices.asphalt.kernel;
          devices.clover.u-boot = devicePackages.mkUBoot { device = "clover"; };
          devices.clover.system = cloverSystem.config.system.build.toplevel;
          devices.clover.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ cloverSystem.config.system.build.toplevel ];
          };
          devices.clover.boot-files = cloverSystem.config.system.build.mobileBootFiles;
          devices.clover.boot = cloverSystem.config.system.build.mobileBootImage;
          devices.clover.boot-kernel = cloverSystem.config.system.build.mobileDirectBootImage;
          devices.clover.recovery = cloverRecovery.config.system.build.mobileBootImage;
          devices.clover.kernel = devicePackages.devices.clover.kernel;
          devices.sheng.u-boot = devicePackages.mkUBoot { device = "sheng"; };
          devices.sheng.system = shengSystem.config.system.build.toplevel;
          devices.sheng.bootstrap-info = buildPackages.closureInfo {
            rootPaths = [ shengSystem.config.system.build.toplevel ];
          };
          devices.sheng.boot-files = shengSystem.config.system.build.mobileBootFiles;
          devices.sheng.boot = shengSystem.config.system.build.mobileBootImage;
          devices.sheng.boot-kernel = shengSystem.config.system.build.mobileDirectBootImage;
          devices.sheng.recovery = shengRecovery.config.system.build.mobileBootImage;
          devices.sheng.kernel = devicePackages.devices.sheng.kernel;
        };
    in
    {
      lib = deviceLib;
      overlays.default = import ./overlays;
      nixosModules = deviceModules // {
        host = ./modules/host;
      };
      checks = nixpkgsLib.genAttrs [ "aarch64-linux" "x86_64-linux" ] (
        buildSystem:
        let
          pkgs = (deviceLib.mkBuildEnvironment { inherit buildSystem; }).buildPackages;
          evaluation =
            name: path:
            builtins.seq (builtins.stringLength (
              builtins.toJSON ((import path { flake = self; }).${buildSystem})
            )) (pkgs.runCommand name { } "touch $out");
        in
        {
          packaging = import ./tests/packaging.nix {
            flake = self;
            inherit buildSystem;
          };
          host = evaluation "android-development-host" ./tests/host.nix;
          public-api = evaluation "android-public-api" ./tests/public-api.nix;
          charger = evaluation "android-charger" ./tests/charger.nix;
          charger-install =
            pkgs.runCommand "android-charger-install"
              {
                nativeBuildInputs = [ pkgs.python3 ];
              }
              ''
                export PYTHONDONTWRITEBYTECODE=1
                mkdir -p modules/mobile tests
                cp ${./modules/mobile/install-charger.py} modules/mobile/install-charger.py
                cp ${./tests/charger-install.py} tests/charger-install.py
                python3 tests/charger-install.py
                touch $out
              '';
          platforms = evaluation "android-platforms" ./tests/eval-platforms.nix;
          video-vaapi = evaluation "android-video-vaapi" ./tests/video-vaapi.nix;
          native-userspace = evaluation "android-native-userspace" ./tests/native-userspace.nix;
          ssc-sensors = evaluation "android-ssc-sensors" ./tests/ssc-sensors.nix;
          ssc-sensors-unit =
            pkgs.runCommand "android-ssc-sensors-unit"
              {
                nativeBuildInputs = [
                  pkgs.python3
                  pkgs.coreutils
                  pkgs.bash
                ];
              }
              ''
                export PYTHONDONTWRITEBYTECODE=1 MOBILE_TEST_TMP="$TMPDIR"
                mkdir -p project/pkgs/mobile/ssc-registry project/pkgs/mobile/sensor-rotate
                cp ${./pkgs/mobile/ssc-registry/generate.py} project/pkgs/mobile/ssc-registry/generate.py
                cp ${./pkgs/mobile/sensor-rotate/rotate.py} project/pkgs/mobile/sensor-rotate/rotate.py
                mkdir -p project/pkgs/asphalt/sensors project/pkgs/polaris/sensors
                cp -r ${./pkgs/asphalt/sensors/config} project/pkgs/asphalt/sensors/config
                cp -r ${./pkgs/polaris/sensors/config} project/pkgs/polaris/sensors/config
                python3 ${./tests/ssc-sensors.py} project
                touch "$out"
              '';
          direct-boot = evaluation "android-direct-boot" ./tests/direct-boot.nix;
          boot-actions = evaluation "android-boot-actions" ./tests/boot-actions.nix;
          unicorn-usb-kernel = import ./tests/unicorn-usb-kernel.nix { flake = self; };
          unicorn-cdsp = import ./tests/unicorn-cdsp.nix {
            flake = self;
          };
          usb-modes = pkgs.writeText "android-usb-modes.json" (
            builtins.unsafeDiscardStringContext (
              builtins.toJSON (import ./tests/usb-modes.nix { flake = self; })
            )
          );
          usb-gadgets = pkgs.runCommand "usb-gadgets-tests" {
            nativeBuildInputs = [ pkgs.python3 pkgs.dosfstools ];
          } ''
            export PYTHONDONTWRITEBYTECODE=1
            mkdir -p tests pkgs/mobile/usb-gadgets
            cp ${./tests/usb-gadgets.py} tests/usb-gadgets.py
            cp ${./pkgs/mobile/usb-gadgets}/*.py pkgs/mobile/usb-gadgets/
            python3 tests/usb-gadgets.py
            touch "$out"
          '';
          usb-gadgets-vm = import ./tests/usb-gadgets-vm.nix { flake = self; };
          # Run state-machine unit tests natively on the workstation; usb-tools
          # separately verifies the cross-compiled ARM64 daemon/responder.
          usb-manager = (
            (deviceLib.mkBuildEnvironment { buildSystem = "x86_64-linux"; }).buildPackages.callPackage
              ./pkgs/usb-signaller/default.nix
              { }
          );
          umtp-mqueue =
            ((deviceLib.mkBuildEnvironment { buildSystem = "x86_64-linux"; }).buildPackages.callPackage
              ./pkgs/mobile/umtp-responder.nix
              { }
            ).overrideAttrs
              { doCheck = true; };
          usb-tethering = pkgs.runCommand "usb-tethering-tests" { nativeBuildInputs = [ pkgs.python3 ]; } ''
            mkdir -p tests modules/mobile
            cp ${./tests/usb-tethering.py} tests/usb-tethering.py
            cp ${./modules/mobile/usb-tethering.py} modules/mobile/usb-tethering.py
            python3 tests/usb-tethering.py
            touch "$out"
          '';
          usb-signaller = evaluation "android-usb-signaller" ./tests/usb-signaller.nix;
          usb-serial-lifecycle = import ./tests/usb-serial-lifecycle.nix {
            flake = self;
            inherit pkgs buildSystem;
          };
          recovery-host-key = evaluation "android-recovery-host-key" ./tests/recovery-host-key.nix;
          package-scope = evaluation "android-package-scope" ./tests/package-scope.nix;
          polaris-minimal = evaluation "android-polaris-minimal" ./tests/polaris-minimal.nix;
          asphalt-uefi = evaluation "android-asphalt-uefi" ./tests/asphalt-uefi.nix;
          unicorn = evaluation "android-unicorn" ./tests/unicorn.nix;
          unicorn-fingerprint-config = evaluation "android-unicorn-fingerprint" ./tests/unicorn-fingerprint.nix;
          unicorn-fingerprint = import ./tests/unicorn-fingerprint-check.nix { inherit self nixpkgs; };
          unicorn-fingerprint-services = import ./tests/unicorn-fingerprint-services.nix { flake = self; };
          unicorn-charging = import ./tests/unicorn-charging.nix {
            flake = self;
            inherit pkgs buildSystem;
          };
          unicorn-display-lifecycle = import ./tests/unicorn-display-lifecycle.nix {
            flake = self;
            inherit pkgs buildSystem;
          };
          unicorn-hardware-keys =
            pkgs.runCommand "unicorn-hardware-keys-tests" { nativeBuildInputs = [ pkgs.python3 ]; }
              ''
                export PYTHONDONTWRITEBYTECODE=1
                mkdir -p tests pkgs/unicorn
                cp ${./tests/unicorn-hardware-keys.py} tests/unicorn-hardware-keys.py
                cp ${./pkgs/unicorn/hardware-keys.py} pkgs/unicorn/hardware-keys.py
                python3 tests/unicorn-hardware-keys.py
                touch "$out"
              '';
          # This suite evaluates one fixed AArch64 recovery configuration.
          recovery-usb = builtins.seq (builtins.stringLength (
            builtins.toJSON (import ./tests/recovery-usb.nix { flake = self; })
          )) (pkgs.runCommand "android-recovery-usb" { } "touch $out");
          acm-login =
            pkgs.runCommand "android-acm-login-tests"
              {
                nativeBuildInputs = [
                  pkgs.python3
                  pkgs.bash
                  pkgs.util-linux
                ];
              }
              ''
                export PYTHONDONTWRITEBYTECODE=1 MOBILE_TEST_TMP="$TMPDIR"
                python3 ${./tests/acm-wait-host.py} ${./modules/mobile/acm-wait-host.sh}
                touch "$out"
              '';
          boot-actions-unit =
            pkgs.runCommand "mobile-boot-action-tests"
              {
                nativeBuildInputs = [
                  pkgs.stdenv.cc
                  pkgs.gnu-efi
                  pkgs.python3
                  pkgs.bash
                ];
              }
              ''
                mkdir -p tests pkgs/mobile-boot-action modules/mobile
                cp ${./tests/boot-actions.c} tests/boot-actions.c
                cp ${./pkgs/mobile-boot-action/main.c} pkgs/mobile-boot-action/main.c
                cp ${./pkgs/mobile-boot-action/protocol.h} pkgs/mobile-boot-action/protocol.h
                cp ${./pkgs/mobile-boot-action/pon-transaction.h} pkgs/mobile-boot-action/pon-transaction.h
                cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
                  -ffreestanding -fshort-wchar \
                  -I${pkgs.gnu-efi}/include/efi -I${pkgs.gnu-efi}/include/efi/${pkgs.stdenv.hostPlatform.parsed.cpu.name} \
                  tests/boot-actions.c -o boot-actions-test
                ./boot-actions-test
                cp ${./tests/boot-actions-ab.c} tests/boot-actions-ab.c
                cp ${./pkgs/mobile-boot-action/qcom-ab.h} pkgs/mobile-boot-action/qcom-ab.h
                cc -std=c11 -Wall -Wextra -Werror tests/boot-actions-ab.c -o boot-actions-ab-test
                ./boot-actions-ab-test
                cp ${./tests/boot-actions-ab-adapter.c} tests/boot-actions-ab-adapter.c
                cp ${./tests/boot-actions-ab-adapter.py} tests/boot-actions-ab-adapter.py
                cp ${./pkgs/mobile-boot-action/qcom-ab-firmware.h} pkgs/mobile-boot-action/qcom-ab-firmware.h
                export MOBILE_TEST_TMP="$TMPDIR"
                export MOBILE_EFI_CFLAGS="-I${pkgs.gnu-efi}/include/efi -I${pkgs.gnu-efi}/include/efi/${pkgs.stdenv.hostPlatform.parsed.cpu.name}"
                python3 tests/boot-actions-ab-adapter.py
                cp ${./tests/boot-actions-confirm.c} tests/boot-actions-confirm.c
                cp ${./tests/boot-actions-confirm.py} tests/boot-actions-confirm.py
                cp ${./pkgs/mobile-boot-action/qcom-ab-confirm.h} pkgs/mobile-boot-action/qcom-ab-confirm.h
                mkdir -p pkgs/u-boot/patches
                cp ${./pkgs/u-boot/patches/0009-mobile-qcom-ab-efi-confirm.patch} pkgs/u-boot/patches/0009-mobile-qcom-ab-efi-confirm.patch
                export MOBILE_UBOOT_SOURCE="${
                  self.legacyPackages.${buildSystem}.devices.sheng.u-boot.firmware.src
                }/lib/efi_loader/efi_bootbin.c"
                python3 tests/boot-actions-confirm.py
                cp ${./pkgs/u-boot/patches/0008-mobile-ufs-boot-lun.patch} pkgs/u-boot/patches/0008-mobile-ufs-boot-lun.patch
                cp ${./tests/boot-actions-ufs.c} tests/boot-actions-ufs.c
                cp ${./tests/boot-actions-ufs.py} tests/boot-actions-ufs.py
                python3 tests/boot-actions-ufs.py
                cp ${./pkgs/mobile-boot-action/firmware.c} pkgs/mobile-boot-action/firmware.c
                cp ${./tests/boot-actions-firmware.c} tests/boot-actions-firmware.c
                cp ${./tests/boot-actions-uboot-mocks.h} tests/boot-actions-uboot-mocks.h
                cp ${./tests/boot-actions-firmware.py} tests/boot-actions-firmware.py
                cp ${./tests/boot-actions-install.py} tests/boot-actions-install.py
                cp ${./modules/mobile/loader-actions.py} modules/mobile/loader-actions.py
                export MOBILE_TEST_TMP="$TMPDIR" PYTHONDONTWRITEBYTECODE=1
                python3 tests/boot-actions-firmware.py
                python3 tests/boot-actions-install.py ${
                  pkgs.writeText "boot-actions-evaluations.json" (
                    builtins.unsafeDiscardStringContext (
                      builtins.toJSON {
                        ${buildSystem} = (import ./tests/boot-actions.nix { flake = self; }).${buildSystem};
                      }
                    )
                  )
                } ${nixpkgs.outPath}
                touch $out
              '';
          module-boundary = evaluation "android-module-boundary" ./tests/mobile-module-boundary.nix;
          release-config = evaluation "android-release-config" ./tests/release-config.nix;
          # The Rust recovery daemon's unit tests (storage roles, GPT, BCB and
          # operation contracts) built for the build platform.
          recovery-daemon = pkgs.callPackage ./pkgs/mobile-recovery-screen {
            device = "polaris";
            features = [
              "refresh-panel"
              "recovery-daemon"
            ];
          };
          host-tools =
            pkgs.runCommand "android-host-tools"
              {
                nativeBuildInputs = [
                  pkgs.python3
                  pkgs.bash
                ];
              }
              ''
                export PYTHONDONTWRITEBYTECODE=1
                python3 -m unittest discover -s ${./pkgs/mobile-device} -p 'test_*.py'
                python3 -m unittest discover -s ${./pkgs/mobile} -p 'test_*.py'
                cp -r ${./tests} tests
                mkdir -p tmp modules/devices/polaris
                cp ${./modules/devices/polaris/ufs-safe-mode.sh} modules/devices/polaris/ufs-safe-mode.sh
                export MOBILE_TEST_TMP="$PWD/tmp"
                for test in mobile-ufs.py basic/test-runner.py; do
                  python3 tests/$test
                done
                touch $out
              '';
        }
      );
      devShells = nixpkgsLib.genAttrs [ "aarch64-linux" "x86_64-linux" ] (
        buildSystem:
        let
          environment = deviceLib.mkBuildEnvironment { inherit buildSystem; };
        in
        {
          kernel = environment.buildPackages.mkShell {
            packages = import ./lib/kernel-tools.nix environment.buildPackages;
            MOBILE_ARM64_BOOT_WRAPPER = "${./pkgs/mobile/arm64-boot-wrapper.py}";
            ARCH = "arm64";
            LLVM = "1";
            CROSS_COMPILE = environment.compilationPkgs.stdenv.cc.targetPrefix;
          };
        }
      );
      # Keep generic tools in the flat packages schema. Nested device artifacts
      # use legacyPackages and resolve as `nix build .#devices.<device>.<role>`.
      packages = nixpkgsLib.genAttrs [ "aarch64-linux" "x86_64-linux" ] (
        system: builtins.removeAttrs (mkPackages system) [ "devices" ]
      );
      legacyPackages = nixpkgsLib.genAttrs [ "aarch64-linux" "x86_64-linux" ] (system: {
        devices = recurseArtifacts (mkPackages system).devices;
      });
    };
}
