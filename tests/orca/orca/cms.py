"""CMS decryption for ORC '.p7b' results.

An ORC '.p7b' is CMS enveloped-data (DER): a content key RSA-wrapped to the recipient
certificate, wrapping a DES-EDE3-CBC or AES128-CBC stream. The plaintext is a JRNL journal,
not a '.7z' - see journal.py for that second layer.

Three interchangeable backends are probed in this order:

    cng        Windows CryptoAPI, recipient key taken from the certificate store (no key file)
    libcrypto  OpenSSL's shared library through ctypes, with a PEM key file
    openssl    the 'openssl' executable, with a PEM key file

The standard library cannot do this alone: it ships no symmetric cipher, and a pure-Python
AES/3DES would run at about 1 MB/s, which is hours on a GB-scale archive.
"""
import ctypes
import ctypes.util
import os
import shutil
import subprocess
import sys

BACKENDS = ("cng", "libcrypto", "openssl")

# CMS_BINARY: no text canonicalization, matching 'openssl cms -decrypt -binary'.
CMS_BINARY = 0x80

_OPENSSL_LOCATIONS = (
    r"C:\Program Files\Git\usr\bin\openssl.exe",
    r"C:\Program Files\Git\mingw64\bin\openssl.exe",
    r"C:\msys64\usr\bin\openssl.exe",
)


def find_openssl() -> str | None:
    found = shutil.which("openssl")
    if found:
        return found
    for candidate in _OPENSSL_LOCATIONS:
        if os.path.exists(candidate):
            return candidate
    return None


def backends(private_key: str | None = None) -> list[str]:
    """Backend names usable here, in probe order. A backend that needs a PEM key is reported
    as usable only when one is supplied, so the list answers 'what would decrypt() try'."""
    return [name for name in BACKENDS if _PROBES[name](private_key) is None]


def decrypt(src: str, dst: str, private_key: str | None = None,
            backend: str | None = None) -> str:
    """Decrypt the CMS blob at 'src' into 'dst'; returns the backend that succeeded.

    Backends are tried in order and every refusal is kept, so a total failure names what each
    one said: a wrong key reported as a bare 'no backend worked' is undebuggable remotely.
    """
    if backend is not None and backend not in BACKENDS:
        raise RuntimeError(f"cms: unknown backend '{backend}' (expected one of "
                           f"{', '.join(BACKENDS)})")

    failures = []
    for name in (BACKENDS if backend is None else (backend,)):
        reason = _PROBES[name](private_key)
        if reason:
            failures.append(f"{name}: {reason}")
            continue

        try:
            _BACKENDS[name](src, dst, private_key)
            return name
        except (RuntimeError, OSError) as error:
            # OSError too: a ctypes library that fails to load, or an unreadable file, is a
            # refusal like any other and must not abort the remaining backends.
            failures.append(f"{name}: {error}")
            # A half-written output would be mistaken for a decrypted archive by the next
            # layer, so drop it before falling through.
            if os.path.exists(dst):
                os.remove(dst)

    raise RuntimeError(f"cms: no backend could decrypt '{src}':\n  " + "\n  ".join(failures)
                       + _size_ceiling_note(src) + _free_space_note(src, dst))


def _size_ceiling_note(src: str) -> str:
    """Name the known ceiling when the failure is most likely that, not a wrong key.

    The key-file backends decode the whole structure in memory and fail near 2 GiB with an
    opaque malloc error. The streaming CryptoAPI backend has no such limit, so point at it.
    """
    try:
        size = os.path.getsize(src)
    except OSError:
        return ""

    if size < CMS_SIZE_CEILING:
        return ""

    return (f"\n  note: '{src}' is {size // (1 << 20)} MB. The key-file backends decode the "
            f"message in memory and fail near 2 GiB, so the size is the likely cause rather "
            f"than the key. Import the recipient certificate into the Windows 'MY' store and "
            f"rerun without --private-key: the 'cng' backend streams and has no size limit.")


