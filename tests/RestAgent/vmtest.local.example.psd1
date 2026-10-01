# Template for run-vmtest.ps1's settings. Copy it next to this file as
#
#     vmtest.local.psd1
#
# and fill in your own bench. That name is git-ignored and MUST STAY THAT WAY: it names your test
# host, your ssh alias and your local paths, and it points at fixtures that may carry a certificate
# pin or an upload credential. None of that belongs in the repository.
#
# This template is NOT a working default and must never become one. Every value below is
# deliberately unusable: '<name>' for a value to substitute, 'c:\path\to\...' for a path. Neither
# can be reached by accident - angle brackets are not valid in a hostname or a Windows path.
# run-vmtest.ps1 recognises both forms and refuses to run while any setting is still one of them,
# so a half-copied template fails immediately and loudly instead of reaching for something real.
#
# The file is read with Import-PowerShellDataFile, so it is data only - no code runs from it. Every
# key is optional; anything omitted keeps run-vmtest.ps1's default. A command-line switch of the
# same name overrides the file for a single run.
@{
    # Address of the sink host, as DFIR-ORC must reach it. It goes into %ORC_UPLOAD_SERVER% /
    # %ORC_UPLOAD_PROXY%, and the certificate the https fixtures pin must be valid for this name
    # (an IP-SAN certificate for an IP address here).
    VmHost = '<host>'

    # ssh alias (or user@host) used to clear the sink between scenarios and to hash what it received.
    # Set it to '' (empty) to deliberately skip every remote check - local-only runs then cannot
    # verify delivery.
    SshHost = '<ssh-alias>'

    # DFIR-ORC to drive. Two shapes:
    #   - a packaged capsule (DFIR-ORC.exe) with an embedded WOLFLAUNCHER_CONFIG: leave
    #     OrcPrefixArgs and MainConfig empty, the capsule dispatches to WolfLauncher itself;
    #   - a raw per-arch build (DFIR-ORC_x64.exe) straight out of the build tree: it embeds no
    #     configuration, so name the subcommand in OrcPrefixArgs and supply MainConfig below.
    Orc           = 'c:\path\to\DFIR-ORC.exe'
    OrcPrefixArgs = @()
    # OrcPrefixArgs = @('WolfLauncher')
    # MainConfig    = 'wolf-main.example.xml'   # or your own; see that file's header

    # Directory holding the vm-*.xml fixtures. Point this at your own untracked copies when your
    # bench needs a different server_cert_pin or different credentials than the committed ones -
    # make-local-fixtures.ps1 generates exactly that from your host's certificate.
    # ConfigDir = 'c:\path\to\your\local\fixtures'

    # Where each scenario writes its archive and run.log. Keep it OUTSIDE the repository: run.log
    # records the host address and the full command line. It must also be on a local disk - the
    # safe-temp check rejects a network drive.
    OutRoot  = 'c:\path\to\scratch\out'
    SafeTemp = 'c:\path\to\scratch\safetemp'

    # Where the sink reassembles uploads, and the script that wipes it between scenarios.
    # ReceivedDir = '/root/received/up'
    # ClearScript = '/root/clear_received.sh'

    # Seed for the big-* scenarios: a local file whose bytes do NOT compress, which they concatenate
    # (once, twice, three times) into the multi-GB payload they upload. A real collection archive is
    # the obvious choice - a file of zeroes would compress to nothing and the archive uploaded would
    # not be the size under test. It must be on a local disk, and large enough to be worth the name:
    # each scenario's payload is a whole multiple of it, and the point of the third size is to pass
    # 4 GB. Left unset, the big-* scenarios skip instead of failing.
    # BigSeed = 'c:\path\to\a\real\collection.7z'

    # Which authentication your squid is configured for. A squid serves one mode at a time, so this
    # decides which squid scenarios "-Tag all" includes: 'ntlm' needs Samba ntlm_auth against a DC,
    # 'basic' needs basic_ncsa_auth, 'none' leaves every squid scenario out.
    SquidMode = 'none'

    # Listen ports on the sink host, if you moved any (see vm/start_sinks.sh).
    # Ports = @{ Http = 8080; Https = 8443; TlsCap = 8444; TlsCap11 = 8445; Challenge = 8447; Proxy = 8888; ProxyAuth = 8889; Squid = 3128 }
}
