#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace frogjs {

// Incremental message digest (a hash function or an HMAC)
class Digest {
public:
    virtual ~Digest() = default;
    virtual void Update(const uint8_t* data, size_t length) = 0;
    // The digest bytes. The object can't be updated afterwards.
    virtual std::string Final() = 0;
    // A copy with the same state (for crypto's hash.copy() and PBKDF2)
    virtual std::unique_ptr<Digest> Clone() const = 0;
    virtual size_t BlockSize() const = 0;
    virtual size_t DigestSize() const = 0;

    void Update(const std::string& data) {
        Update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }
};

// Canonical name of a supported hash ("SHA-256", "RSA-SHA256" -> "sha256"),
// or "" if it isn't supported
std::string NormalizeHashName(const std::string& name);

// md5, sha1, sha224, sha256, sha384, sha512
const std::vector<std::string>& SupportedHashes();

// A digest for a canonical hash name, or nullptr
std::unique_ptr<Digest> NewHash(const std::string& name);

// An HMAC (RFC 2104) keyed with key, or nullptr if the hash is unsupported
std::unique_ptr<Digest> NewHmac(const std::string& name, const std::string& key);

// PBKDF2 with HMAC-<name> (RFC 8018). name must be supported.
std::string Pbkdf2(const std::string& name, const std::string& password, const std::string& salt,
                   uint32_t iterations, size_t key_length);

}  // namespace frogjs
