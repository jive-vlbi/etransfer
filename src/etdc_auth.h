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
#include <functional>

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

    // Called to obtain a passphrase for an encrypted private key. Given the
    // prompt to display, returns the (possibly empty) passphrase. When a
    // load needs a passphrase and no callback was supplied, the key is read
    // from the controlling terminal with echo disabled.
    using passphrase_cb = std::function<std::string(std::string const& prompt)>;

    // Load a private key from 'path'. Both PEM / PKCS#8 and native OpenSSH
    // ("BEGIN OPENSSH PRIVATE KEY") containers are accepted, including
    // passphrase-encrypted keys (OpenSSH uses bcrypt_pbkdf + AES; see
    // etdc_bcrypt.h). 'ask' supplies the passphrase if the key is encrypted;
    // if empty, a no-echo terminal prompt is used. Throws on any failure
    // (including a wrong passphrase).
    pkey_ptr    load_private_key(std::string const& path, passphrase_cb ask = {});

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

    // ---- channel binding ------------------------------------------------
    //
    // The TLS keying-material exporter (RFC 5705) both peers use to derive
    // the bytes an 'auth' signature commits to. Same shape as
    // etdc::etdc_fd::tls_exporter_fn, but kept as a distinct alias so this
    // module does not need to include etdc_fd.h.
    using exporter_fn = std::function<bytes(std::string const& label,
                                            std::string const& context,
                                            size_t length)>;

    // Derive the channel binding for 'principal' from 'exporter'. Defined
    // once so the client (signing) and daemon (verifying) cannot drift on the
    // exporter label / output length. Throws std::runtime_error if 'exporter'
    // is empty (e.g. a cleartext connection - there is no binding to sign).
    bytes       auth_channel_binding(exporter_fn const& exporter, std::string const& principal);

    // ---- signer abstraction (client side) -------------------------------
    //
    // What the client must present in the wire 'auth' command (besides the
    // principal, which the caller already knows): the SSH signature-algorithm
    // name and the SSH public-key + signature blobs.
    struct auth_material {
        std::string sig_algo;     // 'keytype' field
        bytes       pubkey_blob;  // base64 -> 'pubkey-b64' field
        bytes       sig_blob;     // base64 -> 'sig-b64' field
    };

    // A signer turns the channel binding (derived from 'exporter' for the
    // given principal) into auth_material. Backends: an identity file now,
    // ssh-agent later (2b-iii). Throws std::runtime_error on failure.
    using signer_fn = std::function<auth_material(std::string const& principal,
                                                  exporter_fn const& exporter)>;

    // Identity-file backend: load (and, if needed, decrypt via 'ask') the
    // private key at 'path' now - so a bad path / wrong passphrase fails fast,
    // before any network round-trip - and return a signer that signs with it.
    signer_fn   make_identity_signer(std::string const& path, passphrase_cb ask = {});

}} // namespace etdc::auth

#endif // ETDC_TLS
#endif // ETDC_ETDC_AUTH_H
