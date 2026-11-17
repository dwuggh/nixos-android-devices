{
  config,
  lib,
  androidPackages,
  ...
}:
let
  packages = androidPackages.devices.unicorn;
  enabled = config.services.fprintd.enable && !config.androidDevices.recovery.enable;
  privileged = {
    Type = "notify";
    NotifyAccess = "main";
    TimeoutStartSec = 30;
    Restart = "on-failure";
    RestartSec = 2;
    CapabilityBoundingSet = [ "CAP_SYS_ADMIN" ];
    NoNewPrivileges = true;
    PrivateTmp = true;
    ProtectSystem = "strict";
    ProtectHome = true;
    ProtectKernelTunables = true;
    ProtectKernelModules = true;
    ProtectControlGroups = true;
    RestrictAddressFamilies = [ "AF_UNIX" ];
    RestrictRealtime = true;
    LockPersonality = true;
    MemoryDenyWriteExecute = true;
    UMask = "0077";
    LimitCORE = 0;
  };
in
{
  # TEE numbering is dynamic; match the QSEECOM driver by ancestry.
  services.udev.extraRules = ''
    SUBSYSTEM=="tee", DRIVERS=="qcom_qseecom_tee", OWNER="root", GROUP="root", MODE="0600"
  '';

  # Select the first candidate whose full initialization and calibration succeed.
  # Deployment and first biometric qualification remain separate operations.
  environment.systemPackages = lib.mkIf enabled [ packages.fingerprint-runtime ];
  hardware.firmware = lib.mkIf enabled [
    packages.fingerprint-ta
    packages.fingerprint-dsp-firmware
  ];
  services.fprintd.enable = lib.mkIf config.androidDevices.recovery.enable (lib.mkForce false);
  services.fprintd.package = lib.mkIf enabled (lib.mkDefault packages.fprintd);
  systemd.sockets.unicorn-fingerprint-display = lib.mkIf enabled {
    description = "Unicorn fingerprint display descriptor broker";
    wantedBy = [ "sockets.target" ];
    socketConfig = {
      ListenSequentialPacket = "/run/unicorn-fingerprint-display.sock";
      SocketMode = "0666";
      RemoveOnStop = true;
    };
  };
  systemd.user.services.unicorn-fingerprint-hint = lib.mkIf enabled {
    description = "Draw and acknowledge the Unicorn fingerprint capture region";
    wantedBy = [ "graphical-session.target" ];
    partOf = [ "graphical-session.target" ];
    after = [ "graphical-session-pre.target" ];
    serviceConfig = {
      ExecStart = "${packages.fingerprint-runtime}/bin/unicorn-fingerprint-hint";
      Restart = "on-failure";
      RestartSec = 2;
      NoNewPrivileges = true;
      # Mount isolation in a user service implicitly creates a user namespace.
      # Host root is then unmapped (65534), breaking SO_PEERCRED checks for both
      # the broker and its passed worker fd. Keep host credential semantics.
      RestrictAddressFamilies = [ "AF_UNIX" ];
      LimitCORE = 0;
    };
  };
  systemd.services = lib.mkIf enabled {
    unicorn-fingerprint-display = {
      description = "Pass fingerprint display requests to the active user's hint";
      requires = [ "unicorn-fingerprint-display.socket" ];
      after = [ "unicorn-fingerprint-display.socket" ];
      serviceConfig = {
        ExecStart = "${packages.fingerprint-runtime}/bin/unicorn-fingerprint-display-broker";
        NoNewPrivileges = true;
        CapabilityBoundingSet = "";
        ProtectSystem = "strict";
        ProtectHome = true;
        PrivateTmp = true;
        RestrictAddressFamilies = [ "AF_UNIX" ];
        LimitCORE = 0;
      };
    };
    qsee-supplicant = {
      description = "QSEECOM filesystem listeners for Unicorn";
      serviceConfig = privileged // {
        ExecStart = "${packages.qsee-supplicant}/bin/qsee-supplicant --state-dir /var/lib/unicorn-fingerprint";
        StateDirectory = "unicorn-fingerprint";
        StateDirectoryMode = "0700";
      };
    };
    unicorn-fingerprint-select = {
      description = "Select the Unicorn TA using actual sensor responses";
      requires = [ "qsee-supplicant.service" ];
      bindsTo = [ "qsee-supplicant.service" ];
      after = [ "qsee-supplicant.service" ];
      unitConfig = {
        StartLimitIntervalSec = 120;
        StartLimitBurst = 3;
      };
      environment = {
        # Full startup qualification invokes the same native DSP path as workers.
        ADSP_LIBRARY_PATH = "${packages.fingerprint-dsp-firmware}/lib/firmware/qcom/sm8475/unicorn/cdsp";
        LD_LIBRARY_PATH = "${packages.fingerprint-runtime}/lib:${lib.getLib packages.fingerprint-runtime.fastrpc}/lib";
      };
      serviceConfig = privileged // {
        ExecStart = "${packages.fingerprint-runtime}/bin/unicorn-fingerprint-select";
        RuntimeDirectory = "unicorn-fingerprint";
        RuntimeDirectoryMode = "0700";
        StateDirectory = "unicorn-fingerprint-selector";
        StateDirectoryMode = "0700";
        TimeoutStartSec = 90;
        KillMode = "control-group";
        RestartSec = 5;
        # Missing CDSP or an unqualified TA needs intervention,
        # not an endless loop of secure-app loads and sensor resets.
        RestartPreventExitStatus = [ 78 ];
      };
    };
    fprintd = {
      requires = [ "unicorn-fingerprint-select.service" ];
      bindsTo = [ "unicorn-fingerprint-select.service" ];
      after = [ "unicorn-fingerprint-select.service" ];
      serviceConfig = {
        # Native workers attach through /dev/teeN; only the separate loader
        # service receives permission to load trusted applications.
        # An empty list emits no directive; an empty string resets the set.
        CapabilityBoundingSet = lib.mkForce "";
        # Upstream fprintd uses a device allowlist. TEE majors are dynamic;
        # privileged loading still requires CAP_SYS_ADMIN, absent here.
        DeviceAllow = [
          "char-tee rw"
          "char-input r"
          "/dev/unicorn-fingerprint rw"
          "/dev/unicorn-fingerprint-illumination rw"
          # libadsprpc allocates coherent shared buffers through its default
          # ADSP domain even when the remote URI selects CDSP. Without this
          # allocator fd, CDSP process-shell loading fails before proxy open.
          "/dev/fastrpc-adsp rw"
          "/dev/fastrpc-cdsp rw"
          "/dev/fastrpc-cdsp-secure rw"
          "/dev/dma_heap/system rw"
        ];
        NoNewPrivileges = true;
        LimitCORE = 0;
      };
    };
  };
}
