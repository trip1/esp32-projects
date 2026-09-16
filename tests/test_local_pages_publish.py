import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).parents[1] / "scripts" / "publish_pages_local.py"
spec = importlib.util.spec_from_file_location("publish_pages_local", SCRIPT)
assert spec is not None and spec.loader is not None
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class LocalPagesPublisherTests(unittest.TestCase):
    def test_parses_only_two_part_github_remotes(self):
        self.assertEqual("trip1/esp32-projects", publisher.repository_name("git@github.com:trip1/esp32-projects.git"))
        self.assertEqual("trip1/esp32-projects", publisher.repository_name("https://github.com/trip1/esp32-projects.git"))
        self.assertEqual("trip1/esp32-projects", publisher.repository_name("ssh://git@github.com/trip1/esp32-projects.git"))
        rejected = (
            "https://example.com/trip1/esp32-projects.git",
            "https://evilgithub.com/trip1/esp32-projects.git",
            "https://github.com.evil.test/trip1/esp32-projects.git",
            "https://token@github.com/trip1/esp32-projects.git",
            "https://token:secret@evilgithub.com/trip1/esp32-projects.git",
            "/tmp/github.com/trip1/esp32-projects.git",
        )
        for remote in rejected:
            with self.subTest(remote=remote), self.assertRaises(RuntimeError) as error:
                publisher.repository_name(remote)
            self.assertNotIn("token", str(error.exception))
            self.assertNotIn("secret", str(error.exception))

    def test_copy_site_adds_deployment_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            destination = root / "publish"
            site.mkdir()
            destination.mkdir()
            (site / "index.html").write_text("ok")
            previous = getattr(publisher, "SITE")
            setattr(publisher, "SITE", site)
            try:
                publisher.copy_site(destination, "a" * 40, "3.6.0", "d" * 32)
            finally:
                setattr(publisher, "SITE", previous)
            self.assertEqual("ok", (destination / "index.html").read_text())
            self.assertEqual("a" * 40, (destination / "SOURCE_COMMIT").read_text().strip())
            self.assertTrue((destination / ".nojekyll").is_file())
            build_info = json.loads((destination / "BUILD_INFO.json").read_text())
            self.assertEqual("a" * 40, build_info["source_commit"])
            self.assertEqual("d" * 32, build_info["deployment_id"])
            self.assertEqual("3.6.0", build_info["portal_version"])
            index = next(item for item in build_info["files"] if item["path"] == "index.html")
            self.assertEqual(hashlib.sha256(b"ok").hexdigest(), index["sha256"])

    def test_copy_site_rejects_top_level_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            destination = root / "publish"
            site.mkdir()
            destination.mkdir()
            nested = site / "nested"
            nested.mkdir()
            (site / "target").write_text("data")
            (nested / "link").symlink_to("../target")
            previous = getattr(publisher, "SITE")
            setattr(publisher, "SITE", site)
            try:
                with self.assertRaises(RuntimeError):
                    publisher.copy_site(destination, "b" * 40, "3.6.0", "e" * 32)
            finally:
                setattr(publisher, "SITE", previous)

    def test_copy_site_rejects_git_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            destination = root / "publish"
            (site / "nested" / ".git").mkdir(parents=True)
            (site / "nested" / ".git" / "config").write_text("not allowed")
            destination.mkdir()
            previous = getattr(publisher, "SITE")
            setattr(publisher, "SITE", site)
            try:
                with self.assertRaises(RuntimeError):
                    publisher.copy_site(destination, "c" * 40, "3.6.0", "f" * 32)
            finally:
                setattr(publisher, "SITE", previous)

    def test_configure_pages_validates_https_and_checks_build_request(self):
        pages = json.dumps({
            "build_type": "legacy",
            "source": {"branch": "gh-pages", "path": "/"},
            "https_enforced": True,
            "html_url": "https://trip1.github.io/esp32-projects/",
        })
        with mock.patch.object(publisher, "output", return_value=pages), mock.patch.object(publisher.subprocess, "run") as run:
            self.assertEqual("https://trip1.github.io/esp32-projects/", publisher.configure_pages("trip1/esp32-projects"))
            self.assertEqual(2, run.call_count)
            self.assertTrue(run.call_args_list[0].kwargs["check"])
            self.assertTrue(run.call_args_list[1].kwargs["check"])

        insecure = json.loads(pages)
        insecure["https_enforced"] = False
        with mock.patch.object(publisher, "output", return_value=json.dumps(insecure)), mock.patch.object(publisher.subprocess, "run"):
            with self.assertRaises(RuntimeError):
                publisher.configure_pages("trip1/esp32-projects")

        failure = subprocess.CalledProcessError(1, ["gh", "api"])
        with mock.patch.object(publisher, "output", return_value=pages), mock.patch.object(
            publisher.subprocess, "run", side_effect=[subprocess.CompletedProcess([], 0), failure]
        ):
            with self.assertRaises(subprocess.CalledProcessError):
                publisher.configure_pages("trip1/esp32-projects")

    def test_served_files_exclude_git_metadata_and_nojekyll(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "index.html").write_text("ok")
            (root / ".nojekyll").write_text("")
            subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
            relative = [path.relative_to(root).as_posix() for path in publisher.served_files(root)]
            self.assertEqual(["index.html"], relative)

    def test_local_builder_cleans_outputs_before_every_firmware_build(self):
        source = SCRIPT.read_text()
        self.assertIn('[pio, "run", "-d", f"firmware/{project}", "-t", "clean"]', source)
        self.assertLess(source.index('"-t", "clean"'), source.index('run([pio, "run", "-d", f"firmware/{project}"])'))


if __name__ == "__main__":
    unittest.main()
