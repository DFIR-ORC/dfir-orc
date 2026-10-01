#!/bin/bash
#
# Provision the Samba AD domain controller the 'squid-ntlm-*' scenarios need, on the sink host. This
# is the checked-in master copy; deploy it next to setup_squid.sh:
#
#   scp tests/RestAgent/vm/setup_addc.sh "$SINK":/root/
#   ssh "$SINK" "bash /root/setup_addc.sh"      # then: bash /root/setup_squid.sh ntlm
#
# Why a real domain controller, for a proxy test: squid's NTLM authentication is delegated to Samba's
# ntlm_auth, and only a real one completes the handshake. The fake helper shipped with squid answers
# with a minimal Type-2 message that real SSPI clients reject, so a test built on it fails for a
# reason that has nothing to do with DFIR-ORC. This cost a debugging session once; hence the script.
#
# What the scenarios then prove is the finding that motivated them: WinHTTP sends an EMPTY NTLM domain
# for a bare username, which a real proxy rejects (repeating 407), while a domain-qualified
# 'ORCTEST\orc' completes. That is why the fixtures come in both shapes.
#
# Destructive: it wipes any existing Samba configuration on the host. Run it on a throwaway test VM,
# never on anything that serves anything else.
set -e

REALM=ORCTEST.LOCAL
DOMAIN=ORCTEST
USER_NAME=orc
PASSWORD=secret
ADMIN_PASSWORD='Passw0rd!Passw0rd!'

export DEBIAN_FRONTEND=noninteractive
apt-get install -y -qq samba winbind libnss-winbind krb5-user > /tmp/samba-apt.log 2>&1

# The AD DC role replaces smbd/nmbd/winbind with the single samba-ad-dc service; leaving the others
# enabled makes the provisioning refuse to start.
systemctl disable --now smbd nmbd winbind 2>/dev/null || true
systemctl unmask samba-ad-dc 2>/dev/null || true

rm -f /etc/samba/smb.conf
rm -rf /var/lib/samba/private/*

samba-tool domain provision \
    --use-rfc2307 \
    --realm="$REALM" \
    --domain="$DOMAIN" \
    --server-role=dc \
    --dns-backend=SAMBA_INTERNAL \
    --adminpass="$ADMIN_PASSWORD"

cp -f /var/lib/samba/private/krb5.conf /etc/krb5.conf

systemctl enable --now samba-ad-dc
sleep 5

# 'secret' is deliberately weak: the point of the rig is the NTLM handshake, not password policy.
samba-tool domain passwordsettings set --complexity=off --min-pwd-length=1 --min-pwd-age=0
samba-tool user create "$USER_NAME" "$PASSWORD"

# squid runs as 'proxy' and talks to ntlm_auth through the privileged winbind socket, which is
# readable only by that group. Without this the helper fails with NT_STATUS_ACCESS_DENIED and squid
# answers every request with a 407 that no client can satisfy.
usermod -a -G winbindd_priv proxy
chgrp winbindd_priv /var/lib/samba/winbindd_privileged
chmod 750 /var/lib/samba/winbindd_privileged

echo "--- checking ntlm_auth ---"
/usr/bin/ntlm_auth --username="$USER_NAME" --password="$PASSWORD"
echo "domain controller ready: $DOMAIN\\$USER_NAME"
