{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent bit set over the nonnegative integers, stored as chunked words.
--
-- Ascending nonzero 64-bit words are held in a measured tree whose measure caches population
-- counts, which turns rank and select into a descent rather than a scan. Runs of zeros cost
-- nothing because absent words are not represented. Every operation returns a new version and
-- leaves its inputs valid, sharing unchanged structure, so an edit copies a path rather than the
-- whole collection.
module Durable7.FingerTree.PersistentChunkedBitSet
  ( PersistentChunkedBitSet
  , empty
  , fromList
  , size
  , chunkCount
  , null
  , member
  , insert
  , delete
  , rank
  , select
  , union
  , intersection
  , difference
  , symmetricDifference
  , isSubsetOf
  , overlaps
  , setEquals
  , toList
  , validStructure
  ) where

import Prelude hiding (null)

import Data.Bits ((.&.), (.|.), bit, clearBit, countTrailingZeros, popCount, setBit, testBit, xor)
import Data.Int (Int64)
import qualified Data.List as List
import Data.Word (Word64)
import Durable7.FingerTree.Measured (FingerTree, Measured(..))
import qualified Durable7.FingerTree.Measured as FT

data BitChunk = BitChunk !Int !Word64
  deriving (Eq, Show)

data BitMeasure = BitMeasure !Int !Int64 !(Maybe Int)
  deriving (Eq, Show)

instance Semigroup BitMeasure where
  BitMeasure leftChunks leftBits leftLast <> BitMeasure rightChunks rightBits rightLast =
    BitMeasure (leftChunks + rightChunks) (leftBits + rightBits) (chooseLast leftLast rightLast)
    where
      chooseLast old Nothing = old
      chooseLast _ newest = newest

instance Monoid BitMeasure where
  mempty = BitMeasure 0 0 Nothing

instance Measured BitMeasure BitChunk where
  measure (BitChunk wordIndex bits) = BitMeasure 1 (fromIntegral (popCount bits)) (Just wordIndex)

-- | A persistent bit set over the nonnegative integers, stored as ascending nonzero 64-bit words in
-- a measured tree. Cached population counts turn rank and select into a descent, and runs of
-- zeros cost nothing because absent words are not represented.
newtype PersistentChunkedBitSet = PersistentChunkedBitSet (FingerTree BitMeasure BitChunk)

-- | The empty bit set.
empty :: PersistentChunkedBitSet
empty = PersistentChunkedBitSet FT.empty

-- | A bit set holding a list's set bits, built in bulk rather than by repeated insertion.
fromList :: [Int] -> PersistentChunkedBitSet
fromList = List.foldl' (flip insert) empty

-- | Number of set bits in the bit set.
size :: PersistentChunkedBitSet -> Int64
size (PersistentChunkedBitSet chunks) = measuredPopCount (FT.measureTree chunks)

-- | How many chunks the structure holds.
chunkCount :: PersistentChunkedBitSet -> Int
chunkCount (PersistentChunkedBitSet chunks) = measuredChunkCount (FT.measureTree chunks)

-- | Whether the bit set holds no set bits.
null :: PersistentChunkedBitSet -> Bool
null (PersistentChunkedBitSet chunks) = FT.null chunks

-- | Whether the set bit is present.
member :: Int -> PersistentChunkedBitSet -> Bool
member bitIndex (PersistentChunkedBitSet chunks)
  | bitIndex < 0 || bitIndex > maxBitIndex = False
  | otherwise = case splitAtWord (bitIndex `div` 64) chunks of
      Just (_, BitChunk wordIndex bits, _) -> wordIndex == bitIndex `div` 64 && testBit bits (bitIndex `mod` 64)
      Nothing -> False

-- | A bit set containing the given set bit.
insert :: Int -> PersistentChunkedBitSet -> PersistentChunkedBitSet
insert bitIndex values@(PersistentChunkedBitSet chunks)
  | bitIndex < 0 = error "PersistentChunkedBitSet bit index must be nonnegative"
  | bitIndex > maxBitIndex = error "PersistentChunkedBitSet bit index must fit in a signed 32-bit integer"
  | otherwise =
      let wordIndex = bitIndex `div` 64
          offset = bitIndex `mod` 64
       in case splitAtWord wordIndex chunks of
            Nothing -> PersistentChunkedBitSet (FT.snoc chunks (BitChunk wordIndex (bit offset)))
            Just (left, found@(BitChunk foundIndex bits), right)
              | foundIndex == wordIndex && testBit bits offset -> values
              | foundIndex == wordIndex ->
                  PersistentChunkedBitSet (FT.append (FT.snoc left (BitChunk wordIndex (setBit bits offset))) right)
              | otherwise ->
                  PersistentChunkedBitSet
                    (FT.append (FT.snoc left (BitChunk wordIndex (bit offset))) (FT.cons found right))

-- | A bit set without that set bit.
delete :: Int -> PersistentChunkedBitSet -> PersistentChunkedBitSet
delete bitIndex values@(PersistentChunkedBitSet chunks)
  | bitIndex < 0 || bitIndex > maxBitIndex = values
  | otherwise =
      let wordIndex = bitIndex `div` 64
          offset = bitIndex `mod` 64
       in case splitAtWord wordIndex chunks of
            Just (left, BitChunk foundIndex bits, right)
              | foundIndex == wordIndex && testBit bits offset ->
                  let updated = clearBit bits offset
                      result
                        | updated == 0 = FT.append left right
                        | otherwise = FT.append (FT.snoc left (BitChunk wordIndex updated)) right
                   in PersistentChunkedBitSet result
            _ -> values

-- | Inclusive population rank.
rank :: Int -> PersistentChunkedBitSet -> Int64
rank bitIndex (PersistentChunkedBitSet chunks)
  | bitIndex < 0 = 0
  | bitIndex > maxBitIndex = measuredPopCount (FT.measureTree chunks)
  | otherwise =
      let wordIndex = bitIndex `div` 64
          offset = bitIndex `mod` 64
       in case splitAtWord wordIndex chunks of
            Nothing -> measuredPopCount (FT.measureTree chunks)
            Just (left, BitChunk foundIndex bits, _)
              | foundIndex /= wordIndex -> measuredPopCount (FT.measureTree left)
              | otherwise ->
                  let prefixMask
                        | offset == 63 = maxBound
                        | otherwise = bit (offset + 1) - 1
                   in measuredPopCount (FT.measureTree left) + fromIntegral (popCount (bits .&. prefixMask))

-- | Selects the bit at a zero-based population rank.
select :: Int64 -> PersistentChunkedBitSet -> Maybe Int
select selectedRank (PersistentChunkedBitSet chunks)
  | selectedRank < 0 || selectedRank >= measuredPopCount (FT.measureTree chunks) = Nothing
  | otherwise = do
      (before, BitChunk wordIndex bits) <- FT.locate (\measureValue -> measuredPopCount measureValue > selectedRank) chunks
      let localRank = fromIntegral (selectedRank - measuredPopCount before)
          selectedBits = clearLowest localRank bits
      pure (wordIndex * 64 + countTrailingZeros selectedBits)

-- | The set bits of both bit sets. Subtrees the operands already share are adopted whole rather
-- than re-entered.
union :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> PersistentChunkedBitSet
union = combineWords (.|.) True True

-- | The set bits present in both bit sets.
intersection :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> PersistentChunkedBitSet
intersection = combineWords (.&.) False False

-- | This bit set's set bits that are absent from the other.
difference :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> PersistentChunkedBitSet
difference = combineWords (\left right -> left .&. xor right maxBound) True False

-- | The set bits present in exactly one of the two bit sets.
symmetricDifference :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> PersistentChunkedBitSet
symmetricDifference = combineWords xor True True

-- | Whether every set bit of this bit set also occurs in the other.
isSubsetOf :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> Bool
isSubsetOf left right = null (difference left right)

-- | Whether the two bit sets share at least one set bit.
overlaps :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> Bool
overlaps left right = not (null (intersection left right))

-- | Whether both sets hold the same elements.
setEquals :: PersistentChunkedBitSet -> PersistentChunkedBitSet -> Bool
setEquals left right = chunksOf left == chunksOf right

-- | The set bits, in the bit set's own order.
toList :: PersistentChunkedBitSet -> [Int]
toList values = concatMap chunkBits (chunksOf values)
  where
    chunkBits (BitChunk wordIndex bits) = collect wordIndex bits
    collect _ 0 = []
    collect wordIndex bits =
      let offset = countTrailingZeros bits
       in wordIndex * 64 + offset : collect wordIndex (bits .&. (bits - 1))

-- | Whether the bit set's structural invariants hold. For tests and diagnostics.
validStructure :: PersistentChunkedBitSet -> Bool
validStructure values@(PersistentChunkedBitSet chunks) =
  strictlyIncreasing nonzeroChunks
    && all nonzero nonzeroChunks
    && all inRange nonzeroChunks
    && measuredChunkCount annotation == length nonzeroChunks
    && measuredPopCount annotation == fromIntegral (length (toList values))
  where
    nonzeroChunks = FT.toList chunks
    annotation = FT.measureTree chunks
    nonzero (BitChunk _ bits) = bits /= 0
    inRange (BitChunk wordIndex _) = wordIndex >= 0 && wordIndex <= maxBitIndex `div` 64
    strictlyIncreasing [] = True
    strictlyIncreasing [_] = True
    strictlyIncreasing (BitChunk left _ : rest@(BitChunk right _ : _)) = left < right && strictlyIncreasing rest

splitAtWord :: Int -> FingerTree BitMeasure BitChunk -> Maybe (FingerTree BitMeasure BitChunk, BitChunk, FingerTree BitMeasure BitChunk)
splitAtWord wordIndex = FT.split (\measureValue -> maybe False (>= wordIndex) (measuredLastWord measureValue))

combineWords
  :: (Word64 -> Word64 -> Word64)
  -> Bool
  -> Bool
  -> PersistentChunkedBitSet
  -> PersistentChunkedBitSet
  -> PersistentChunkedBitSet
combineWords operation keepLeftOnly keepRightOnly left right =
  let merged = merge (chunksOf left) (chunksOf right)
   in if merged == chunksOf left then left else PersistentChunkedBitSet (FT.fromList merged)
  where
    merge [] remaining
      | keepRightOnly = remaining
      | otherwise = []
    merge remaining []
      | keepLeftOnly = remaining
      | otherwise = []
    merge leftValues@(leftChunk@(BitChunk leftIndex leftBits) : leftRest)
          rightValues@(rightChunk@(BitChunk rightIndex rightBits) : rightRest)
      | leftIndex < rightIndex =
          if keepLeftOnly then leftChunk : merge leftRest rightValues else merge leftRest rightValues
      | rightIndex < leftIndex =
          if keepRightOnly then rightChunk : merge leftValues rightRest else merge leftValues rightRest
      | otherwise =
          let combined = operation leftBits rightBits
           in if combined == 0
                then merge leftRest rightRest
                else BitChunk leftIndex combined : merge leftRest rightRest

chunksOf :: PersistentChunkedBitSet -> [BitChunk]
chunksOf (PersistentChunkedBitSet chunks) = FT.toList chunks

clearLowest :: Int -> Word64 -> Word64
clearLowest 0 bits = bits
clearLowest count bits = clearLowest (count - 1) (bits .&. (bits - 1))

measuredChunkCount :: BitMeasure -> Int
measuredChunkCount (BitMeasure count _ _) = count

measuredPopCount :: BitMeasure -> Int64
measuredPopCount (BitMeasure _ count _) = count

maxBitIndex :: Int
maxBitIndex = 2147483647

measuredLastWord :: BitMeasure -> Maybe Int
measuredLastWord (BitMeasure _ _ lastWord) = lastWord
