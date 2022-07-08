/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/Support/BigIntSupport.h"

#include "hermes/Support/OptValue.h"

#include "llvh/ADT/APInt.h"
#include "llvh/ADT/bit.h"
#include "llvh/Support/Endian.h"

#include <cmath>
#include <string>

namespace hermes {
namespace bigint {
llvh::ArrayRef<uint8_t> dropExtraSignBits(llvh::ArrayRef<uint8_t> src) {
  if (src.empty()) {
    // return an empty array ref.
    return src;
  }

  const uint8_t drop = getSignExtValue<uint8_t>(src.back());

  // Iterate over all bytes in src, in reverse order, and drop everything that
  // can be inferred with a sign-extension from the previous byte. For example,
  //
  // src = { 0x00, 0x00, 0x00, 0xff }
  //
  // results in { 0x00, 0xff } so that sign extension results in the original
  // sequence being reconstructed.

  auto previousSrc = src;
  while (!src.empty() && src.back() == drop) {
    previousSrc = src;
    src = src.drop_back();
  }

  // Invariants:
  //
  //  * previousSrc.size() > 0
  //  * previousSrc == src -> no bytes dropped from src
  //  * previousSrc != src -> previousSrc.back() == drop
  //  * src.empty() -> original src = {drop, drop, drop, ..., drop, drop} and
  //                   previousSrc[0] == drop
  //
  // The return value should be
  //  * {} iff src.empty and drop == 0x00; or
  //  * {0xff} iff src.empty and drop == 0xff; or
  //  * src iff getSignExtValue(src.back()) == drop; and
  //  * previousSrc otherwise
  //
  // which can be expressed as
  //  * src iff src.empty and drop == 0x00; or
  //  * previousSrc iff src.empty and drop == 0xff; or
  //  * src iff getSignExtValue(src.back()) == drop; and
  //  * previousSrc otherwise
  //
  // By defining
  //   lastChar = src.empty ? 0 : src.back,
  //
  // the return value can be expressed as
  //  * src iff getSignExtValue(lastChar) == drop; and
  //  * previousSrc otherwise
  const uint8_t lastChar = src.empty() ? 0u : src.back();

  return getSignExtValue<uint8_t>(lastChar) == drop ? src : previousSrc;
}

namespace {
/// Trims any digits in \p dst that can be inferred by a sign extension.
void ensureCanonicalResult(MutableBigIntRef &dst) {
  auto ptr = reinterpret_cast<uint8_t *>(dst.digits);
  const uint32_t sizeInBytes = dst.numDigits * BigIntDigitSizeInBytes;

  llvh::ArrayRef<uint8_t> compactView =
      dropExtraSignBits(llvh::makeArrayRef(ptr, sizeInBytes));
  dst.numDigits = numDigitsForSizeInBytes(compactView.size());
}
} // namespace

// Ensure there's a compile-time failure if/when hermes is compiled for
// big-endian machines. This is needed for correct serialization and
// deserialization (the hermes bytecode format expects bigint bytes in
// little-endian format).
static_assert(
    llvh::support::endian::system_endianness() == llvh::support::little,
    "BigIntSupport expects little-endian host");

OperationStatus initWithBytes(
    MutableBigIntRef dst,
    llvh::ArrayRef<uint8_t> data) {
  const uint32_t dstSizeInBytes = dst.numDigits * BigIntDigitSizeInBytes;

  assert(dst.digits != nullptr && "buffer can't be nullptr");

  if (dstSizeInBytes < data.size()) {
    // clear numDigits in the response (i.e., sanitizing the output).
    dst.numDigits = 0;
    return OperationStatus::DEST_TOO_SMALL;
  }

  const size_t dataSizeInBytes = data.size();

  if (dataSizeInBytes == 0) {
    // data is empty, so don't bother copying it to dst; simply return 0n.
    dst.numDigits = 0;
    return OperationStatus::RETURNED;
  }

  // Get a uint8_t* to dst so we can do pointer arithmetic.
  auto *ptr = reinterpret_cast<uint8_t *>(dst.digits);

  // Copy bytes first; dataSizeInBytes may not be a multiple of
  // BigIntDigitSizeInBytes.
  memcpy(ptr, data.data(), dataSizeInBytes);

  // Now sign-extend to a length that's multiple of DigitType size. Note that
  // dataSizeInBytes is not zero (otherwise the function would have returned)
  // by now.
  const uint32_t numBytesToSet = dstSizeInBytes - dataSizeInBytes;
  const uint8_t signExtValue =
      getSignExtValue<uint8_t>(ptr[dataSizeInBytes - 1]);

  memset(ptr + dataSizeInBytes, signExtValue, numBytesToSet);

  ensureCanonicalResult(dst);
  return OperationStatus::RETURNED;
}

bool isNegative(ImmutableBigIntRef src) {
  return src.numDigits > 0 &&
      static_cast<SignedBigIntDigitType>(src.digits[src.numDigits - 1]) < 0;
}

uint32_t fromDoubleResultSize(double src) {
  uint64_t srcI = llvh::bit_cast<uint64_t>(src);
  int64_t exp = ((srcI >> 52) & 0x7ff) - 1023;

  // If the exponent is negative, |src| is in +/- 0.xyz, so
  // just return 0.
  if (exp < 0)
    return 0;

  // double needs at most numBits(mantissa) + 1 (implicit 1 in the mantissa) +
  // exp - numBits(mantissa) + 1 bits to be represented, hence the + 2 below.
  return numDigitsForSizeInBits(exp + 2);
}

OperationStatus fromDouble(MutableBigIntRef dst, double src) {
  assert(
      dst.numDigits >= fromDoubleResultSize(src) &&
      "not enough digits provided for double conversion");
  // A double can represent a 1024-bit number; the extra bit is needed to
  // represent the BigInt's sign.
  const uint32_t MaxBitsToRepresentDouble =
      llvh::alignTo(1024 + 1, BigIntDigitSizeInBits);
  llvh::APInt tmp =
      llvh::APIntOps::RoundDoubleToAPInt(src, MaxBitsToRepresentDouble);

  auto *ptr = reinterpret_cast<const uint8_t *>(tmp.getRawData());
  auto size = tmp.getNumWords() * BigIntDigitSizeInBytes;
  auto bytesRef = llvh::makeArrayRef<uint8_t>(ptr, size);
  return initWithBytes(dst, dropExtraSignBits(bytesRef));
}

double toDouble(ImmutableBigIntRef src) {
  if (src.numDigits == 0) {
    return 0.0;
  }

  const uint32_t numBits = src.numDigits * BigIntDigitSizeInBits;
  llvh::APInt tmp(numBits, llvh::makeArrayRef(src.digits, src.numDigits));
  constexpr bool kSigned = true;
  return tmp.roundToDouble(kSigned);
}

namespace {
/// ES5.1 7.2
/// Copied from Operations.h to preserve layering.
static inline bool isWhiteSpaceChar(char16_t c) {
  return c == u'\u0009' || c == u'\u000B' || c == u'\u000C' || c == u'\u0020' ||
      c == u'\u00A0' || c == u'\uFEFF' || c == u'\u1680' ||
      (c >= u'\u2000' && c <= u'\u200A') || c == u'\u202F' || c == u'\u205F' ||
      c == u'\u3000';
}

/// A class with several utility methods used for parsing strings as bigints.
/// The spec has multiple types of bigint strings; each "type" should have
/// its own parser class that inherits from this.
template <typename StringRefT>
class BigIntLiteralParsingToolBox {
  BigIntLiteralParsingToolBox(const BigIntLiteralParsingToolBox &) = delete;
  BigIntLiteralParsingToolBox &operator=(const BigIntLiteralParsingToolBox &) =
      delete;
  BigIntLiteralParsingToolBox(BigIntLiteralParsingToolBox &&) = delete;
  BigIntLiteralParsingToolBox &operator=(BigIntLiteralParsingToolBox &&) =
      delete;

