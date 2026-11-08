#include "frogjs/hash.h"
#if defined(__aarch64__)
#include <arm_neon.h>
#if !defined(__APPLE__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif
#endif
#include <algorithm>
#include <cctype>
#include <cstring>

// MD5 (RFC 1321), SHA-1 and SHA-2 (FIPS 180-4), HMAC (RFC 2104) and PBKDF2
// (RFC 8018). Written out here so FrogJS doesn't depend on OpenSSL.

namespace frogjs {

namespace {

// Round constants and initial values, as defined by the standards: MD5's from
// sin(i), SHA-2's from the fractional parts of square and cube roots of primes

const uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

const uint32_t kSha256Init[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

const uint32_t kSha224Init[8] = {
    0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4,
};

const uint64_t kSha512K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

const uint64_t kSha512Init[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

const uint64_t kSha384Init[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};

inline uint32_t Rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t Rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t Rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

uint32_t LoadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint32_t LoadLE32(const uint8_t* p) {
    return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
}
uint64_t LoadBE64(const uint8_t* p) {
    return (uint64_t(LoadBE32(p)) << 32) | LoadBE32(p + 4);
}
void StoreBE32(std::string* out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) *out += static_cast<char>((v >> shift) & 0xFF);
}
void StoreLE32(std::string* out, uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) *out += static_cast<char>((v >> shift) & 0xFF);
}
void StoreBE64(std::string* out, uint64_t v) {
    StoreBE32(out, static_cast<uint32_t>(v >> 32));
    StoreBE32(out, static_cast<uint32_t>(v));
}

// Buffers input into blocks and applies the Merkle-Damgard padding: 0x80,
// zeros, then the message length in bits (kLengthBytes wide)
template <size_t kBlockSize, size_t kLengthBytes, bool kBigEndian>
class BlockHash : public Digest {
public:
    void Update(const uint8_t* data, size_t length) override {
        total_ += length;
        while (length > 0) {
            size_t n = std::min(length, kBlockSize - buffered_);
            memcpy(buffer_ + buffered_, data, n);
            buffered_ += n;
            data += n;
            length -= n;
            if (buffered_ == kBlockSize) {
                Compress(buffer_);
                buffered_ = 0;
            }
        }
    }

    std::string Final() override {
        uint64_t bits = total_ * 8;
        uint8_t pad[kBlockSize * 2] = {0x80};
        size_t pad_length = (buffered_ < kBlockSize - kLengthBytes ? kBlockSize : kBlockSize * 2) - buffered_;
        // Low 64 bits of the length in the last 8 bytes
        uint8_t* length_bytes = pad + pad_length - 8;
        for (int i = 0; i < 8; i++) {
            uint8_t byte = static_cast<uint8_t>(bits >> (8 * i));
            if (kBigEndian) length_bytes[7 - i] = byte;
            else length_bytes[i] = byte;
        }
        // SHA-512's 128-bit length: the bits above 64 (big-endian, before the low half)
        if (kLengthBytes > 8) pad[pad_length - 9] = static_cast<uint8_t>(total_ >> 61);
        Update(pad, pad_length);
        return Output();
    }

    size_t BlockSize() const override { return kBlockSize; }

protected:
    virtual void Compress(const uint8_t* block) = 0;
    virtual std::string Output() const = 0;

private:
    uint8_t buffer_[kBlockSize];
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

class Md5 : public BlockHash<64, 8, false> {
public:
    std::unique_ptr<Digest> Clone() const override { return std::make_unique<Md5>(*this); }
    size_t DigestSize() const override { return 16; }

protected:
    void Compress(const uint8_t* block) override {
        static const int kShifts[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
        };
        uint32_t m[16];
        for (int i = 0; i < 16; i++) m[i] = LoadLE32(block + i * 4);
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            f += a + kMd5K[i] + m[g];
            a = d;
            d = c;
            c = b;
            b += Rotl32(f, kShifts[i]);
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    }

    std::string Output() const override {
        std::string out;
        for (uint32_t word : h_) StoreLE32(&out, word);
        return out;
    }

private:
    uint32_t h_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
};

class Sha1 : public BlockHash<64, 8, true> {
public:
    std::unique_ptr<Digest> Clone() const override { return std::make_unique<Sha1>(*this); }
    size_t DigestSize() const override { return 20; }

protected:
    void Compress(const uint8_t* block) override {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = LoadBE32(block + i * 4);
        for (int i = 16; i < 80; i++) w[i] = Rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            uint32_t temp = Rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rotl32(b, 30);
            b = a;
            a = temp;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
    }