def _free_space_note(src: str, dst: str) -> str:
    """Name a full destination volume, which no backend is able to report as one.

    OpenSSL writes the plaintext itself - through a BIO for the library backend, to '-out' for
    the executable - so there is no Python write to raise ENOSPC. The volume filling up comes
    back as 'CMS_decrypt failed' or 'Error decrypting CMS structure', which reads as a wrong
    key. The plaintext is within a rounding error of the ciphertext, so the source's size is
    what the free space has to be compared against.
    """
    target = os.path.dirname(os.path.abspath(dst))
    try:
        needed = os.path.getsize(src)
        free = shutil.disk_usage(target).free
    except OSError:
        return ""

    if free >= needed:
        return ""

    return (f"\n  note: '{target}' has {free // (1 << 20)} MB free and the message is "
            f"{needed // (1 << 20)} MB. The plaintext is written there, and a volume with no "
            f"room left surfaces as a decryption failure rather than a disk one, so the space "
            f"is the likely cause rather than the key. '--work-dir' puts it somewhere else.")


def _probe_openssl(private_key: str | None) -> str | None:
    if not private_key:
        return "needs a private key file (--private-key)"
    if not os.path.exists(private_key):
        return f"private key '{private_key}' does not exist"
    if not find_openssl():
        return "'openssl' not found on PATH or in the usual Git/msys locations"
    return None


def _decrypt_openssl(src: str, dst: str, private_key: str | None) -> None:
    openssl = find_openssl()
    result = subprocess.run(
        [openssl, "cms", "-decrypt", "-inform", "DER", "-binary",
         "-in", src, "-out", dst, "-inkey", private_key],
        capture_output=True, text=True)

    if result.returncode != 0:
        message = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"'{openssl} cms -decrypt' failed (code {result.returncode}): "
                           f"{message}")


_LIBCRYPTO = None


def _libcrypto_candidates() -> list[str]:
    if os.name == "nt":
        # CPython ships the OpenSSL its _ssl module links against; that is the one library we
        # can count on being present and ABI-compatible on a Windows host with no OpenSSL
        # installation of its own.
        names = ["libcrypto-3-x64.dll", "libcrypto-3.dll", "libcrypto-1_1-x64.dll",
                 "libcrypto-1_1.dll"]
        directories = [os.path.join(sys.base_prefix, "DLLs"), sys.base_prefix, ""]
        return [os.path.join(d, n) if d else n for d in directories for n in names]

    found = ctypes.util.find_library("crypto")
    return [found] if found else ["libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"]


def find_libcrypto() -> str | None:
    """Path or soname of a libcrypto that loads and exports the CMS API."""
    return _load_libcrypto()[1]


def _load_libcrypto():
    """(handle, name) for the first loadable libcrypto exporting CMS_decrypt, else (None, None).

    A build configured with 'no-cms' loads fine but has no CMS_decrypt, so the export is part
    of the availability test rather than something to discover mid-decryption. Memoized: the
    probe and the decryption both ask, and the prototypes are declared once.
    """
    global _LIBCRYPTO
    if _LIBCRYPTO is not None:
        return _LIBCRYPTO

    _LIBCRYPTO = (None, None)
    for candidate in _libcrypto_candidates():
        if os.path.dirname(candidate) and not os.path.exists(candidate):
            continue
        try:
            handle = ctypes.CDLL(candidate)
        except OSError:
            continue
        if hasattr(handle, "CMS_decrypt"):
            _declare_libcrypto(handle)
            _LIBCRYPTO = (handle, candidate)
            break

    return _LIBCRYPTO


def _declare_libcrypto(lib) -> None:
    pointer = ctypes.c_void_p
    lib.BIO_new_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.BIO_new_file.restype = pointer
    lib.BIO_free_all.argtypes = [pointer]
    lib.BIO_free_all.restype = None
    lib.d2i_CMS_bio.argtypes = [pointer, pointer]
    lib.d2i_CMS_bio.restype = pointer
    lib.CMS_ContentInfo_free.argtypes = [pointer]
    lib.CMS_ContentInfo_free.restype = None
    lib.PEM_read_bio_PrivateKey.argtypes = [pointer, pointer, pointer, pointer]
    lib.PEM_read_bio_PrivateKey.restype = pointer
    lib.EVP_PKEY_free.argtypes = [pointer]
    lib.EVP_PKEY_free.restype = None
    lib.CMS_decrypt.argtypes = [pointer, pointer, pointer, pointer, pointer, ctypes.c_uint]
    lib.CMS_decrypt.restype = ctypes.c_int
    lib.ERR_get_error.argtypes = []
    lib.ERR_get_error.restype = ctypes.c_ulong
    lib.ERR_error_string_n.argtypes = [ctypes.c_ulong, ctypes.c_char_p, ctypes.c_size_t]
    lib.ERR_error_string_n.restype = None