 protected:
  // The underlying string's char type.
  using CharT = std::remove_cv_t<std::remove_reference_t<decltype(
      *typename StringRefT::const_iterator{})>>;

  BigIntLiteralParsingToolBox(
      StringRefT str,
      uint8_t &radix,
      std::string &bigintDigits,
      ParsedSign &sign,
      std::string *outError)
      : it_(str.begin()),
#ifndef NDEBUG
        begin_(str.begin()),
#endif // NDEBUG
        end_(str.end()),
        radix_(radix),
        bigintDigits_(bigintDigits),
        sign_(sign),
        outError_(outError) {
    bigintDigits_.clear();
    bigintDigits_.reserve(end_ - it_);

    sign_ = ParsedSign::None;
  }

  bool nonDecimalIntegerLiteral() {
    return binaryIntegerLiteral() || octalIntegerLiteral() ||
        hexIntegerLiteral();
  }

  bool binaryIntegerLiteral() {
#define BIGINT_BINARY_PREFIX 'B', 'b'
#define BIGINT_BINARY_DIGITS '0', '1'
    if (lookaheadAndEatIfAnyOf<BIGINT_BINARY_PREFIX>()) {
      radix_ = 2;
      buildBigIntWithDigits<BIGINT_BINARY_DIGITS>();
      return bigintDigits_.size() > 0;
    }

    return false;
#undef BIGINT_BINARY_PREFIX
#undef BIGINT_BINARY_DIGITS
  }

