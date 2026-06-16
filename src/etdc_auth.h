// ssh-pubkey authentication helpers (Phase 2) - SSH wire codec + libcrypto
// verify/sign, authorized_keys parsing. Only compiled when ETDC_TLS is set.
// Copyright (C) 2007-2016 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef ETDC_ETDC_AUTH_H
#define ETDC_ETDC_AUTH_H

// The whole module only exists in a TLS build (it needs libcrypto and there
// is no point authenticating a connection that isn't encrypted). Keep the
// file a valid - empty - translation unit otherwise so the Makefile can list
// it unconditionally in etc_SRC / etd_SRC.
#ifdef ETDC_TLS

#include <string>
#include <vector>
#include <memory>

#include <openssl/evp.h>

namespace etdc { namespace auth {

    // Raw byte buffer. SSH "blobs" (public keys, signatures) and the bytes
    // that travel base64-encoded on the wire are all just sequences of octets.
    using bytes = std::vector<unsigned char>;

    // Owning handle for an EVP_PKEY (private or public).
    using pkey_ptr = std::shared_ptr<EVP_PKEY>;

    // ---- base64 (standard alphabet, '=' padding) ------------------------
    // decode throws std::runtime_error on malformed input.
    bytes       base64_decode(std::string const& s);
    std::string base64_encode(bytes const& b);

    // ---- verification (daemon side) -------------------------------------
    //
    // verify(): is 'sig_blob' (an SSH signature blob: string(algo) ||
    // string(sig)) a valid signature, under SSH signature algorithm
    // 'sig_algo', over 'signed_data', made by the key encoded in
    // 'pubkey_blob' (an SSH public-key blob, i.e. the 2nd field of an
    // authorized_keys line, base64-decoded)?
    //
    // Total function: any malformed input or OpenSSL failure yields false
    // (never throws, never leaks OpenSSL error text to the caller/peer).
    bool verify(bytes const& signed_data, std::string const& sig_algo,
                bytes const& pubkey_blob, bytes const& sig_blob);

    // A single parsed authorized_keys entry.
    struct authkey {
        std::string key_algo;   // e.g. "ssh-ed25519", "ssh-rsa", "ecdsa-sha2-nistp256"
        bytes       blob;       // the SSH public-key blob (base64-decoded 2nd field)
        std::string comment;    // trailing comment, if any (for audit logging)
    };

    // Parse <dir>/<principal> as an OpenSSH authorized_keys file and return
    // the recognised entries. A missing/unreadable file yields an empty list
    // (not an error: it simply means "no keys authorised for this principal").
    // Lines that are blank, comments (#...) or unrecognised are skipped.
    std::vector<authkey> authorized_keys_for(std::string const& dir, std::string const& principal);

    // High-level daemon check: the principal must have an authorized key
    // whose blob equals 'pubkey_blob' AND the signature must verify over
    // 'signed_data'. On success returns true and sets out_comment to the
    // matching key's comment (for the audit log). Total function (no throw).
    bool authenticate(std::string const& dir, std::string const& principal,
                      bytes const& signed_data, std::string const& sig_algo,
                      bytes const& pubkey_blob, bytes const& sig_blob,
                      std::string& out_comment);

    // ---- signing (client / test side) -----------------------------------
    //
    // All of these throw std::runtime_error on failure.

    // Load a private key from a PEM / PKCS#8 file (optionally encrypted; the
    // standard OpenSSL passphrase prompt is used if needed). NOTE: native
    // OpenSSH-format keys ("BEGIN OPENSSH PRIVATE KEY") are not handled here
    // yet - that is a 2b-ii concern.
    pkey_ptr    load_private_key(std::string const& path);

    // The canonical SSH *signature* algorithm name to use with this key:
    // ed25519 -> "ssh-ed25519", RSA -> "rsa-sha2-256", EC P-256 ->
    // "ecdsa-sha2-nistp256". Throws if the key type is unsupported.
    std::string default_sig_algo(EVP_PKEY* pkey);

    // The SSH public-key blob for 'pkey' (suitable for an authorized_keys
    // line / for sending on the wire). out_key_algo receives the key-type
    // name. Throws if the key type is unsupported.
    bytes       public_key_blob(EVP_PKEY* pkey, std::string& out_key_algo);

    // Produce the SSH signature blob (string(algo) || string(sig)) over
    // 'data' using private key 'pkey' and SSH signature algorithm 'sig_algo'.
    bytes       sign(EVP_PKEY* pkey, std::string const& sig_algo, bytes const& data);

}} // namespace etdc::auth

#endif // ETDC_TLS
#endif // ETDC_ETDC_AUTH_H