def _libcrypto_error(lib, fallback: str) -> str:
    """The most recent OpenSSL error, drained so it cannot leak into the next call."""
    messages = []
    while True:
        code = lib.ERR_get_error()
        if not code:
            break
        buffer = ctypes.create_string_buffer(256)
        lib.ERR_error_string_n(code, buffer, len(buffer))
        messages.append(buffer.value.decode("utf-8", "replace"))

    return "; ".join(messages) if messages else fallback


def _probe_libcrypto(private_key: str | None) -> str | None:
    if not private_key:
        return "needs a private key file (--private-key)"
    if not os.path.exists(private_key):
        return f"private key '{private_key}' does not exist"
    if not _load_libcrypto()[0]:
        return "no libcrypto with CMS support could be loaded"
    return None


def _decrypt_libcrypto(src: str, dst: str, private_key: str | None) -> None:
    lib, _ = _load_libcrypto()

    # BIO_new_file takes a byte path, so a non-ASCII path can fail here on Windows. That is a
    # refusal like any other and the chain falls through to the openssl executable.
    handles = []

    def opened(bio, what: str):
        if not bio:
            raise RuntimeError(_libcrypto_error(lib, f"could not open {what}"))
        handles.append(bio)
        return bio

    try:
        source = opened(lib.BIO_new_file(os.fsencode(src), b"rb"), f"'{src}'")
        key_bio = opened(lib.BIO_new_file(os.fsencode(private_key), b"rb"),
                         f"key '{private_key}'")
        target = opened(lib.BIO_new_file(os.fsencode(dst), b"wb"), f"'{dst}'")

        content = lib.d2i_CMS_bio(source, None)
        if not content:
            raise RuntimeError(_libcrypto_error(lib, f"'{src}' is not CMS DER"))

        try:
            key = lib.PEM_read_bio_PrivateKey(key_bio, None, None, None)
            if not key:
                raise RuntimeError(_libcrypto_error(lib, f"'{private_key}' is not a PEM key"))

            try:
                if not lib.CMS_decrypt(content, key, None, None, target, CMS_BINARY):
                    raise RuntimeError(_libcrypto_error(lib, "CMS_decrypt failed"))
            finally:
                lib.EVP_PKEY_free(key)
        finally:
            lib.CMS_ContentInfo_free(content)
    finally:
        for bio in handles:
            lib.BIO_free_all(bio)


X509_ASN_ENCODING = 0x00000001
PKCS_7_ASN_ENCODING = 0x00010000
CMSG_CTRL_DECRYPT = 2
CMSG_RECIPIENT_COUNT_PARAM = 17
CMSG_RECIPIENT_INFO_PARAM = 19
CMSG_INDEFINITE_LENGTH = 0xFFFFFFFF
CERT_FIND_SUBJECT_CERT = 0x000B0000
CRYPT_ACQUIRE_CACHE_FLAG = 0x00000001
CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG = 0x00010000

# Both OpenSSL paths decode the whole structure in memory and fail near 2 GiB: measured, a
# 1794 MB '.p7b' decrypts with libcrypto and openssl and a 2013 MB one does not (OpenSSL's
# BUF_MEM ceiling). The streaming CryptoAPI backend has no such limit - it decrypts 2050 MB -
# so past this size say which backend can actually do the job.
CMS_SIZE_CEILING = 1800 << 20

# How much of the message to hand the decoder at a time. Only this much is ever in flight.
_CNG_CHUNK = 8 << 20

# Windows hands decrypted content back through this callback: (pvArg, pbData, cbData, fFinal).
_STREAM_OUTPUT = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p,
                                    ctypes.POINTER(ctypes.c_ubyte), ctypes.c_uint32,
                                    ctypes.c_int) if os.name == "nt" else None


def _probe_cng(private_key: str | None) -> str | None:
    if os.name != "nt":
        return "Windows only"
    if private_key:
        # Deliberate: CryptoAPI takes its key from the certificate store, and importing a PEM
        # key into a store just to use it here buys nothing a PEM-capable backend cannot do.
        return "a private key file was given; PEM keys go to a PEM-capable backend"
    try:
        ctypes.WinDLL("crypt32", use_last_error=True)
    except OSError as error:
        return f"crypt32 could not be loaded ({error})"
    return None