  bool octalIntegerLiteral() {
#define BIGINT_OCTAL_PREFIX 'O', 'o'
#define BIGINT_OCTAL_DIGITS '0', '1', '2', '3', '4', '5', '6', '7'
    if (lookaheadAndEatIfAnyOf<BIGINT_OCTAL_PREFIX>()) {
      radix_ = 8;
      buildBigIntWithDigits<BIGINT_OCTAL_DIGITS>();
      return bigintDigits_.size() > 0;
    }

    return false;
#undef BIGINT_OCTAL_PREFIX
#undef BIGINT_OCTAL_DIGITS
  }

  bool hexIntegerLiteral() {
#define BIGINT_HEX_PREFIX 'X', 'x'
#define BIGINT_HEX_DIGITS                                                    \
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', \
      'F', 'a', 'b', 'c', 'd', 'e', 'f'

    if (lookaheadAndEatIfAnyOf<BIGINT_HEX_PREFIX>()) {
      radix_ = 16;
      buildBigIntWithDigits<BIGINT_HEX_DIGITS>();
      return bigintDigits_.size() > 0;
    }

    return false;
#undef BIGINT_HEX_PREFIX
#undef BIGINT_HEX_DIGITS
  }

  bool nonZeroDecimalLiteral() {
#define BIGINT_NONZERO_DEC_DIGITS '1', '2', '3', '4', '5', '6', '7', '8', '9'
#define BIGINT_DEC_DIGITS '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
    if (nextIsAnyOf<BIGINT_NONZERO_DEC_DIGITS>()) {
      radix_ = 10;
      buildBigIntWithDigits<BIGINT_DEC_DIGITS>();
      return bigintDigits_.size() > 0;
    }
    return false;
#undef BIGINT_NONZERO_DEC_DIGITS
#undef BIGINT_DEC_DIGITS
  }

  bool decimalDigits() {
    // first trims all leading zeroes, but keep 1 if the input is just zeroes.
    auto ch0 = peek();
    while (ch0 && *ch0 == '0') {
      auto chNext = peek(1);
      if (!chNext) {
        break;
      }
      eat();
      ch0 = chNext;
    }

#define BIGINT_DEC_DIGITS '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
    if (nextIsAnyOf<BIGINT_DEC_DIGITS>()) {
      radix_ = 10;
      buildBigIntWithDigits<BIGINT_DEC_DIGITS>();
      return bigintDigits_.size() > 0;
    }
    return false;
#undef BIGINT_NONZERO_DEC_DIGITS
  }

  bool fail(const char *err) {
    if (outError_) {
      *outError_ = err;
    }
    return false;
  }

  bool checkEnd(const char *err) {
    auto ch = peek();
    // parsing suceeded if there are no more characters to be consumed, or if
    // the next charater is a null terminator.
    return (ch && *ch != 0) ? fail(err) : true;
  }

