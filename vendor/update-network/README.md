# Update-check HTTPS dependencies

`scripts/build-update-network.sh` builds pinned curl 8.22.0 and Mbed TLS 3.6.5
source archives, checked by SHA-256. They are statically linked only into the host.
Their upstream licenses are included in the VPK under `licenses/`.

The Vita compatibility patch is from
<https://github.com/vitasdk/packages/blob/master/mbedtls/mbedtls.patch>.
It uses VitaSDK's `getentropy` and socket interfaces. The build options follow
the VitaSDK `mbedtls` and `curl-mbedtls` recipes.

`host-direct/resources/update-ca.pem` is a snapshot of Mozilla's CA bundle from
<https://curl.se/ca/cacert.pem>, retrieved 2026-10-08. Certificate and hostname
verification remain enabled. The certificate data is covered by MPL 2.0:
<https://curl.se/docs/caextract.html> and <https://www.mozilla.org/MPL/2.0/>.

Update this bundle periodically along with the pinned libraries. Never bypass
certificate verification to work around an old system certificate store.
