{ flake }:
let
  host = (flake.lib.mkBuildEnvironment {
    buildSystem = "x86_64-linux";
    config.allowUnfree = true;
  }).buildPackages;
  packages = import ../pkgs { pkgs = host; };
  lib = host.lib;
in
host.testers.runNixOSTest {
  name = "unicorn-fingerprint-services";
  node.specialArgs.androidPackages = packages;
  nodes.machine = { ... }: {
    imports = [ ../modules/devices/unicorn/fingerprint.nix ];
    options.androidDevices.recovery.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
    };
    config = {
      virtualisation.memorySize = 1024;
      networking.useDHCP = false;
      users.users.test = { isNormalUser = true; uid = 1000; };
      environment.systemPackages = [ host.python3 ];
      services.fprintd.enable = true;
      services.fprintd.package = host.fprintd;
      hardware.firmware = lib.mkForce [ ];
      # No TEE in the VM: exercise the real selector's preflight and systemd
      # exit policy without starting the hardware-dependent supplicant.
      systemd.services.unicorn-fingerprint-select = {
        requires = lib.mkForce [ ];
        bindsTo = lib.mkForce [ ];
        after = lib.mkForce [ ];
      };
    };
  };
  testScript = ''
    start_all()
    machine.wait_for_unit("multi-user.target")
    machine.succeed("loginctl enable-linger test")
    machine.wait_for_unit("user@1000.service")
    userctl = "runuser -u test -- env XDG_RUNTIME_DIR=/run/user/1000 systemctl --user "
    machine.succeed(userctl + "start unicorn-fingerprint-hint")
    machine.wait_for_unit("unicorn-fingerprint-display.service")
    machine.succeed("python3 ${./unicorn-fingerprint-services.py}")
    machine.succeed(userctl + "is-active unicorn-fingerprint-hint")
    assert machine.succeed(userctl + "show unicorn-fingerprint-hint -p NRestarts --value").strip() == "0"
    machine.fail("systemctl start unicorn-fingerprint-select", timeout=30)
    assert machine.succeed("systemctl show unicorn-fingerprint-select -p ExecMainStatus --value").strip() == "78"
    assert machine.succeed("systemctl show unicorn-fingerprint-select -p NRestarts --value").strip() == "0"
    journal_text = machine.succeed("journalctl -u unicorn-fingerprint-select -b --no-pager")
    assert "event=platform_unavailable" in journal_text and "CDSP" in journal_text
    assert "event=sensor_probe" not in journal_text
    machine.succeed("test ! -e /run/unicorn-fingerprint/profile")
  '';
}