  template <CharT... digits>
  void buildBigIntWithDigits() {
    OptValue<CharT> ch = lookaheadAndEatIfAnyOf<digits...>();
    while (ch.hasValue()) {
      bigintDigits_.push_back(*ch);
      ch = lookaheadAndEatIfAnyOf<digits...>();
    }
  }

  template <CharT c>
  static bool anyOf(CharT rhs) {
    return c == rhs;
  }

  template <CharT c, CharT d, CharT... rest>
  static bool anyOf(CharT rhs) {
    return (c == rhs) || anyOf<d, rest...>(rhs);
  }

  template <CharT... chars>
  OptValue<CharT> lookaheadAndEatIfAnyOf() {
    OptValue<CharT> ret;

    if (auto ch = nextIsAnyOf<chars...>()) {
      eat();
      ret = ch;
    }

    return ret;
  }

  template <CharT... chars>
  OptValue<CharT> nextIsAnyOf() {
    OptValue<CharT> ret;

    if (auto ch = peek()) {
      if (anyOf<chars...>(*ch)) {
        ret = ch;
      }
    }

    return ret;
  }

  /// An opaque type that represents the current state of the number parser.
  using ParserState = typename StringRefT::const_iterator;

  /// \return the parser's current state, which can later be used as an argument
  /// to restoreParserState to reset the parser. Useful for cases that need
  /// backtracking.
  ParserState getCurrentParserState() const {
    return it_;
  }

  /// Restore the parser state to that of \p state.
  void restoreParserState(const ParserState &state) {
    assert(
        begin_ <= state &&
        "invalid parser state - pointing before input start");
    assert(state <= end_ && "invalid parser state - pointing past input end");
    it_ = state;
  }

  /// Eats (i.e., advances) 1 char in the input string.
  /// \return empty if at the end of input; or the "eaten" char if not.
  OptValue<CharT> eat() {
    OptValue<CharT> c = peek();
    if (c) {
      it_ += 1;
    }
    return c;
  }

  /// Peeks (i.e., returns, but does not advance) the \p which-th char from the
  /// current position.
  /// \return empty if \p which is greater than the remaining chars in the input
  /// buffer; or the \p which-th char from the current position.
  OptValue<CharT> peek(std::ptrdiff_t which = 0) const {
    OptValue<CharT> ret;
    if (it_ + which < end_) {
      ret = *(it_ + which);
    }
    return ret;
  }