    std::string Output() const override {
        std::string out;
        for (uint32_t word : h_) StoreBE32(&out, word);
        return out;
    }

private:
    uint32_t h_[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
};

// SHA-256, and SHA-224 (other initial values, output truncated to 7 words)
#if defined(__aarch64__)

// The ARMv8 SHA-2 instructions: several times faster than the portable code below
__attribute__((target("sha2"))) void Sha256CompressArm(uint32_t* state, const uint8_t* block) {
    uint32x4_t abcd = vld1q_u32(state);
    uint32x4_t efgh = vld1q_u32(state + 4);
    const uint32x4_t abcd_start = abcd;
    const uint32x4_t efgh_start = efgh;

    uint32x4_t m[4];
    for (int i = 0; i < 4; i++) m[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + i * 16)));

    for (int r = 0; r < 16; r++) {
        const uint32x4_t wk = vaddq_u32(m[r % 4], vld1q_u32(&kSha256K[r * 4]));
        const uint32x4_t previous = abcd;
        abcd = vsha256hq_u32(abcd, efgh, wk);
        efgh = vsha256h2q_u32(efgh, previous, wk);
        if (r < 12) {
            m[r % 4] = vsha256su1q_u32(vsha256su0q_u32(m[r % 4], m[(r + 1) % 4]), m[(r + 2) % 4], m[(r + 3) % 4]);
        }
    }
    vst1q_u32(state, vaddq_u32(abcd, abcd_start));
    vst1q_u32(state + 4, vaddq_u32(efgh, efgh_start));
}

bool HasSha2Instructions() {
#if defined(__APPLE__)
    return true;  // every arm64 Mac has them
#else
    static const bool has = (getauxval(AT_HWCAP) & HWCAP_SHA2) != 0;
    return has;
#endif
}

#endif  // __aarch64__

class Sha256 : public BlockHash<64, 8, true> {
public:
    explicit Sha256(bool sha224) : words_(sha224 ? 7 : 8) {
        memcpy(h_, sha224 ? kSha224Init : kSha256Init, sizeof(h_));
    }
    std::unique_ptr<Digest> Clone() const override { return std::make_unique<Sha256>(*this); }
    size_t DigestSize() const override { return words_ * 4; }

protected:
    void Compress(const uint8_t* block) override {
#if defined(__aarch64__)
        if (HasSha2Instructions()) {
            Sha256CompressArm(h_, block);
            return;
        }
#endif
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = LoadBE32(block + i * 4);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; i++) {
            uint32_t s1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + s1 + ch + kSha256K[i] + w[i];
            uint32_t s0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    std::string Output() const override {
        std::string out;
        for (size_t i = 0; i < words_; i++) StoreBE32(&out, h_[i]);
        return out;
    }

private:
    uint32_t h_[8];
    size_t words_;
};

// SHA-512, and SHA-384 (other initial values, output truncated to 6 words)
class Sha512 : public BlockHash<128, 16, true> {
public:
    explicit Sha512(bool sha384) : words_(sha384 ? 6 : 8) {
        memcpy(h_, sha384 ? kSha384Init : kSha512Init, sizeof(h_));
    }
    std::unique_ptr<Digest> Clone() const override { return std::make_unique<Sha512>(*this); }
    size_t DigestSize() const override { return words_ * 8; }

protected:
    void Compress(const uint8_t* block) override {
        uint64_t w[80];
        for (int i = 0; i < 16; i++) w[i] = LoadBE64(block + i * 8);
        for (int i = 16; i < 80; i++) {
            uint64_t s0 = Rotr64(w[i - 15], 1) ^ Rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = Rotr64(w[i - 2], 19) ^ Rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 80; i++) {
            uint64_t s1 = Rotr64(e, 14) ^ Rotr64(e, 18) ^ Rotr64(e, 41);
            uint64_t ch = (e & f) ^ (~e & g);
            uint64_t t1 = h + s1 + ch + kSha512K[i] + w[i];
            uint64_t s0 = Rotr64(a, 28) ^ Rotr64(a, 34) ^ Rotr64(a, 39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    std::string Output() const override {
        std::string out;
        for (size_t i = 0; i < words_; i++) StoreBE64(&out, h_[i]);
        return out;
    }

private:
    uint64_t h_[8];
    size_t words_;
};

class Hmac : public Digest {
public:
    Hmac(std::unique_ptr<Digest> inner, std::unique_ptr<Digest> outer, const std::string& key)
        : inner_(std::move(inner)), outer_(std::move(outer)) {
        // Keys longer than a block are hashed first; shorter ones are zero-padded
        std::string block_key = key;
        if (block_key.size() > inner_->BlockSize()) {
            std::unique_ptr<Digest> keyHash = inner_->Clone();
            keyHash->Update(block_key);
            block_key = keyHash->Final();
        }
        block_key.resize(inner_->BlockSize(), '\0');
        std::string ipad(block_key), opad(block_key);
        for (size_t i = 0; i < block_key.size(); i++) {
            ipad[i] = static_cast<char>(ipad[i] ^ 0x36);
            opad[i] = static_cast<char>(opad[i] ^ 0x5c);
        }
        inner_->Update(ipad);
        outer_->Update(opad);
    }

    Hmac(const Hmac& other) : inner_(other.inner_->Clone()), outer_(other.outer_->Clone()) {}

    void Update(const uint8_t* data, size_t length) override { inner_->Update(data, length); }

    std::string Final() override {
        outer_->Update(inner_->Final());
        return outer_->Final();
    }

    std::unique_ptr<Digest> Clone() const override { return std::make_unique<Hmac>(*this); }
    size_t BlockSize() const override { return inner_->BlockSize(); }
    size_t DigestSize() const override { return inner_->DigestSize(); }

private:
    std::unique_ptr<Digest> inner_;
    std::unique_ptr<Digest> outer_;
};

}  // namespace

std::string NormalizeHashName(const std::string& name) {
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.rfind("rsa-", 0) == 0) lower.erase(0, 4);
    if (lower.rfind("sha-", 0) == 0) lower.erase(3, 1);  // "sha-256" -> "sha256"
    for (const std::string& supported : SupportedHashes()) {
        if (lower == supported) return supported;
    }
    return "";
}

const std::vector<std::string>& SupportedHashes() {
    static const std::vector<std::string> names = {"md5", "sha1", "sha224", "sha256", "sha384", "sha512"};
    return names;
}

std::unique_ptr<Digest> NewHash(const std::string& name) {
    if (name == "md5") return std::make_unique<Md5>();
    if (name == "sha1") return std::make_unique<Sha1>();
    if (name == "sha224") return std::make_unique<Sha256>(true);
    if (name == "sha256") return std::make_unique<Sha256>(false);
    if (name == "sha384") return std::make_unique<Sha512>(true);
    if (name == "sha512") return std::make_unique<Sha512>(false);
    return nullptr;
}

std::unique_ptr<Digest> NewHmac(const std::string& name, const std::string& key) {
    std::unique_ptr<Digest> inner = NewHash(name);
    if (!inner) return nullptr;
    std::unique_ptr<Digest> outer = NewHash(name);
    return std::make_unique<Hmac>(std::move(inner), std::move(outer), key);
}

std::string Pbkdf2(const std::string& name, const std::string& password, const std::string& salt,
                   uint32_t iterations, size_t key_length) {
    // The keyed HMAC state is computed once and cloned for every iteration
    std::unique_ptr<Digest> keyed = NewHmac(name, password);
    std::string derived;
    derived.reserve(key_length);
    for (uint32_t block = 1; derived.size() < key_length; block++) {
        std::unique_ptr<Digest> mac = keyed->Clone();
        mac->Update(salt);
        std::string index;
        StoreBE32(&index, block);
        mac->Update(index);
        std::string u = mac->Final();
        std::string t = u;
        for (uint32_t i = 1; i < iterations; i++) {
            mac = keyed->Clone();
            mac->Update(u);
            u = mac->Final();
            for (size_t j = 0; j < t.size(); j++) t[j] = static_cast<char>(t[j] ^ u[j]);
        }
        derived.append(t, 0, std::min(t.size(), key_length - derived.size()));
    }
    return derived;
}

}  // namespace frogjs
