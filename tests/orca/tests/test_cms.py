import os
import struct
import subprocess
import tempfile
import unittest
from unittest import mock
from orca import cms, journal

HERE = os.path.dirname(os.path.abspath(__file__))
CI_DIR = os.path.abspath(os.path.join(HERE, "..", "..", "..", "tools", "ci"))
PUBLIC_KEY = os.path.join(CI_DIR, "contoso.com.pem")
PRIVATE_KEY = os.path.join(CI_DIR, "contoso.com.key")


def openssl_available():
    return cms.find_openssl() is not None and os.path.exists(PRIVATE_KEY)


class TestChain(unittest.TestCase):
    """The backend chain, with the backends themselves stubbed out."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.src = os.path.join(self.dir.name, "in.p7b")
        self.dst = os.path.join(self.dir.name, "out")
        with open(self.src, "wb") as f:
            f.write(b"ciphertext")

    def patch(self, probes, backends):
        for table, values in ((cms._PROBES, probes), (cms._BACKENDS, backends)):
            patcher = mock.patch.dict(table, values)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_falls_through_to_the_next_backend(self):
        def fails(src, dst, key):
            raise RuntimeError("wrong key")

        def works(src, dst, key):
            with open(dst, "wb") as f:
                f.write(b"plaintext")

        self.patch({"cng": lambda key: None, "libcrypto": lambda key: None},
                   {"cng": fails, "libcrypto": works})
        self.assertEqual(cms.decrypt(self.src, self.dst), "libcrypto")

    def test_unavailable_backend_is_skipped_without_running(self):
        ran = []
        self.patch({"cng": lambda key: "no certificate store here",
                    "libcrypto": lambda key: None},
                   {"cng": lambda *a: ran.append("cng"),
                    "libcrypto": lambda src, dst, key: open(dst, "wb").close()})
        cms.decrypt(self.src, self.dst)
        self.assertEqual(ran, [])

    def test_failure_names_every_backend_and_its_reason(self):
        def fails(src, dst, key):
            raise RuntimeError("wrong key")

        self.patch({"cng": lambda key: "not on Windows", "libcrypto": lambda key: None,
                    "openssl": lambda key: "not found"},
                   {"libcrypto": fails})
        with self.assertRaises(RuntimeError) as caught:
            cms.decrypt(self.src, self.dst)

        message = str(caught.exception)
        self.assertIn("cng: not on Windows", message)
        self.assertIn("libcrypto: wrong key", message)
        self.assertIn("openssl: not found", message)

    def test_partial_output_is_removed_before_the_next_backend(self):
        def half_writes_then_fails(src, dst, key):
            with open(dst, "wb") as f:
                f.write(b"truncated")
            raise RuntimeError("stream ended early")

        self.patch({"cng": lambda key: None, "libcrypto": lambda key: "unavailable",
                    "openssl": lambda key: "unavailable"},
                   {"cng": half_writes_then_fails})
        with self.assertRaises(RuntimeError):
            cms.decrypt(self.src, self.dst)
        self.assertFalse(os.path.exists(self.dst))

    def test_pinned_backend_is_the_only_one_tried(self):
        ran = []
        self.patch({"cng": lambda key: None, "openssl": lambda key: None},
                   {"cng": lambda *a: ran.append("cng"),
                    "openssl": lambda src, dst, key: ran.append("openssl")})
        cms.decrypt(self.src, self.dst, backend="openssl")
        self.assertEqual(ran, ["openssl"])

    def test_unknown_pinned_backend(self):
        with self.assertRaises(RuntimeError) as caught:
            cms.decrypt(self.src, self.dst, backend="gpg")
        self.assertIn("unknown backend 'gpg'", str(caught.exception))


class TestProbes(unittest.TestCase):
    def test_openssl_needs_a_key(self):
        self.assertIn("private key", cms._probe_openssl(None))

    def test_openssl_reports_a_missing_key_file(self):
        self.assertIn("does not exist", cms._probe_openssl("no-such-file.key"))

    @unittest.skipUnless(openssl_available(), "openssl or the test key is missing")
    def test_openssl_is_listed_when_a_key_is_given(self):
        self.assertIn("openssl", cms.backends(PRIVATE_KEY))

    def test_libcrypto_needs_a_key(self):
        self.assertIn("private key", cms._probe_libcrypto(None))

    def test_cng_declines_a_pem_key(self):
        # By design: CryptoAPI reads the certificate store, so a key file belongs elsewhere.
        self.assertIn("PEM", cms._probe_cng("some.key"))

    @unittest.skipIf(os.name == "nt", "posix-only expectation")
    def test_cng_is_windows_only(self):
        self.assertEqual(cms._probe_cng(None), "Windows only")

    def test_oversized_input_says_so_instead_of_blaming_the_key(self):
        # Near 2 GiB every backend fails with an opaque malloc/ASN.1 error; the aggregated
        # message must name the real cause.
        with tempfile.TemporaryDirectory() as d:
            huge = os.path.join(d, "huge.p7b")
            with open(huge, "wb") as f:
                f.write(b"x")

            with mock.patch.object(cms, "CMS_SIZE_CEILING", 0):
                with self.assertRaises(RuntimeError) as caught:
                    cms.decrypt(huge, os.path.join(d, "out"), None)

        # It must point at the backend that can actually do it, not just say "too big".
        self.assertIn("no size limit", str(caught.exception))
        self.assertIn("cng", str(caught.exception))

    def test_a_full_destination_says_so_instead_of_blaming_the_key(self):
        # OpenSSL writes the plaintext itself, so ENOSPC comes back as 'CMS_decrypt failed'.
        # Without the note the reader goes looking for a bad key.
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "big.p7b")
            with open(src, "wb") as f:
                f.write(b"x" * 4096)

            full = mock.Mock(total=1 << 30, used=1 << 30, free=0)
            with mock.patch.object(cms.shutil, "disk_usage", lambda path: full):
                with self.assertRaises(RuntimeError) as caught:
                    cms.decrypt(src, os.path.join(d, "out"), None)

        message = str(caught.exception)
        self.assertIn("free", message)
        self.assertIn("rather than the key", message)
        self.assertIn("--work-dir", message)

    def test_room_to_spare_gets_no_space_note(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "small.p7b")
            with open(src, "wb") as f:
                f.write(b"x")

            with self.assertRaises(RuntimeError) as caught:
                cms.decrypt(src, os.path.join(d, "out"), None)

        self.assertNotIn("free and the message", str(caught.exception))

    def test_an_unreachable_destination_adds_no_note(self):
        # Same failure shape as the ceiling helper: a path that cannot be measured says nothing.
        self.assertEqual(cms._free_space_note(__file__, "/nonexistent-volume/x/y/out"), "")

    def test_normal_sized_input_gets_no_ceiling_note(self):
        with tempfile.TemporaryDirectory() as d:
            small = os.path.join(d, "small.p7b")
            with open(small, "wb") as f:
                f.write(b"x")

            with self.assertRaises(RuntimeError) as caught:
                cms.decrypt(small, os.path.join(d, "out"), None)

        self.assertNotIn("no size limit", str(caught.exception))

    def test_key_needing_backends_are_not_listed_without_a_key(self):
        # cng takes its key from the certificate store, so it may appear here; the two that
        # read a PEM file must not.
        listed = cms.backends(None)
        self.assertNotIn("openssl", listed)
        self.assertNotIn("libcrypto", listed)


@unittest.skipUnless(openssl_available(), "openssl or the test key is missing")
class TestRoundTrip(unittest.TestCase):
    """Encrypt with the CI certificate, decrypt with the CI key, replay the journal.

    Covers the real '.p7b' shape (CMS over a JRNL stream) without needing an ORC run.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def path(self, name):
        return os.path.join(self.dir.name, name)

    def test_p7b_to_archive(self):
        payload = os.urandom(200000)
        stream = (struct.pack("<4sI", journal.MAGIC, journal.VERSION)
                  + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(payload)) + payload
                  + struct.pack("<4sIQ", journal.OP_CLOSE, 0, 0))

        plain = self.path("stream.7zs")
        with open(plain, "wb") as f:
            f.write(stream)

        encrypted = self.path("archive.p7b")
        result = subprocess.run(
            [cms.find_openssl(), "cms", "-encrypt", "-binary", "-outform", "DER",
             "-in", plain, "-out", encrypted, PUBLIC_KEY],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

        decrypted = self.path("stream-back.7zs")
        self.assertEqual(cms.decrypt(encrypted, decrypted, PRIVATE_KEY), "openssl")
        with open(decrypted, "rb") as f:
            self.assertEqual(f.read(), stream)

        archive = self.path("archive.7z")
        journal.replay(decrypted, archive)
        with open(archive, "rb") as f:
            self.assertEqual(f.read(), payload)

    @unittest.skipUnless(cms.find_libcrypto(), "no libcrypto with CMS support")
    def test_libcrypto_matches_openssl_byte_for_byte(self):
        payload = os.urandom(120000)
        plain = self.path("plain.bin")
        with open(plain, "wb") as f:
            f.write(payload)

        encrypted = self.path("archive.p7b")
        subprocess.run([cms.find_openssl(), "cms", "-encrypt", "-binary", "-outform", "DER",
                        "-in", plain, "-out", encrypted, PUBLIC_KEY],
                       capture_output=True, text=True, check=True)

        results = {}
        for backend in ("libcrypto", "openssl"):
            out = self.path(f"out-{backend}.bin")
            self.assertEqual(cms.decrypt(encrypted, out, PRIVATE_KEY, backend), backend)
            with open(out, "rb") as f:
                results[backend] = f.read()

        self.assertEqual(results["libcrypto"], payload)
        self.assertEqual(results["libcrypto"], results["openssl"])

    def test_wrong_key_fails_closed(self):
        # A wrong key is NOT guaranteed to raise, so this asserts the guarantee that does hold:
        # the plaintext never comes back. An ORC '.p7b' wraps its content key with RSA PKCS#1
        # v1.5 over an AES-CBC stream, which carries no integrity check anywhere, and OpenSSL
        # answers an RSA padding failure with a RANDOM session key instead of an error (the
        # Bleichenbacher countermeasure - reporting the failure is the padding oracle). The
        # content then decrypts to garbage, and whenever that garbage happens to end in valid
        # PKCS#7 padding - about once in 256 attempts - it is returned as a success.
        #
        # So assertRaises here is a lottery: it failed roughly once in 25 runs, via the
        # libcrypto backend, returning 15 bytes of garbage for a 6-byte payload (a trailing
        # 0x01 read as a one-byte pad). Do not put it back. Both branches below are checked.
        payload = b"secret"
        plain = self.path("plain.bin")
        with open(plain, "wb") as f:
            f.write(payload)

        encrypted = self.path("archive.p7b")
        subprocess.run([cms.find_openssl(), "cms", "-encrypt", "-binary", "-outform", "DER",
                        "-in", plain, "-out", encrypted, PUBLIC_KEY],
                       capture_output=True, text=True, check=True)

        other_key = self.path("other.key")
        subprocess.run([cms.find_openssl(), "genrsa", "-out", other_key, "2048"],
                       capture_output=True, text=True, check=True)

        out = self.path("out.bin")
        try:
            cms.decrypt(encrypted, out, other_key)
        except RuntimeError as error:
            # The usual path: every backend refused, the message names them, and no output is
            # left behind for the next layer to mistake for a decrypted archive.
            self.assertIn("openssl", str(error))
            self.assertFalse(os.path.exists(out))
        else:
            # The rare path: well-padded garbage was returned. It must not be the plaintext.
            with open(out, "rb") as f:
                self.assertNotEqual(f.read(), payload)


if __name__ == "__main__":
    unittest.main()