  typename StringRefT::const_iterator it_;
#ifndef NDEBUG
  typename StringRefT::const_iterator begin_;
#endif // NDEBUG
  typename StringRefT::const_iterator end_;
  uint8_t &radix_;
  std::string &bigintDigits_;
  ParsedSign &sign_;
  std::string *outError_;
};

/// A class for parsing StringIntegerLiteral as bigints. Used to parse string
/// arguments to the BigInt constructor.
/// See https://tc39.es/ecma262/#sec-stringintegerliteral-grammar.
template <typename StringRefT>
class StringIntegerLiteralParser
    : public BigIntLiteralParsingToolBox<StringRefT> {
 public:
  StringIntegerLiteralParser(
      StringRefT str,
      uint8_t &radix,
      std::string &bigintDigits,
      ParsedSign &sign,
      std::string *outError)
      : BigIntLiteralParsingToolBox<StringRefT>(
            str,
            radix,
            bigintDigits,
            sign,
            outError) {
    if (this->it_ < this->end_ && *(this->end_ - 1) == 0) {
      --this->end_;
    }

    // StringIntegerLiterals may have leading/trailing whitespaces; skip them
    // now.
    while (this->it_ < this->end_ && isWhiteSpaceChar(*this->it_)) {
      ++this->it_;
    }

    while (this->it_ < this->end_ && isWhiteSpaceChar(*(this->end_ - 1))) {
      --this->end_;
    }
  }

  /// goal function for parsing a string passed to %BigInt% -- the bigint
  /// conversion function. \p return true if parsing suceeds, and false
  /// otherwise.
  bool goal() && {
    auto ch = this->peek();
    if (!ch) {
      this->radix_ = 10;
      this->bigintDigits_ = "0";
      return true;
    } else {
      if (*ch == '0') {
        // Saving the parser state in case this is not a non-decimal integer,
        // but rather a decimal integer with leading zeros.
        auto atZero = this->getCurrentParserState();

        // discard the current char -- possibly the '0' in 0x, 0o, or 0b.
        this->eat();

        // NonDecimalIntegerLiteral
        if (this->nonDecimalIntegerLiteral()) {
          return this->checkEnd("trailing data in non-decimal literal");
        }

        // Put the parser back at the initial 0, and retry parsing input as a
        // decimal string.
        this->restoreParserState(atZero);
      }

      if (auto signCh = this->template lookaheadAndEatIfAnyOf<'+', '-'>()) {
        this->sign_ = *ch == '+' ? ParsedSign::Plus : ParsedSign::Minus;
      }

      // This must be NonZeroDecimalLiteral
      if (this->decimalDigits()) {
        return this->checkEnd("trailing data in decimal literal");
      }
    }

    return this->fail("invalid bigint literal");
  }
};

/// \return How many bits to request when creating the APInt for \p str
/// using \p radix. Round the result so we always request a whole number of
/// BigIntDigitType words.
template <typename StringRefT>
static unsigned numBitsForBigintDigits(StringRefT str, uint8_t radix) {
  assert(
      (radix == 2 || radix == 4 || radix == 8 || radix == 10 || radix == 16) &&
      "unpected bigint radix");

  // For power of 2 radixes we know exactly how many bits each digit
  // consumes in binary.  For base 10, we have to guess, so we assume the
  // maximum bits each digit consumes.
  uint8_t maxBitsPerChar = radix == 10 ? 4 : llvh::findFirstSet(radix);

  // maxBitsPerChar * str.size() gives the exact-ish size S to represent str as
  // a BigIntDigitType, and the + 1 adds the sign bit.
  return numDigitsForSizeInBits(maxBitsPerChar * str.size() + 1) *
      BigIntDigitSizeInBits;
}

template <typename ParserT, typename StringRefT>
static std::optional<std::string> getDigitsWith(
    StringRefT src,
    uint8_t &radix,
    ParsedSign &sign,
    std::string *outError) {
  std::string bigintDigits;
  std::optional<std::string> ret;
  if (ParserT{src, radix, bigintDigits, sign, outError}.goal()) {
    ret = std::move(bigintDigits);
  }
  return ret;
}
} // namespace

std::optional<std::string> getStringIntegerLiteralDigitsAndSign(
    llvh::ArrayRef<char> src,
    uint8_t &radix,
    ParsedSign &sign,
    std::string *outError) {
  return getDigitsWith<StringIntegerLiteralParser<llvh::ArrayRef<char>>>(
      src, radix, sign, outError);
}

std::optional<std::string> getStringIntegerLiteralDigitsAndSign(
    llvh::ArrayRef<char16_t> src,
    uint8_t &radix,
    ParsedSign &sign,
    std::string *outError) {
  return getDigitsWith<StringIntegerLiteralParser<llvh::ArrayRef<char16_t>>>(
      src, radix, sign, outError);
}

namespace {
template <typename ParserT, typename StringRefT>
static std::optional<std::vector<uint8_t>> parsedBigIntFrom(
    StringRefT input,
    std::string *outError) {
  uint8_t radix;
  ParsedSign sign;
  std::optional<std::string> bigintDigits =
      getDigitsWith<ParserT>(input, radix, sign, outError);

  std::optional<std::vector<uint8_t>> result;
  if (bigintDigits) {
    llvh::APInt i(
        numBitsForBigintDigits(*bigintDigits, radix), *bigintDigits, radix);

    assert(
        i.getBitWidth() % sizeof(llvh::APInt::WordType) == 0 &&
        "Must always allocate full words");

    auto *ptr = reinterpret_cast<const uint8_t *>(i.getRawData());
    size_t size = i.getNumWords() * sizeof(llvh::APInt::WordType);

    if (sign == ParsedSign::Minus) {
      i.negate();
    }

    result = std::vector<uint8_t>(ptr, ptr + size);
  }

  return result;
}
} // namespace

std::optional<ParsedBigInt> ParsedBigInt::parsedBigIntFromStringIntegerLiteral(
    llvh::ArrayRef<char> input,
    std::string *outError) {
  std::optional<ParsedBigInt> ret;
  if (auto maybeBytes =
          parsedBigIntFrom<StringIntegerLiteralParser<llvh::ArrayRef<char>>>(
              input, outError)) {
    ret = ParsedBigInt(*maybeBytes);
  }

  return ret;
}

std::optional<ParsedBigInt> ParsedBigInt::parsedBigIntFromStringIntegerLiteral(
    llvh::ArrayRef<char16_t> input,
    std::string *outError) {
  std::optional<ParsedBigInt> ret;
  if (auto maybeBytes = parsedBigIntFrom<
          StringIntegerLiteralParser<llvh::ArrayRef<char16_t>>>(
          input, outError)) {
    ret = ParsedBigInt(*maybeBytes);
  }

  return ret;
}

std::string toString(ImmutableBigIntRef src, uint8_t radix) {
  assert(radix >= 2 && radix <= 36);

  if (compare(src, 0) == 0) {
    return "0";
  }

  const unsigned numBits = src.numDigits * BigIntDigitSizeInBytes * 8;
  const bool sign = isNegative(src);
  llvh::APInt tmp(numBits, llvh::makeArrayRef(src.digits, src.numDigits));

  if (sign) {
    // negate negative numbers, and then add a "-" to the output.
    tmp.negate();
  }

  std::string digits;

  // avoid trashing the heap by pre-allocating the largest possible string
  // returned by this function. The "1" below is to account for a possible "-"
  // sign.
  digits.reserve(1 + src.numDigits * maxCharsPerDigitInRadix(radix));
  do {
    llvh::APInt quoc;
    uint64_t rem;
    llvh::APInt::udivrem(tmp, static_cast<uint64_t>(radix), quoc, rem);

    if (rem < 10) {
      digits.push_back('0' + rem);
    } else {
      digits.push_back('a' + rem - 10);
    }

    tmp = std::move(quoc);
  } while (tmp != 0);

  if (sign) {
    digits.push_back('-');
  }

  std::reverse(digits.begin(), digits.end());
  return digits;
}

int compare(ImmutableBigIntRef lhs, ImmutableBigIntRef rhs) {
  const int kLhsGreater = 1;
  const int kRhsGreater = -kLhsGreater;

  const bool lhsSign = isNegative(lhs);
  const bool rhsSign = isNegative(rhs);

  // Different signs:
  //   1) lhsSign => !rhsSign => lhs < rhs; or
  //   2) !lhsSign => rhsSign => lhs > rhs
  if (lhsSign != rhsSign) {
    return lhsSign ? kRhsGreater : kLhsGreater;
  }

  int result;

  if (lhs.numDigits == rhs.numDigits) {
    // Defer to APInt's comparison routine.
    result = llvh::APInt::tcCompare(lhs.digits, rhs.digits, lhs.numDigits);
  } else {
    // bigints are always created using their compact representation, thus
    // their sizes (in number of digits) can be used to compare them, with
    // the bigint that has more digits being greater/less (depending on
    // their sign).
    if (lhsSign) {
      // negative numbers -- the one with fewer digits is the greater.
      result = lhs.numDigits < rhs.numDigits ? kLhsGreater : kRhsGreater;
    } else {
      // positive numbers -- the one with most digits is the greater.
      result = lhs.numDigits < rhs.numDigits ? kRhsGreater : kLhsGreater;
    }
  }

  return result;
}

int compare(ImmutableBigIntRef lhs, SignedBigIntDigitType rhs) {
  // A single BigIntDigit is enough to represent the (scalar) rhs --  given that
  // rhs is **SIGNED**, the value 0x8000000000000000 represents a negative
  // quantity, thus there's no need for an extra digit in the bigint -- there
  // would be if 0x8000000000000000 represented a positive number.
  auto *digits = reinterpret_cast<BigIntDigitType *>(&rhs);
  uint32_t numDigits = 1;
  MutableBigIntRef mr{digits, numDigits};
  // make sure mr is canonicalized, otherwise comparisons may fail -- they
  // assume all inputs to be canonical. This can happen if
  ensureCanonicalResult(mr);

  // now use the other compare, with both rhs and lhs being canonical.
  return compare(lhs, ImmutableBigIntRef{digits, numDigits});
}

namespace {
/// Helper adapter for calling getSignExtValue with *BigIntRefs.
template <typename AnyBigIntRef>
BigIntDigitType getBigIntRefSignExtValue(const AnyBigIntRef &src) {
  return src.numDigits == 0
      ? static_cast<BigIntDigitType>(0)
      : getSignExtValue<BigIntDigitType>(src.digits[src.numDigits - 1]);
}

/// Copies \p src's digits to \p dst's, which must have at least
/// as many digits as \p src. Sign-extends dst to fill \p dst.numDigits.
OperationStatus initNonCanonicalWithReadOnlyBigInt(
    MutableBigIntRef &dst,
    const ImmutableBigIntRef &src) {
  // ensure dst is large enough
  if (dst.numDigits < src.numDigits) {
    return OperationStatus::DEST_TOO_SMALL;
  }

  // now copy src digits to dst.
  const uint32_t digitsToCopy = src.numDigits;
  const uint32_t bytesToCopy = digitsToCopy * BigIntDigitSizeInBytes;
  memcpy(dst.digits, src.digits, bytesToCopy);

  // and finally size-extend dst to its size.
  const uint32_t digitsToSet = dst.numDigits - digitsToCopy;
  const uint32_t bytesToSet = digitsToSet * BigIntDigitSizeInBytes;
  const BigIntDigitType signExtValue = getBigIntRefSignExtValue(src);
  memset(dst.digits + digitsToCopy, signExtValue, bytesToSet);

  return OperationStatus::RETURNED;
}
} // namespace

uint32_t unaryMinusResultSize(ImmutableBigIntRef src) {
  // negating a non-negative number requires at most the same number of digits,
  // but it could require less; negating a negative number could require an
  // extra digit to hold the sign bit(s). Specifically, negating
  //
  // 0x8000000000000000n (which is a negative number)
  //
  // requires an extra digit:
  //
  // 0x0000000000000000 0x8000000000000000n
  return !isNegative(src) ? src.numDigits : src.numDigits + 1;
}

OperationStatus unaryMinus(MutableBigIntRef dst, ImmutableBigIntRef src) {
  auto res = initNonCanonicalWithReadOnlyBigInt(dst, src);
  if (LLVM_UNLIKELY(res != OperationStatus::RETURNED)) {
    return res;
  }

  llvh::APInt::tcNegate(dst.digits, dst.numDigits);
  ensureCanonicalResult(dst);

  assert(
      ((isNegative(ImmutableBigIntRef{dst.digits, dst.numDigits}) !=
        isNegative(src)) ||
       compare(src, 0) == 0) &&
      "unaryMinus overflow");
  return OperationStatus::RETURNED;
}

uint32_t unaryNotResultSize(ImmutableBigIntRef src) {
  // ~ 0n requires one extra digit;
  // ~ anything else requires at most src.numDigits digits.
  return std::max(1u, src.numDigits);
}

OperationStatus unaryNot(MutableBigIntRef lhs, ImmutableBigIntRef rhs) {
  auto res = initNonCanonicalWithReadOnlyBigInt(lhs, rhs);
  if (LLVM_UNLIKELY(res != OperationStatus::RETURNED)) {
    return res;
  }

  llvh::APInt::tcComplement(lhs.digits, lhs.numDigits);
  ensureCanonicalResult(lhs);
  return OperationStatus::RETURNED;
}

namespace {
using AdditiveOp = BigIntDigitType (*)(
    BigIntDigitType *,
    const BigIntDigitType *,
    BigIntDigitType,
    unsigned);
using AdditiveOpPart =
    BigIntDigitType (*)(BigIntDigitType *, BigIntDigitType, unsigned);
using AdditiveOpPostProcess = void (*)(MutableBigIntRef &);

OperationStatus additiveOperation(
    AdditiveOp op,
    AdditiveOpPart opPart,
    AdditiveOpPostProcess opPost,
    MutableBigIntRef dst,
    ImmutableBigIntRef lhs,
    ImmutableBigIntRef rhs) {
  // Requirement: lhs should have at most rhs.numDigits digits. This allows for
  // an efficient implementation of the operation as follows:
  //
  // dst = sign-ext lhs
  // dst op= rhs
  //
  // Which fits nicely into the APInt model where
  //  1. operands should have the same size; and
  //  2. operations are in-place
  assert(
      lhs.numDigits <= rhs.numDigits &&
      "lhs should have fewer digits than rhs");

  if (dst.numDigits < rhs.numDigits) {
    return OperationStatus::DEST_TOO_SMALL;
  }

  // The caller provided dst may be too big -- i.e., have more digits than
  // actually needed. Precisely rhs.numDigits + 1 digits are needed to simulate
  // infinite precision. Thus limit the result size to rhs.numDigits + 1 if dst
  // has more digits than that.
  if (rhs.numDigits + 1 < dst.numDigits) {
    dst.numDigits = rhs.numDigits + 1;
  }

  // dst = sign-ext lhs.
  auto res = initNonCanonicalWithReadOnlyBigInt(dst, lhs);
  if (LLVM_UNLIKELY(res != OperationStatus::RETURNED)) {
    return res;
  }

  // dst op= rhs
  const BigIntDigitType carryIn = 0;
  BigIntDigitType carryOut =
      (*op)(dst.digits, rhs.digits, carryIn, rhs.numDigits);
  (*opPart)(
      dst.digits + rhs.numDigits,
      carryOut + getBigIntRefSignExtValue(rhs),
      dst.numDigits - rhs.numDigits);

  // perform any post-op transformation.
  (*opPost)(dst);

  // Resize dst appropriately to ensure the resulting bigint is canonical.
  ensureCanonicalResult(dst);
  return OperationStatus::RETURNED;
}

void noopAdditiveOpPostProcess(MutableBigIntRef &) {}

void negateAdditiveOpPostProcess(MutableBigIntRef &dst) {
  llvh::APInt::tcNegate(dst.digits, dst.numDigits);
}

} // namespace

uint32_t addResultSize(ImmutableBigIntRef lhs, ImmutableBigIntRef rhs) {
  // simulate infinite precision by requiring an extra digits in the result,
  // regardless of the operands. It could be smarter -- carry will only happen
  // if the operands are the same size, and the sign_bit ^ last_bit is 1 for
  // either.
  return std::max(lhs.numDigits, rhs.numDigits) + 1;
}

OperationStatus
add(MutableBigIntRef dst, ImmutableBigIntRef lhs, ImmutableBigIntRef rhs) {
  // Addition is commutative, so lhs and rhs can be swapped at will.
  const auto &[srcWithFewerDigits, srcWithMostDigits] =
      lhs.numDigits <= rhs.numDigits ? std::make_tuple(lhs, rhs)
                                     : std::make_tuple(rhs, lhs);

  return additiveOperation(
      llvh::APInt::tcAdd,
      llvh::APInt::tcAddPart,
      noopAdditiveOpPostProcess,
      dst,
      srcWithFewerDigits,
      srcWithMostDigits);
}

uint32_t subtractResultSize(ImmutableBigIntRef lhs, ImmutableBigIntRef rhs) {
  // Simulate infinite precision by requiring an extra digit in the result,
  // regardless of the operands. It could be smarter -- carry will only happen
  // if the operands are the same size, and the sign_bit ˆ last_bit is 1 for
  // either.
  return std::max(lhs.numDigits, rhs.numDigits) + 1;
}

OperationStatus
subtract(MutableBigIntRef dst, ImmutableBigIntRef lhs, ImmutableBigIntRef rhs) {
  // Subtraction is not commutative, so the result may need to be negated, in
  // case rhs has fewer digits than lhs.
  const auto &[srcWithFewerDigits, srcWithMostDigits, postProcess] =
      lhs.numDigits <= rhs.numDigits
      ? std::make_tuple(lhs, rhs, noopAdditiveOpPostProcess)
      : std::make_tuple(rhs, lhs, negateAdditiveOpPostProcess);

  return additiveOperation(
      llvh::APInt::tcSubtract,
      llvh::APInt::tcSubtractPart,
      postProcess,
      dst,
      srcWithFewerDigits,
      srcWithMostDigits);
}
} // namespace bigint
} // namespace hermes