def _declare_crypt32(crypt32, stream_info_type, decrypt_para_type) -> None:
    """Prototype every call used below.

    Not optional: an undeclared function returns c_int, which truncates the 64-bit message,
    store, certificate and key handles on x64.
    """
    from ctypes import wintypes

    crypt32.CryptMsgOpenToDecode.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                             ctypes.c_void_p, ctypes.c_void_p,
                                             ctypes.POINTER(stream_info_type)]
    crypt32.CryptMsgOpenToDecode.restype = ctypes.c_void_p
    crypt32.CryptMsgUpdate.argtypes = [ctypes.c_void_p, ctypes.c_char_p, wintypes.DWORD,
                                       wintypes.BOOL]
    crypt32.CryptMsgUpdate.restype = wintypes.BOOL
    crypt32.CryptMsgGetParam.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                         ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
    crypt32.CryptMsgGetParam.restype = wintypes.BOOL
    crypt32.CryptMsgControl.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                        ctypes.POINTER(decrypt_para_type)]
    crypt32.CryptMsgControl.restype = wintypes.BOOL
    crypt32.CryptMsgClose.argtypes = [ctypes.c_void_p]
    crypt32.CryptMsgClose.restype = wintypes.BOOL
    crypt32.CertOpenSystemStoreW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
    crypt32.CertOpenSystemStoreW.restype = ctypes.c_void_p
    crypt32.CertCloseStore.argtypes = [ctypes.c_void_p, wintypes.DWORD]
    crypt32.CertCloseStore.restype = wintypes.BOOL
    crypt32.CertFindCertificateInStore.argtypes = [ctypes.c_void_p, wintypes.DWORD,
                                                   wintypes.DWORD, wintypes.DWORD,
                                                   ctypes.c_void_p, ctypes.c_void_p]
    crypt32.CertFindCertificateInStore.restype = ctypes.c_void_p
    crypt32.CertFreeCertificateContext.argtypes = [ctypes.c_void_p]
    crypt32.CertFreeCertificateContext.restype = wintypes.BOOL
    crypt32.CryptAcquireCertificatePrivateKey.argtypes = [
        ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.BOOL)]
    crypt32.CryptAcquireCertificatePrivateKey.restype = wintypes.BOOL


def _attach_recipient_key(crypt32, message, store, recipients: int, para_type) -> None:
    """Find a recipient whose private key this user holds and switch the message to decrypting.

    Must happen after the header is parsed and before content is handed back, which is why the
    feed loop below asks for the recipient count after every chunk until it is available.
    """
    from ctypes import wintypes

    last_error = "no recipient in the message matched a certificate in the user's 'MY' store"
    for index in range(recipients):
        size = wintypes.DWORD()
        if not crypt32.CryptMsgGetParam(message, CMSG_RECIPIENT_INFO_PARAM, index, None,
                                        ctypes.byref(size)):
            continue

        info = ctypes.create_string_buffer(size.value)
        if not crypt32.CryptMsgGetParam(message, CMSG_RECIPIENT_INFO_PARAM, index, info,
                                        ctypes.byref(size)):
            continue

        # The recipient info starts with the issuer and serial, which is exactly what
        # CERT_FIND_SUBJECT_CERT matches on.
        certificate = crypt32.CertFindCertificateInStore(
            store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, info, None)
        if not certificate:
            continue

        key = ctypes.c_void_p()
        key_spec = wintypes.DWORD()
        caller_frees = wintypes.BOOL()
        # ALLOW_NCRYPT is required, not optional: a PFX imported by certutil lands in a CNG
        # provider, and without this flag the acquired handle is treated as a legacy provider
        # and CryptMsgControl fails with NTE_BAD_PROV_TYPE.
        acquired = crypt32.CryptAcquireCertificatePrivateKey(
            certificate, CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG, None,
            ctypes.byref(key), ctypes.byref(key_spec), ctypes.byref(caller_frees))
        crypt32.CertFreeCertificateContext(certificate)

        if not acquired:
            last_error = (f"found the recipient certificate but could not use its private key: "
                          f"{ctypes.WinError(ctypes.get_last_error())}")
            continue

        para = para_type(ctypes.sizeof(para_type), key, key_spec, index)
        if crypt32.CryptMsgControl(message, 0, CMSG_CTRL_DECRYPT, ctypes.byref(para)):
            return

        last_error = f"CMSG_CTRL_DECRYPT failed: {ctypes.WinError(ctypes.get_last_error())}"

    raise RuntimeError(last_error)


