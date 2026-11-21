"""Optional Authenticode signing of the release's own Windows programs (Azure Trusted Signing).

The packagers call sign_release_binaries(folder) after every program is in the release folder and
before anything is zipped or hashed: a signature changes the file's bytes, so a zip or a SHA-256
made earlier would describe the unsigned file. Setup: docs/windows-signing.md.

Nothing happens unless MU_SIGN_METADATA is set:
    MU_SIGN_METADATA  the Trusted Signing metadata.json (Endpoint, CodeSigningAccountName,
                      CertificateProfileName)
    MU_SIGN_DLIB      Azure.CodeSigning.Dlib.dll from the Microsoft.Trusted.Signing.Client package
    MU_SIGNTOOL       signtool.exe (optional: PATH, then the newest Windows 10/11 SDK)
With them set, a failed signature or verification stops the package: no unsigned program is
shipped as signed.
"""
import os
import shutil
import subprocess
from pathlib import Path

TIMESTAMP = "http://timestamp.acs.microsoft.com"
# Programs this project builds. The NVIDIA, Intel, AMD, Microsoft and other third-party files beside
# them keep their vendors' signatures; several are also checked by SHA-256 against the vendor's build
# (host/updater.cpp), which a second signature would break.
OWN_BINARIES = ("MeleeUnlockedLauncher.exe", "melee_port.exe", "melee_port_compat.exe",
                "melee_port_playback.exe", "melee_source.exe", "melee_source_compat.exe",
                "melee_game.dll", "melee_game_compat.dll")
ATTEMPTS = 3   # the signing service or the timestamp server now and then refuses one request

_run = subprocess.run   # tests replace this


def own_binaries(folder):
    folder = Path(folder)
    return [folder / name for name in OWN_BINARIES if (folder / name).is_file()]


def find_signtool(env):
    explicit = env.get("MU_SIGNTOOL")
    if explicit:
        return explicit
    found = shutil.which("signtool")
    if found:
        return found
    kits = Path(env.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    candidates = sorted(kits.glob("10.*/x64/signtool.exe"),
                        key=lambda p: tuple(int(part) for part in p.parts[-3].split(".") if part.isdigit()))
    return str(candidates[-1]) if candidates else None


def sign_command(signtool, dlib, metadata, files):
    return [str(signtool), "sign", "/v", "/fd", "SHA256", "/tr", TIMESTAMP, "/td", "SHA256",
            "/dlib", str(dlib), "/dmdf", str(metadata)] + [str(f) for f in files]


def verify_command(signtool, files):
    return [str(signtool), "verify", "/pa"] + [str(f) for f in files]


def settings(env=None):
    """(signtool, dlib, metadata), or None when signing is not set up. Half a setup is an error."""
    env = os.environ if env is None else env
    metadata, dlib = env.get("MU_SIGN_METADATA"), env.get("MU_SIGN_DLIB")
    if not metadata and not dlib:
        return None
    if not metadata or not dlib:
        raise SystemExit("signing: set both MU_SIGN_METADATA and MU_SIGN_DLIB, or neither")
    for name, path in (("MU_SIGN_METADATA", metadata), ("MU_SIGN_DLIB", dlib)):
        if not Path(path).is_file():
            raise SystemExit(f"signing: {name} is not a file: {path}")
    signtool = find_signtool(env)
    if not signtool or not Path(signtool).is_file():
        raise SystemExit(f"signing: signtool.exe not found ({signtool or 'set MU_SIGNTOOL'})")
    return signtool, dlib, metadata


def sign_files(files, env=None):
    """Signs and verifies `files`. False (and one line) when signing is not set up."""
    setup = settings(env)
    if setup is None:
        print("signing skipped: MU_SIGN_METADATA not set")
        return False
    signtool, dlib, metadata = setup
    files = [Path(f) for f in files]
    if not files:
        raise SystemExit("signing: no programs to sign")
    for attempt in range(1, ATTEMPTS + 1):
        result = _run(sign_command(signtool, dlib, metadata, files), timeout=600)
        if result.returncode == 0:
            break
        if attempt == ATTEMPTS:
            raise SystemExit(f"signing failed (signtool exit {result.returncode}); no package was made")
        print(f"signing: attempt {attempt} failed (exit {result.returncode}), trying again")
    result = _run(verify_command(signtool, files), timeout=600)
    if result.returncode != 0:
        raise SystemExit(f"signing: signtool verify failed (exit {result.returncode}); no package was made")
    print("signed: " + ", ".join(f.name for f in files))
    return True


def sign_release_binaries(folder, env=None):
    """Signs this project's programs in a release folder (OWN_BINARIES that are present)."""
    return sign_files(own_binaries(folder), env)
