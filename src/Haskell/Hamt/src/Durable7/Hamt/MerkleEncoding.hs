{-# LANGUAGE BangPatterns #-}

-- | Canonical codecs, SHA-256 digests, and policy framing for Merkle search trees.
module Durable7.Hamt.MerkleEncoding
  ( MerkleCodec(..)
  , MerkleCodecError(..)
  , int32MerkleCodec
  , int64MerkleCodec
  , nullableUtf8MerkleCodec
  , nullableBytesMerkleCodec
  , Rfc4122Guid
  , makeRfc4122Guid
  , rfc4122GuidBytes
  , rfc4122GuidMerkleCodec
  , MerkleDigest
  , digestBytes
  , digestHex
  , parseDigestBytes
  , parseDigestHex
  , hashBytes
  , MerkleSearchTreePolicy
  , MerklePolicyError(..)
  , merkleAlgorithmId
  , makeMerkleSearchTreePolicy
  , naturalMerkleSearchTreePolicy
  , merklePolicyId
  , merkleKeyCodec
  , merkleValueCodec
  , compareMerkleKeys
  , merkleDomainDigest
  , merkleEmptyDigest
  , compatibleMerklePolicies
  , hashMerkleKeyBytes
  , hashMerkleKey
  , merkleLevel
  , int32BigEndian
  , int64BigEndian
  , word32BigEndian
  , word64BigEndian
  ) where

import Data.Array (Array, (!), listArray)
import Data.Bits
  ( complement
  , rotateR
  , shiftL
  , shiftR
  , xor
  , (.&.)
  , (.|.)
  )
import qualified Data.ByteString as ByteString
import Data.ByteString (ByteString)
import qualified Data.Text as Text
import qualified Data.Text.Encoding as TextEncoding
import Data.Char (isSpace)
import Data.Int (Int32, Int64)
import Data.Word (Word8, Word32, Word64)
import Numeric (showHex)

-- | An injective, versioned canonical representation for one value type.
--
-- Decoders must consume exactly one complete encoding. The persistence layer additionally
-- re-encodes decoded values and rejects any byte sequence that does not round-trip exactly.
data MerkleCodec a = MerkleCodec
  { merkleEncodingId :: !String
  , encodeMerkleValue :: a -> Either MerkleCodecError ByteString
  , decodeMerkleValue :: ByteString -> Either MerkleCodecError a
  }

-- | A malformed, noncanonical, or otherwise rejected codec value.
newtype MerkleCodecError = MerkleCodecError { merkleCodecErrorMessage :: String }
  deriving (Eq, Show)

-- | Canonical signed 32-bit big-endian codec (@i32-be-v1@).
int32MerkleCodec :: MerkleCodec Int32
int32MerkleCodec = MerkleCodec
  { merkleEncodingId = "i32-be-v1"
  , encodeMerkleValue = Right . int32BigEndian
  , decodeMerkleValue = \bytes ->
      if ByteString.length bytes == 4
        then Right (fromIntegral (decodeWord32 bytes))
        else Left (MerkleCodecError "invalid i32-be-v1 encoding: expected exactly four bytes")
  }

-- | Canonical signed 64-bit big-endian codec (@i64-be-v1@).
int64MerkleCodec :: MerkleCodec Int64
int64MerkleCodec = MerkleCodec
  { merkleEncodingId = "i64-be-v1"
  , encodeMerkleValue = Right . int64BigEndian
  , decodeMerkleValue = \bytes ->
      if ByteString.length bytes == 8
        then Right (fromIntegral (decodeWord64 bytes))
        else Left (MerkleCodecError "invalid i64-be-v1 encoding: expected exactly eight bytes")
  }

-- | Canonical nullable UTF-8 codec (@nullable-utf8-v1@).
nullableUtf8MerkleCodec :: MerkleCodec (Maybe String)
nullableUtf8MerkleCodec = MerkleCodec
  { merkleEncodingId = "nullable-utf8-v1"
  , encodeMerkleValue = \value -> case value of
      Nothing -> Right (ByteString.singleton 0)
      Just text -> do
        bytes <- encodeUtf8Strict "invalid nullable-utf8-v1 value" text
        Right (ByteString.cons 1 bytes)
  , decodeMerkleValue = \bytes -> case ByteString.uncons bytes of
      Nothing -> Left (MerkleCodecError "invalid nullable-utf8-v1 encoding: nullable tag is missing")
      Just (0, payload)
        | ByteString.null payload -> Right Nothing
        | otherwise -> Left (MerkleCodecError "invalid nullable-utf8-v1 encoding: null has trailing bytes")
      Just (1, payload) -> case TextEncoding.decodeUtf8' payload of
        Left _ -> Left (MerkleCodecError "invalid nullable-utf8-v1 encoding: payload is not canonical UTF-8")
        Right text -> Right (Just (Text.unpack text))
      Just _ -> Left (MerkleCodecError "invalid nullable-utf8-v1 encoding: nullable tag must be zero or one")
  }

-- | Canonical nullable byte-string codec (@nullable-bytes-v1@).
nullableBytesMerkleCodec :: MerkleCodec (Maybe ByteString)
nullableBytesMerkleCodec = MerkleCodec
  { merkleEncodingId = "nullable-bytes-v1"
  , encodeMerkleValue = Right . maybe (ByteString.singleton 0) (ByteString.cons 1)
  , decodeMerkleValue = \bytes -> case ByteString.uncons bytes of
      Nothing -> Left (MerkleCodecError "invalid nullable-bytes-v1 encoding: nullable tag is missing")
      Just (0, payload)
        | ByteString.null payload -> Right Nothing
        | otherwise -> Left (MerkleCodecError "invalid nullable-bytes-v1 encoding: null has trailing bytes")
      Just (1, payload) -> Right (Just payload)
      Just _ -> Left (MerkleCodecError "invalid nullable-bytes-v1 encoding: nullable tag must be zero or one")
  }

-- | An exact RFC-4122/network-order 16-byte GUID value.
newtype Rfc4122Guid = Rfc4122Guid ByteString
  deriving (Eq, Ord)

instance Show Rfc4122Guid where
  show = digestLikeHex . rfc4122GuidBytes

-- | Creates a GUID only when exactly sixteen network-order bytes are supplied.
makeRfc4122Guid :: ByteString -> Either MerkleCodecError Rfc4122Guid
makeRfc4122Guid bytes
  | ByteString.length bytes == 16 = Right (Rfc4122Guid bytes)
  | otherwise = Left (MerkleCodecError "an RFC-4122 GUID requires exactly sixteen bytes")

-- | Returns the exact RFC-4122/network-order bytes.
rfc4122GuidBytes :: Rfc4122Guid -> ByteString
rfc4122GuidBytes (Rfc4122Guid bytes) = bytes

-- | Canonical RFC-4122/network-order GUID codec (@guid-rfc4122-v1@).
rfc4122GuidMerkleCodec :: MerkleCodec Rfc4122Guid
rfc4122GuidMerkleCodec = MerkleCodec
  { merkleEncodingId = "guid-rfc4122-v1"
  , encodeMerkleValue = Right . rfc4122GuidBytes
  , decodeMerkleValue = makeRfc4122Guid
  }

-- | A 256-bit SHA-256 content digest.
newtype MerkleDigest = MerkleDigest ByteString
  deriving (Eq, Ord)

instance Show MerkleDigest where
  show = digestHex

-- | Returns the exact 32 digest bytes.
digestBytes :: MerkleDigest -> ByteString
digestBytes (MerkleDigest bytes) = bytes

-- | Formats a digest as exactly 64 lowercase hexadecimal characters.
digestHex :: MerkleDigest -> String
digestHex = digestLikeHex . digestBytes

-- | Parses exactly 32 binary digest bytes.
parseDigestBytes :: ByteString -> Either String MerkleDigest
parseDigestBytes bytes
  | ByteString.length bytes == 32 = Right (MerkleDigest bytes)
  | otherwise = Left ("a Merkle digest must contain exactly 32 bytes; received " ++ show (ByteString.length bytes))

-- | Parses exactly 64 hexadecimal characters, accepting either case.
parseDigestHex :: String -> Either String MerkleDigest
parseDigestHex text
  | length text /= 64 = Left ("a Merkle digest must contain exactly 64 hexadecimal characters; received " ++ show (length text))
  | otherwise = MerkleDigest . ByteString.pack <$> traverseBytePairs 0 text
  where
    traverseBytePairs :: Int -> String -> Either String [Word8]
    traverseBytePairs _ [] = Right []
    traverseBytePairs !index (high : low : rest) = do
      highValue <- maybe (Left ("invalid hexadecimal character at index " ++ show index)) Right (hexValue high)
      lowValue <- maybe (Left ("invalid hexadecimal character at index " ++ show (index + 1))) Right (hexValue low)
      ((highValue `shiftL` 4 .|. lowValue) :) <$> traverseBytePairs (index + 2) rest
    traverseBytePairs _ _ = Left "invalid hexadecimal digest length"

-- | Exact hashing and block-format identifier shared by every language port.
merkleAlgorithmId :: String
merkleAlgorithmId = "mst-sha256-b16-v2"

-- | Deterministic comparison, canonical codecs, and SHA-256 domain for one map family.
data MerkleSearchTreePolicy k v = MerkleSearchTreePolicy
  { merklePolicyId :: !String
  , compareMerkleKeys :: k -> k -> Ordering
  , merkleKeyCodec :: !(MerkleCodec k)
  , merkleValueCodec :: !(MerkleCodec v)
  , merkleDomainDigest :: !MerkleDigest
  , merkleEmptyDigest :: !MerkleDigest
  }

instance Show (MerkleSearchTreePolicy k v) where
  show policy = "MerkleSearchTreePolicy { policyId = " ++ show (merklePolicyId policy)
    ++ ", algorithmId = " ++ show merkleAlgorithmId
    ++ ", domainDigest = " ++ digestHex (merkleDomainDigest policy) ++ " }"

-- | A deterministic policy could not be constructed.
data MerklePolicyError
  = EmptyMerklePolicyId
  | InvalidMerkleEncodingId !String !String
  | InvalidMerklePolicyUtf8 !String
  deriving (Eq, Show)

-- | Creates an explicit deterministic policy.
makeMerkleSearchTreePolicy
  :: String
  -> (k -> k -> Ordering)
  -> MerkleCodec k
  -> MerkleCodec v
  -> Either MerklePolicyError (MerkleSearchTreePolicy k v)
makeMerkleSearchTreePolicy policyId comparer keyCodec valueCodec = do
  if null policyId || all isSpace policyId
    then Left EmptyMerklePolicyId
    else Right ()
  validateEncodingId "key codec" (merkleEncodingId keyCodec)
  validateEncodingId "value codec" (merkleEncodingId valueCodec)
  algorithmBytes <- policyUtf8 "algorithm id" merkleAlgorithmId
  policyBytes <- policyUtf8 "policy id" policyId
  keyIdBytes <- policyUtf8 "key codec id" (merkleEncodingId keyCodec)
  valueIdBytes <- policyUtf8 "value codec id" (merkleEncodingId valueCodec)
  let domain = hashFramed 0x50 [algorithmBytes, policyBytes, keyIdBytes, valueIdBytes]
      emptyManifest = ByteString.concat [ascii "MST2", ByteString.singleton 0, digestBytes domain]
      emptyHash = hashBytes emptyManifest
  Right MerkleSearchTreePolicy
    { merklePolicyId = policyId
    , compareMerkleKeys = comparer
    , merkleKeyCodec = keyCodec
    , merkleValueCodec = valueCodec
    , merkleDomainDigest = domain
    , merkleEmptyDigest = emptyHash
    }

-- | Creates a deterministic policy using ordinary 'Ord' key comparison.
naturalMerkleSearchTreePolicy
  :: Ord k
  => String
  -> MerkleCodec k
  -> MerkleCodec v
  -> Either MerklePolicyError (MerkleSearchTreePolicy k v)
naturalMerkleSearchTreePolicy policyId = makeMerkleSearchTreePolicy policyId compare

-- | Whether two policies name the same algorithm/policy/codec domain.
compatibleMerklePolicies :: MerkleSearchTreePolicy k v -> MerkleSearchTreePolicy k v -> Bool
compatibleMerklePolicies left right = merkleDomainDigest left == merkleDomainDigest right

-- | Hashes canonical encoded key bytes within the policy domain.
hashMerkleKeyBytes :: MerkleSearchTreePolicy k v -> ByteString -> MerkleDigest
hashMerkleKeyBytes policy keyBytes = hashFramed 0x4b [digestBytes (merkleDomainDigest policy), keyBytes]

-- | Encodes and hashes one key.
hashMerkleKey :: MerkleSearchTreePolicy k v -> k -> Either MerkleCodecError MerkleDigest
hashMerkleKey policy key = hashMerkleKeyBytes policy <$> encodeMerkleValue (merkleKeyCodec policy) key

-- | Counts leading zero base-16 digits, yielding a level from zero through 64.
merkleLevel :: MerkleDigest -> Word8
merkleLevel digest = go 0 (ByteString.unpack (digestBytes digest))
  where
    go !level [] = level
    go !level (byte : rest)
      | byte .&. 0xf0 /= 0 = level
      | byte .&. 0x0f /= 0 = level + 1
      | otherwise = go (level + 2) rest

-- | Computes SHA-256 over exact bytes.
hashBytes :: ByteString -> MerkleDigest
hashBytes = MerkleDigest . sha256

-- | Encodes a signed 32-bit integer in two's-complement big-endian order.
int32BigEndian :: Int32 -> ByteString
int32BigEndian = word32BigEndian . fromIntegral

-- | Encodes a signed 64-bit integer in two's-complement big-endian order.
int64BigEndian :: Int64 -> ByteString
int64BigEndian = word64BigEndian . fromIntegral

-- | Encodes an unsigned 32-bit integer in big-endian order.
word32BigEndian :: Word32 -> ByteString
word32BigEndian value = ByteString.pack
  [ fromIntegral (value `shiftR` 24)
  , fromIntegral (value `shiftR` 16)
  , fromIntegral (value `shiftR` 8)
  , fromIntegral value
  ]

-- | Encodes an unsigned 64-bit integer in big-endian order.
word64BigEndian :: Word64 -> ByteString
word64BigEndian value = ByteString.pack
  [ fromIntegral (value `shiftR` 56)
  , fromIntegral (value `shiftR` 48)
  , fromIntegral (value `shiftR` 40)
  , fromIntegral (value `shiftR` 32)
  , fromIntegral (value `shiftR` 24)
  , fromIntegral (value `shiftR` 16)
  , fromIntegral (value `shiftR` 8)
  , fromIntegral value
  ]

validateEncodingId :: String -> String -> Either MerklePolicyError ()
validateEncodingId field encodingId
  | not (null encodingId)
      && trimWhitespace encodingId == encodingId
      && validVersionSuffix encodingId = Right ()
  | otherwise = Left (InvalidMerkleEncodingId field encodingId)

validVersionSuffix :: String -> Bool
validVersionSuffix text = case lastVersionMarker text of
  Nothing -> False
  Just index -> index > 0 && index + 2 < length text && all isAsciiDigit (drop (index + 2) text)

lastVersionMarker :: String -> Maybe Int
lastVersionMarker text = foldl' chooseVersion Nothing [0 .. length text - 2]
  where
    chooseVersion current index
      | take 2 (drop index text) == "-v" = Just index
      | otherwise = current

policyUtf8 :: String -> String -> Either MerklePolicyError ByteString
policyUtf8 field text = case encodeUtf8Strict ("invalid " ++ field) text of
  Left _ -> Left (InvalidMerklePolicyUtf8 field)
  Right bytes -> Right bytes

encodeUtf8Strict :: String -> String -> Either MerkleCodecError ByteString
encodeUtf8Strict context text
  | any isSurrogate text = Left (MerkleCodecError (context ++ ": surrogate code point is not a Unicode scalar value"))
  | otherwise = Right (TextEncoding.encodeUtf8 (Text.pack text))

isSurrogate :: Char -> Bool
isSurrogate value = value >= '\xD800' && value <= '\xDFFF'

isAsciiDigit :: Char -> Bool
isAsciiDigit value = value >= '0' && value <= '9'

trimWhitespace :: String -> String
trimWhitespace = reverse . dropWhile isSpace . reverse . dropWhile isSpace

ascii :: String -> ByteString
ascii = ByteString.pack . map (fromIntegral . fromEnum)

hashFramed :: Word8 -> [ByteString] -> MerkleDigest
hashFramed tag fields = hashBytes (ByteString.concat (ByteString.singleton tag : concatMap frame fields))
  where
    frame bytes = [int32BigEndian (fromIntegral (ByteString.length bytes)), bytes]

digestLikeHex :: ByteString -> String
digestLikeHex = concatMap byteHex . ByteString.unpack
  where
    byteHex byte = case showHex byte "" of
      [digit] -> ['0', digit]
      digits -> digits

hexValue :: Char -> Maybe Word8
hexValue value
  | value >= '0' && value <= '9' = Just (fromIntegral (fromEnum value - fromEnum '0'))
  | value >= 'a' && value <= 'f' = Just (fromIntegral (fromEnum value - fromEnum 'a' + 10))
  | value >= 'A' && value <= 'F' = Just (fromIntegral (fromEnum value - fromEnum 'A' + 10))
  | otherwise = Nothing

decodeWord32 :: ByteString -> Word32
decodeWord32 bytes = foldl' (\value byte -> value `shiftL` 8 .|. fromIntegral byte) 0 (ByteString.unpack bytes)

decodeWord64 :: ByteString -> Word64
decodeWord64 bytes = foldl' (\value byte -> value `shiftL` 8 .|. fromIntegral byte) 0 (ByteString.unpack bytes)

-- A small dependency-free SHA-256 implementation. The schedule uses a lazy boxed array so every
-- word is computed once while retaining the recurrence in its specification form.
sha256 :: ByteString -> ByteString
sha256 input = ByteString.concat (map word32BigEndian finalState)
  where
    bitLength = fromIntegral (ByteString.length input) * 8 :: Word64
    remainder = (ByteString.length input + 1) `mod` 64
    zeroCount = (56 - remainder) `mod` 64
    padded = ByteString.concat
      [ input
      , ByteString.singleton 0x80
      , ByteString.replicate zeroCount 0
      , word64BigEndian bitLength
      ]
    chunks = chunkBytes 64 padded
    finalState = foldl' compress initialHash chunks

initialHash :: [Word32]
initialHash =
  [ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a
  , 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  ]

roundConstants :: Array Int Word32
roundConstants = listArray (0, 63)
  [ 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
  , 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
  , 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
  , 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
  , 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
  , 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
  , 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
  , 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  ]

compress :: [Word32] -> ByteString -> [Word32]
compress [h0, h1, h2, h3, h4, h5, h6, h7] chunk =
  zipWith (+) [h0, h1, h2, h3, h4, h5, h6, h7] [a, b, c, d, e, f, g, h]
  where
    baseWords = map decodeWord32 (chunkBytes 4 chunk)
    schedule :: Array Int Word32
    schedule = listArray (0, 63)
      (baseWords ++
        [ smallSigma1 (schedule ! (index - 2))
            + schedule ! (index - 7)
            + smallSigma0 (schedule ! (index - 15))
            + schedule ! (index - 16)
        | index <- [16 .. 63]
        ])
    (!a, !b, !c, !d, !e, !f, !g, !h) = foldl' roundStep (h0, h1, h2, h3, h4, h5, h6, h7) [0 .. 63]
    roundStep (!ra, !rb, !rc, !rd, !re, !rf, !rg, !rh) index =
      let !temporary1 = rh + bigSigma1 re + choose re rf rg + roundConstants ! index + schedule ! index
          !temporary2 = bigSigma0 ra + majority ra rb rc
       in (temporary1 + temporary2, ra, rb, rc, rd + temporary1, re, rf, rg)
compress _ _ = error "internal SHA-256 state must contain eight words"

choose :: Word32 -> Word32 -> Word32 -> Word32
choose x y z = (x .&. y) `xor` (complement x .&. z)

majority :: Word32 -> Word32 -> Word32 -> Word32
majority x y z = (x .&. y) `xor` (x .&. z) `xor` (y .&. z)

bigSigma0, bigSigma1, smallSigma0, smallSigma1 :: Word32 -> Word32
bigSigma0 value = rotateR value 2 `xor` rotateR value 13 `xor` rotateR value 22
bigSigma1 value = rotateR value 6 `xor` rotateR value 11 `xor` rotateR value 25
smallSigma0 value = rotateR value 7 `xor` rotateR value 18 `xor` shiftR value 3
smallSigma1 value = rotateR value 17 `xor` rotateR value 19 `xor` shiftR value 10

chunkBytes :: Int -> ByteString -> [ByteString]
chunkBytes width bytes
  | ByteString.null bytes = []
  | otherwise = let (prefix, suffix) = ByteString.splitAt width bytes in prefix : chunkBytes width suffix
