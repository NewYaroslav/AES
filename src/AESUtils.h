#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "AES.h"
#include "secure_zero.h"

namespace aesutils {

constexpr std::size_t BLOCK_SIZE = 16;

inline std::array<uint8_t, BLOCK_SIZE> generate_iv() {
  std::array<uint8_t, BLOCK_SIZE> iv{};
  std::random_device rd;
  std::uniform_int_distribution<int> distribution(0, 255);
  if (rd.entropy() > 0) {
    for (auto &byte : iv) {
      byte = static_cast<uint8_t>(distribution(rd));
    }
  } else {
    auto seed = static_cast<unsigned long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::mt19937 gen(seed);
    for (auto &byte : iv) {
      byte = static_cast<uint8_t>(distribution(gen));
    }
  }
  return iv;
}

inline std::vector<uint8_t> add_padding(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> padded = data;
  std::size_t padding = BLOCK_SIZE - (data.size() % BLOCK_SIZE);
  padded.insert(padded.end(), padding, static_cast<uint8_t>(padding));
  return padded;
}

inline std::vector<uint8_t> remove_padding(const std::vector<uint8_t> &data) {
  if (data.empty()) {
    throw std::invalid_argument("Data is empty, cannot remove padding.");
  }
  uint8_t padding = data.back();
  bool invalid = padding == 0 || padding > BLOCK_SIZE || padding > data.size();
  std::size_t start = data.size();
  if (!invalid) {
    start -= padding;
  }
  for (std::size_t i = start; i < data.size(); ++i) {
    invalid |= (data[i] != padding);
  }
  if (invalid) {
    throw std::invalid_argument("Invalid padding detected.");
  }
  return std::vector<uint8_t>(data.begin(), data.end() - padding);
}

inline std::vector<uint8_t> add_iv_to_ciphertext(
    const std::vector<uint8_t> &ciphertext,
    const std::array<uint8_t, BLOCK_SIZE> &iv) {
  std::vector<uint8_t> result;
  result.reserve(iv.size() + ciphertext.size());
  result.insert(result.end(), iv.begin(), iv.end());
  result.insert(result.end(), ciphertext.begin(), ciphertext.end());
  return result;
}

inline std::vector<uint8_t> extract_iv_from_ciphertext(
    const std::vector<uint8_t> &ciphertext_with_iv,
    std::array<uint8_t, BLOCK_SIZE> &iv) {
  if (ciphertext_with_iv.size() < BLOCK_SIZE) {
    throw std::invalid_argument(
        "Ciphertext is too short to contain a valid IV.");
  }
  std::copy_n(ciphertext_with_iv.begin(), BLOCK_SIZE, iv.begin());
  return std::vector<uint8_t>(ciphertext_with_iv.begin() + BLOCK_SIZE,
                              ciphertext_with_iv.end());
}

struct EncryptedData {
  std::chrono::system_clock::time_point timestamp;
  std::array<uint8_t, BLOCK_SIZE> iv;
  std::vector<uint8_t> ciphertext;
};

enum class AesMode { CBC, CFB };

template <class T>
AESKeyLength key_length_from_key(const T &key) {
  switch (key.size()) {
    case 16:
      return AESKeyLength::AES_128;
    case 24:
      return AESKeyLength::AES_192;
    case 32:
      return AESKeyLength::AES_256;
    default:
      throw std::invalid_argument("Invalid key length");
  }
}

template <class T>
EncryptedData encrypt(const std::vector<uint8_t> &plain, const T &key,
                      AesMode mode) {
  AES aes(key_length_from_key(key));
  auto iv = generate_iv();
  std::vector<uint8_t> padded = add_padding(plain);

  std::unique_ptr<unsigned char[]> encrypted;
  switch (mode) {
    case AesMode::CBC:
      encrypted.reset(
          aes.EncryptCBC(padded.data(), padded.size(), key.data(), iv.data()));
      break;
    case AesMode::CFB:
      encrypted.reset(
          aes.EncryptCFB(padded.data(), padded.size(), key.data(), iv.data()));
      break;
    default:
      throw std::invalid_argument("Invalid AES mode");
  }

  std::vector<uint8_t> ciphertext(encrypted.get(),
                                  encrypted.get() + padded.size());
  secure_zero(encrypted.get(), padded.size());
  secure_zero(padded.data(), padded.size());
  return {std::chrono::system_clock::now(), iv, std::move(ciphertext)};
}

template <class T>
EncryptedData encrypt(const std::string &plain_text, const T &key,
                      AesMode mode) {
  return encrypt(std::vector<uint8_t>(plain_text.begin(), plain_text.end()),
                 key, mode);
}

template <class T>
std::vector<uint8_t> decrypt(const EncryptedData &data, const T &key,
                             AesMode mode) {
  AES aes(key_length_from_key(key));
  std::unique_ptr<unsigned char[]> decrypted;
  switch (mode) {
    case AesMode::CBC:
      decrypted.reset(aes.DecryptCBC(data.ciphertext.data(),
                                     data.ciphertext.size(), key.data(),
                                     data.iv.data()));
      break;
    case AesMode::CFB:
      decrypted.reset(aes.DecryptCFB(data.ciphertext.data(),
                                     data.ciphertext.size(), key.data(),
                                     data.iv.data()));
      break;
    default:
      throw std::invalid_argument("Invalid AES mode");
  }

  std::vector<uint8_t> plain(decrypted.get(),
                             decrypted.get() + data.ciphertext.size());
  secure_zero(decrypted.get(), data.ciphertext.size());
  auto result = remove_padding(plain);
  secure_zero(plain.data(), plain.size());
  return result;
}

template <class T>
std::string decrypt_to_string(const EncryptedData &data, const T &key,
                              AesMode mode) {
  std::vector<uint8_t> plain = decrypt(data, key, mode);
  std::string result(plain.begin(), plain.end());
  secure_zero(plain.data(), plain.size());
  return result;
}

}  // namespace aesutils
