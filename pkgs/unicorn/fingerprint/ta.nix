{
  lib,
  runCommand,
  python3,
}:
runCommand "unicorn-fingerprint-ta-candidates"
  {
    nativeBuildInputs = [ python3 ];
    meta = {
      description = "Hash-checked OEM gfuni/gfuni6 candidates; neither is sensor-qualified";
      license = lib.licenses.unfree;
      platforms = lib.platforms.linux;
    };
  }
  ''
    python3 ${./assemble-ta.py} ${./inputs/ta-candidates.tar.xz} ${./inputs/manifest.json} "$out"
  ''
