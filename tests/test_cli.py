"""Exercise the actual executable in pipes and a pseudo-terminal."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.log = Path(self.temp.name) / "notification"
        stub = Path(self.temp.name) / "notify-send"
        stub.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$NOTIFICATION_LOG"\nexit 1\n')
        stub.chmod(0o755)
        self.env = dict(os.environ, TERM="xterm-256color", PATH=self.temp.name,
                        NOTIFICATION_LOG=str(self.log))
        self.env.pop("NO_COLOR", None)

    def start_terminal(self, duration="2s", columns=80, rows=24):
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        original = termios.tcgetattr(slave)
        process = subprocess.Popen([BINARY, duration], stdin=slave, stdout=slave,
                                   stderr=slave, env=self.env)
        def cleanup():
            if process.poll() is None:
                process.kill()
            process.wait()
        self.addCleanup(cleanup)
        return process, master, slave, original

    def read_for(self, master, duration):
        output = b""
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], max(0, deadline - time.monotonic()))
            if ready:
                output += os.read(master, 65536)
        return output

    def test_help_and_invalid_arguments(self):
        result = subprocess.run([BINARY, "--help"], capture_output=True, env=self.env)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"Space", result.stdout)
        for args in ([], ["0s"], ["1.5m"], ["-2h"], ["2s", "3m"], ["999999999999999999h"]):
            result = subprocess.run([BINARY, *args], capture_output=True, env=self.env)
            self.assertEqual(result.returncode, 2)
            self.assertIn(b"Usage:", result.stderr)

    def test_redirected_completion_and_notification_failure(self):
        start = time.monotonic()
        result = subprocess.run([BINARY, "1s"], capture_output=True, env=self.env, timeout=3)
        self.assertGreaterEqual(time.monotonic() - start, 0.95)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"countdown complete", result.stdout)
        self.assertNotIn(b"\x1b", result.stdout)
        self.assertNotIn(b"\a", result.stdout)
        deadline = time.monotonic() + 1
        while not self.log.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        notification = self.log.read_text()
        self.assertIn("suppress-sound:true", notification)
        self.assertIn("Timer complete", notification)

    def test_missing_notifier(self):
        self.env["PATH"] = "/nonexistent"
        result = subprocess.run([BINARY, "1s"], capture_output=True, env=self.env, timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")

    def test_pause_resume_completion_and_cleanup(self):
        process, master, slave, original = self.start_terminal("1s")
        output = self.read_for(master, 0.15)
        os.write(master, b" ")
        output += self.read_for(master, 1.2)
        self.assertIsNone(process.poll(), "Paused timer completed")
        self.assertIn(b"PAUSED", output)
        self.assertIn(b"\x1b[36m", output)
        self.assertIn(b" ### ", output)
        os.write(master, b" ")
        output += self.read_for(master, 1.1)
        self.assertEqual(process.wait(timeout=1), 0)
        self.assertIn(b"countdown complete", output)
        self.assertIn(b"\x1b[?25h\x1b[?1049l", output)
        self.assertEqual(termios.tcgetattr(slave), original)

    def test_escape_and_resize(self):
        process, master, slave, original = self.start_terminal("2h")
        output = self.read_for(master, 0.15)
        self.assertIn(b"NEOTIMER", output)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 8, 24, 0, 0))
        output = self.read_for(master, 0.15)
        self.assertIn(b"02:00:00", output)
        os.write(master, b"\x1b")
        output += self.read_for(master, 0.15)
        self.assertEqual(process.wait(timeout=1), 0)
        self.assertIn(b"cancelled", output)
        self.assertEqual(termios.tcgetattr(slave), original)
        self.assertFalse(self.log.exists())

    def test_interrupt_cleanup(self):
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT):
            process, master, slave, original = self.start_terminal("45m")
            self.read_for(master, 0.1)
            process.send_signal(sig)
            output = self.read_for(master, 0.1)
            self.assertEqual(process.wait(timeout=1), 128 + sig)
            self.assertIn(b"\x1b[?25h\x1b[?1049l", output)
            self.assertEqual(termios.tcgetattr(slave), original)

    def test_compact_transition_and_no_color(self):
        self.env["NO_COLOR"] = "1"
        process, master, _, _ = self.start_terminal("60s", columns=24, rows=8)
        output = self.read_for(master, 0.1)
        self.assertIn(b"01:00", output)
        output += self.read_for(master, 1.1)
        self.assertIn(b"59", output)
        self.assertNotIn(b"\x1b[36m", output)
        os.write(master, b"\x1b")
        process.wait(timeout=1)

    def test_tiny_terminal(self):
        process, master, _, _ = self.start_terminal("30s", columns=8, rows=1)
        output = self.read_for(master, 0.1)
        self.assertIn(b"30", output)
        self.assertNotIn(b"NEOTIMER", output)
        os.write(master, b"\x1b")
        process.wait(timeout=1)


if __name__ == "__main__":
    unittest.main()
