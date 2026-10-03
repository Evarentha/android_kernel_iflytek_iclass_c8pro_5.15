# C8Pro KernelSU-Next snapshot

Upstream: https://github.com/KernelSU-Next/KernelSU-Next
Commit: `cd739c78802333455391df973db17d9f28328b83` (`legacy`).
Source archive SHA256: `5bcb584a8d36735159454faaa3e22a2b73a0b1c293ab14afb58027a4ee3de9a3`.

Built in with manual hooks; source includes both ARM64 and compat exec paths.
Manager candidate: official v3.4.0 (API 26 minimum), UAPI/signature validation
recorded in the parent project integration records.

Local adjustments: build-time automatic source editing disabled; fixed version
metadata; 5.15 LSM signatures follow the actual local headers; Android 9 init
environment scan includes env[0]. C8Pro diagnostic mode does not install SELinux
status/context fops hiding hooks or enable AVC spoofing by default. Root still
requires normal manager/allowlist authorization (KSU_DEBUG disabled).

This snapshot does not claim to resolve pre-existing kernel text corruption.
