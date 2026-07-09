module Data.Structures.FingerTree.Rope
  ( Rope
  , Chunk(..)
  , maxChunkSize
  , empty
  , singleton
  , fromList
  , fromChunks
  , chunks
  , toList
  , count
  , null
  , append
  , cons
  , snoc
  , index
  , setAt
  , insertAt
  , deleteAt
  , splitAt
  , slice
  , removeRange
  , compact
  ) where

import Prelude hiding (null, splitAt)

import qualified Prelude as P
import qualified Data.Structures.FingerTree.Deque as Deque

data Chunk a = Chunk
  { chunkLength :: !Int
  , chunkItems :: [a]
  }
  deriving (Eq, Ord, Read, Show)

data Rope a = Rope !Int !(Deque.Deque (Chunk a))
  deriving (Show)

-- Extensional equality: chunk layout is an implementation detail, so two
-- ropes are equal exactly when their element sequences are.
instance Eq a => Eq (Rope a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (Rope a) where
  compare left right = compare (toList left) (toList right)

maxChunkSize :: Int
maxChunkSize = 64

empty :: Rope a
empty = Rope 0 Deque.empty

singleton :: a -> Rope a
singleton value = fromList [value]

fromList :: [a] -> Rope a
fromList values = fromChunks (chunkify values)

-- Imported chunks are re-chunked so no stored chunk exceeds maxChunkSize,
-- preserving the module's chunk invariant for arbitrary caller layouts.
fromChunks :: [[a]] -> Rope a
fromChunks sourceChunks = Rope total (Deque.fromList ownedChunks)
  where
    ownedChunks =
      [ Chunk (length chunk) chunk
      | source <- sourceChunks
      , chunk <- chunkify source
      ]
    total = sum (map chunkLength ownedChunks)

chunks :: Rope a -> [[a]]
chunks (Rope _ values) = map chunkItems (Deque.toList values)

toList :: Rope a -> [a]
toList rope = concat (chunks rope)

count :: Rope a -> Int
count (Rope total _) = total

null :: Rope a -> Bool
null rope = count rope == 0

append :: Rope a -> Rope a -> Rope a
append (Rope leftCount left) (Rope rightCount right) = Rope (leftCount + rightCount) (Deque.append left right)

cons :: a -> Rope a -> Rope a
cons value rope = fromList (value : toList rope)

snoc :: Rope a -> a -> Rope a
snoc rope value = fromList (toList rope ++ [value])

index :: Int -> Rope a -> Maybe a
index position rope
  | position < 0 || position >= count rope = Nothing
  | otherwise = Just (toList rope !! position)

setAt :: Int -> a -> Rope a -> Maybe (Rope a)
setAt position value rope
  | position < 0 || position >= count rope = Nothing
  | otherwise = Just (fromList (replaceAt position value (toList rope)))

insertAt :: Int -> a -> Rope a -> Maybe (Rope a)
insertAt position value rope
  | position < 0 || position > count rope = Nothing
  | otherwise = Just (fromList (prefix ++ [value] ++ suffix))
  where
    (prefix, suffix) = P.splitAt position (toList rope)

deleteAt :: Int -> Rope a -> Maybe (Rope a)
deleteAt position rope
  | position < 0 || position >= count rope = Nothing
  | otherwise = Just (fromList (prefix ++ drop 1 suffix))
  where
    (prefix, suffix) = P.splitAt position (toList rope)

splitAt :: Int -> Rope a -> Maybe (Rope a, Rope a)
splitAt position rope
  | position < 0 || position > count rope = Nothing
  | otherwise = Just (fromList prefix, fromList suffix)
  where
    (prefix, suffix) = P.splitAt position (toList rope)

slice :: Int -> Int -> Rope a -> Maybe (Rope a)
slice position lengthValue rope
  | not (isValidRange position lengthValue (count rope)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList rope))))

removeRange :: Int -> Int -> Rope a -> Maybe (Rope a)
removeRange position lengthValue rope
  | not (isValidRange position lengthValue (count rope)) = Nothing
  | otherwise = Just (fromList (prefix ++ drop lengthValue suffix))
  where
    (prefix, suffix) = P.splitAt position (toList rope)

compact :: Rope a -> Rope a
compact = fromList . toList

chunkify :: [a] -> [[a]]
chunkify [] = []
chunkify values =
  let (chunk, rest) = P.splitAt maxChunkSize values
   in chunk : chunkify rest

replaceAt :: Int -> a -> [a] -> [a]
replaceAt 0 value (_ : rest) = value : rest
replaceAt position value (x : rest) = x : replaceAt (position - 1) value rest
replaceAt _ _ [] = []

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
