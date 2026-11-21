"""Optional Windows signing: off by default, loud when set up and failing, and always before zips and hashes."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

import windows_signing

ROOT = Path(__file__).resolve().parents[1]
UNSET = {"MU_SIGN_METADATA": "", "MU_SIGN_DLIB": "", "MU_SIGNTOOL": ""}


class FakeSigntool:
    """Stands in for signtool: 'sign' appends a marker to each file, 'verify' succeeds."""

    def __init__(self, sign_exits=(0,), verify_exit=0, before_sign=None):
        self.calls, self.sign_exits, self.verify_exit, self.before_sign = [], list(sign_exits), verify_exit, before_sign

    def __call__(self, command, timeout=None):
        self.calls.append(command)
        if command[1] == "verify":
            return subprocess.CompletedProcess(command, self.verify_exit)
        if self.before_sign:
            self.before_sign()
        code = self.sign_exits.pop(0) if self.sign_exits else 0
        if code == 0:
            for name in command[command.index("/dmdf") + 2:]:
                with open(name, "ab") as f:
                    f.write(b"SIGNED")
        return subprocess.CompletedProcess(command, code)


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, True)
        for name in ("metadata.json", "Azure.CodeSigning.Dlib.dll", "signtool.exe"):
            (self.dir / name).write_bytes(b"x")
        self.env = {"MU_SIGN_METADATA": str(self.dir / "metadata.json"),
                    "MU_SIGN_DLIB": str(self.dir / "Azure.CodeSigning.Dlib.dll"),
                    "MU_SIGNTOOL": str(self.dir / "signtool.exe")}

    def test_skipped_when_unset(self):
        fake = FakeSigntool()
        out = io.StringIO()
        with patch.object(windows_signing, "_run", fake), contextlib.redirect_stdout(out):
            self.assertFalse(windows_signing.sign_files([self.dir / "a.exe"], env={}))
        self.assertEqual(out.getvalue().strip(), "signing skipped: MU_SIGN_METADATA not set")
        self.assertEqual(fake.calls, [])

    def test_half_setup_fails(self):
        with self.assertRaises(SystemExit):
            windows_signing.sign_files([], env={"MU_SIGN_METADATA": self.env["MU_SIGN_METADATA"]})
        with self.assertRaises(SystemExit):
            windows_signing.sign_files([], env={"MU_SIGN_DLIB": self.env["MU_SIGN_DLIB"]})
        with self.assertRaises(SystemExit):
            windows_signing.sign_files([], env=dict(self.env, MU_SIGN_DLIB=str(self.dir / "missing.dll")))

    def test_command_line(self):
        self.assertEqual(
            windows_signing.sign_command("signtool.exe", "dlib.dll", "meta.json", [Path("a.exe"), Path("b.dll")]),
            ["signtool.exe", "sign", "/v", "/fd", "SHA256", "/tr", "http://timestamp.acs.microsoft.com",
             "/td", "SHA256", "/dlib", "dlib.dll", "/dmdf", "meta.json", "a.exe", "b.dll"])

    def test_signs_then_verifies(self):
        exe = self.dir / "melee_source.exe"
        exe.write_bytes(b"MZ")
        fake = FakeSigntool(sign_exits=[1, 0])   # one refused request, then success
        with patch.object(windows_signing, "_run", fake), contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(windows_signing.sign_files([exe], env=self.env))
        self.assertEqual([c[1] for c in fake.calls], ["sign", "sign", "verify"])
        self.assertEqual(fake.calls[-1], [self.env["MU_SIGNTOOL"], "verify", "/pa", str(exe)])
        self.assertEqual(exe.read_bytes(), b"MZSIGNED")

    def test_failure_is_loud(self):
        exe = self.dir / "melee_source.exe"
        exe.write_bytes(b"MZ")
        for fake in (FakeSigntool(sign_exits=[1] * windows_signing.ATTEMPTS), FakeSigntool(verify_exit=1)):
            with patch.object(windows_signing, "_run", fake), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit):
                    windows_signing.sign_files([exe], env=self.env)

    def test_only_own_binaries(self):
        for name in ("melee_source.exe", "melee_game.dll", "MeleeUnlockedLauncher.exe", "sl.dlss.dll",
                     "libxess.dll", "vcruntime140.dll", "onnxruntime.dll"):
            (self.dir / name).write_bytes(b"MZ")
        self.assertEqual([p.name for p in windows_signing.own_binaries(self.dir)],
                         ["MeleeUnlockedLauncher.exe", "melee_source.exe", "melee_game.dll"])

    def test_source_port_package_signs_before_zipping(self):
        import package_source_port
        host, game, base, out = (self.dir / n for n in ("host", "game", "base", "out/MU-SourcePort"))
        for folder, names in ((host, package_source_port.HOST_FILES), (game, package_source_port.GAME_FILES)):
            folder.mkdir(parents=True)
            for name in names:
                (folder / name).write_bytes(b"MZ")
        for rel in ("Sys/GC/dsp_coef.bin", "shadercache/recipes.bin", "licenses/COPYING.txt",
                    "ui_sources/a.png", "TrainingMods.md"):
            (base / rel).parent.mkdir(parents=True, exist_ok=True)
            (base / rel).write_bytes(b"data")
        archive = out.with_suffix(".zip")
        fake = FakeSigntool(before_sign=lambda: self.assertFalse(archive.exists(), "zipped before signing"))
        argv = ["package_source_port.py", "--out", str(out), "--host-dir", str(host), "--game-dir", str(game),
                "--from-package", str(base), "--zip"]
        with patch.object(windows_signing, "_run", fake), patch.object(sys, "argv", argv), \
                patch.dict(os.environ, self.env), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(package_source_port.main(), 0)
        with zipfile.ZipFile(archive) as z:
            for name in ("MeleeUnlockedLauncher.exe", "melee_source.exe", "melee_game.dll"):
                self.assertEqual(z.read(f"{out.name}/{name}"), b"MZSIGNED", name)
            self.assertEqual(z.read(f"{out.name}/sl.dlss.dll"), b"MZ")   # third-party: untouched

    def test_release_binaries_skipped_when_unset(self):
        fake = FakeSigntool()
        with patch.object(windows_signing, "_run", fake), patch.dict(os.environ, UNSET), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(windows_signing.sign_release_binaries(self.dir))
        self.assertEqual(fake.calls, [])

    def test_release_package_signs_before_zipping_and_hashing(self):
        text = (ROOT / "tools/package_release.py").read_text(encoding="utf-8")
        sign = text.index("windows_signing.sign_release_binaries(folder)")
        # After the last program is copied in, before the compat zip, compat.sha256 and the main zip.
        self.assertGreater(sign, text.index('shutil.copy2(launcher, folder / "MeleeUnlockedLauncher.exe")'))
        self.assertGreater(sign, text.index('folder / "melee_source_compat.exe"'))
        self.assertLess(sign, text.index("compat_zip = args.out"))
        self.assertLess(sign, text.index('(folder / "compat.sha256")'))
        self.assertLess(sign, text.index('zip_folder(args.out / f"{name}-win64.zip")'))


if __name__ == "__main__":
    unittest.main()
