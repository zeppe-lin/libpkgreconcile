#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

"""Black-box tests for the installed-shape rejmerge executable."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import pwd
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest


if len(sys.argv) != 2:
    raise RuntimeError("usage: test_cli.py /path/to/rejmerge")
BINARY = Path(sys.argv[1]).resolve()
del sys.argv[1]


class RejmergeCliTests(unittest.TestCase):
    """Exercise option parsing, dry-run safety, and external-tool removal."""

    binary = BINARY

    def setUp(self) -> None:
        self.tempdir = Path(tempfile.mkdtemp(prefix="rejmerge-cli-test."))
        self.rejected = self.tempdir / "var/lib/pkg/rejected"
        self.rejected.mkdir(parents=True)

    def tearDown(self) -> None:
        shutil.rmtree(self.tempdir, ignore_errors=True)

    def write_pair(self, relative: str, old: bytes, new: bytes) -> None:
        installed = self.tempdir / relative
        rejected = self.rejected / relative
        installed.parent.mkdir(parents=True, exist_ok=True)
        rejected.parent.mkdir(parents=True, exist_ok=True)
        installed.write_bytes(old)
        rejected.write_bytes(new)

    def run_cli(
        self,
        *arguments: str,
        input_text: str = "",
        env: dict[str, str] | None = None,
        demote: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        command = [str(self.binary), *arguments]
        child_env = os.environ.copy()
        if env is not None:
            child_env.update(env)

        preexec_fn = None
        if demote and os.geteuid() == 0:
            nobody = pwd.getpwnam("nobody")

            def drop_privileges() -> None:
                os.setgroups([])
                os.setgid(nobody.pw_gid)
                os.setuid(nobody.pw_uid)

            preexec_fn = drop_privileges

        return subprocess.run(
            command,
            input=input_text,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=child_env,
            preexec_fn=preexec_fn,
            check=False,
        )

    def tree_digest(self) -> str:
        digest = hashlib.sha256()
        for path in sorted(self.tempdir.rglob("*")):
            relative = path.relative_to(self.tempdir).as_posix().encode()
            digest.update(relative)
            metadata = path.lstat()
            digest.update(str(stat.S_IMODE(metadata.st_mode)).encode())
            if path.is_symlink():
                digest.update(os.readlink(path).encode())
            elif path.is_file():
                digest.update(path.read_bytes())
        return digest.hexdigest()

    def test_help(self) -> None:
        result = self.run_cli("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Usage: rejmerge", result.stdout)
        self.assertNotIn("--config", result.stdout)

    def test_version(self) -> None:
        result = self.run_cli("--version")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"^rejmerge 0\.1\.0\n$")

    def test_unknown_config_option_is_rejected(self) -> None:
        result = self.run_cli("--config", "/tmp/rejmerge.conf")
        self.assertNotEqual(result.returncode, 0)

    def test_unexpected_operand_is_rejected(self) -> None:
        result = self.run_cli("operand")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected operand", result.stderr)

    def test_missing_rejected_directory_is_error(self) -> None:
        shutil.rmtree(self.rejected)
        result = self.run_cli("--dry-run", "--root", str(self.tempdir))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("staging directory not found", result.stderr)

    def test_empty_tree(self) -> None:
        result = self.run_cli("--dry-run", "--root", str(self.tempdir))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "Nothing to reconcile\n")

    def test_dry_run_is_bitwise_immutable(self) -> None:
        self.write_pair("etc/a.conf", b"old\n", b"new\n")
        os.chmod(self.tempdir / "etc/a.conf", 0o640)
        os.chmod(self.rejected / "etc/a.conf", 0o600)
        before = self.tree_digest()
        result = self.run_cli("--dry-run", "--root", str(self.tempdir))
        after = self.tree_digest()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, after)
        self.assertIn("-old\n+new\n", result.stdout)

    def test_internal_diff_does_not_require_path(self) -> None:
        self.write_pair("etc/a.conf", b"old\n", b"new\n")
        result = self.run_cli(
            "--dry-run",
            "--root",
            str(self.tempdir),
            env={"PATH": ""},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-old\n+new\n", result.stdout)

    def test_binary_dry_run(self) -> None:
        self.write_pair("etc/a.bin", b"a\0b", b"a\0c")
        result = self.run_cli("--dry-run", "--root", str(self.tempdir))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Binary files ", result.stdout)

    def test_path_with_spaces(self) -> None:
        self.write_pair("etc/name with spaces", b"old\n", b"new\n")
        result = self.run_cli("--dry-run", "--root", str(self.tempdir))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("name with spaces", result.stdout)

    def test_non_root_dry_run_allowed(self) -> None:
        if os.geteuid() != 0:
            self.skipTest("already running without root privileges")
        self.write_pair("etc/a.conf", b"old\n", b"new\n")
        for path in [self.tempdir, self.tempdir / "etc", self.rejected,
                     self.rejected / "etc"]:
            os.chmod(path, 0o755)
        os.chmod(self.tempdir / "etc/a.conf", 0o644)
        os.chmod(self.rejected / "etc/a.conf", 0o644)
        result = self.run_cli(
            "--dry-run", "--root", str(self.tempdir), demote=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_non_root_mutation_rejected(self) -> None:
        if os.geteuid() != 0:
            result = self.run_cli("--root", str(self.tempdir))
        else:
            os.chmod(self.tempdir, 0o755)
            os.chmod(self.tempdir / "var", 0o755)
            os.chmod(self.tempdir / "var/lib", 0o755)
            os.chmod(self.tempdir / "var/lib/pkg", 0o755)
            os.chmod(self.rejected, 0o755)
            result = self.run_cli("--root", str(self.tempdir), demote=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("only root", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