def _decrypt_cng(src: str, dst: str, private_key: str | None) -> None:
    """Decrypt with the recipient key held in the current user's certificate store, streaming.

    No key file is involved: an operator who imported the recipient certificate decrypts with
    nothing on the command line.

    The message is decoded incrementally (CMSG_STREAM_INFO) rather than in one buffer, which is
    what makes this the only backend without a size ceiling: CryptDecryptMessage refuses past
    about 100 MB and both OpenSSL paths fail near 2 GiB, while this one decrypts a 2 GB '.p7b'
    at flat memory.
    """
    from ctypes import wintypes

    class StreamInfo(ctypes.Structure):
        _fields_ = [("cbContent", wintypes.DWORD), ("pfnStreamOutput", _STREAM_OUTPUT),
                    ("pvArg", ctypes.c_void_p)]

    class DecryptPara(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("hCryptProvOrNCryptKey", ctypes.c_void_p),
                    ("dwKeySpec", wintypes.DWORD), ("dwRecipientIndex", wintypes.DWORD)]

    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    _declare_crypt32(crypt32, StreamInfo, DecryptPara)

    failure = []

    with open(dst, "wb") as out:
        def on_content(argument, data, length, final):
            # Runs on the CryptMsgUpdate thread. An exception here would unwind into C, so
            # record it and return FALSE to make CryptMsgUpdate fail instead.
            try:
                if length:
                    out.write(ctypes.string_at(data, length))
                return 1
            except OSError as error:
                failure.append(f"writing '{dst}': {error}")
                return 0

        # Keep a reference: a callback object collected while Windows still holds the pointer
        # would crash the process.
        callback = _STREAM_OUTPUT(on_content)
        stream_info = StreamInfo(CMSG_INDEFINITE_LENGTH, callback, None)

        message = crypt32.CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0,
                                               None, None, ctypes.byref(stream_info))
        if not message:
            raise RuntimeError(f"CryptMsgOpenToDecode failed: "
                               f"{ctypes.WinError(ctypes.get_last_error())}")

        store = crypt32.CertOpenSystemStoreW(None, "MY")
        if not store:
            crypt32.CryptMsgClose(message)
            raise RuntimeError(f"CertOpenSystemStoreW('MY') failed: "
                               f"{ctypes.WinError(ctypes.get_last_error())}")

        try:
            _feed_message(crypt32, message, store, src, DecryptPara, failure)
        finally:
            crypt32.CertCloseStore(store, 0)
            crypt32.CryptMsgClose(message)


def _feed_message(crypt32, message, store, src: str, para_type, failure: list) -> None:
    """Push the file through the decoder in chunks, attaching the key as soon as it can be."""
    decrypting = False
    with open(src, "rb") as source:
        while True:
            chunk = source.read(_CNG_CHUNK)
            if not chunk:
                break

            if not crypt32.CryptMsgUpdate(message, chunk, len(chunk), 0):
                raise RuntimeError(failure[0] if failure else
                                   f"CryptMsgUpdate failed: "
                                   f"{ctypes.WinError(ctypes.get_last_error())}")

            if not decrypting:
                decrypting = _start_decrypting(crypt32, message, store, para_type)

        # The final call flushes the last block; without it the tail of the content is lost.
        if not crypt32.CryptMsgUpdate(message, None, 0, 1):
            raise RuntimeError(failure[0] if failure else
                               f"final CryptMsgUpdate failed: "
                               f"{ctypes.WinError(ctypes.get_last_error())}")

    if failure:
        raise RuntimeError(failure[0])
    if not decrypting:
        raise RuntimeError(f"'{src}' carries no CMS recipient information")


def _start_decrypting(crypt32, message, store, para_type) -> bool:
    """True once the key is attached. The recipient count is unavailable until the header has
    been parsed, so this is retried after each chunk rather than assumed on the first."""
    from ctypes import wintypes

    recipients = wintypes.DWORD()
    size = wintypes.DWORD(ctypes.sizeof(recipients))
    if not crypt32.CryptMsgGetParam(message, CMSG_RECIPIENT_COUNT_PARAM, 0,
                                    ctypes.byref(recipients), ctypes.byref(size)):
        return False

    _attach_recipient_key(crypt32, message, store, recipients.value, para_type)
    return True


# Tables, not a dispatch chain of ifs: adding a backend is one row in each.
_PROBES = {
    "cng": _probe_cng,
    "libcrypto": _probe_libcrypto,
    "openssl": _probe_openssl,
}

_BACKENDS = {
    "cng": _decrypt_cng,
    "libcrypto": _decrypt_libcrypto,
    "openssl": _decrypt_openssl,
}
