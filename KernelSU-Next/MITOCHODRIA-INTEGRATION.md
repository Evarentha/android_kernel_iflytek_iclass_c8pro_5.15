# Mitochodria KernelSU-Next Integration

Upstream: https://github.com/KernelSU-Next/KernelSU-Next
Commit: `cd739c78802333455391df973db17d9f28328b83` (`legacy`).
Source archive SHA256: `5bcb584a8d36735159454faaa3e22a2b73a0b1c293ab14afb58027a4ee3de9a3`.

Built in with manual hooks; source includes both ARM64 and compat exec paths.
Manager candidate: official v3.4.0 (API 26 minimum), UAPI/signature validation
recorded in the parent project integration records.

Local adjustments: build-time automatic source editing disabled; fixed version
metadata; 5.15 LSM signatures follow the actual local headers; Android 9 init
environment scan includes env[0]. `CONFIG_KSU_MITOCHODRIA_COMPAT` selects the
Android 9 compatibility profile; it does not install SELinux
status/context fops hiding hooks or enable AVC spoofing by default. Root still
requires normal manager/allowlist authorization (KSU_DEBUG disabled).

This functional profile also selects the matching LSM signatures, fscrypt-aware
allowlist persistence, bounded manager discovery retries and static adbd
integration. It is not a diagnostic logging switch.

This snapshot does not claim to resolve pre-existing kernel text corruption.
