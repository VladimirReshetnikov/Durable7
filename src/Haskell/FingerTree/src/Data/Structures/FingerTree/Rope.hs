{-# LANGUAGE MultiParamTypeClasses #-}

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
import qualified Data.List as List
import qualified Data.Structures.FingerTree.Measured as FT
import Data.Structures.FingerTree.Measured (ViewL(..), ViewR(..))
import Data.Structures.FingerTree.Measures (Size(..))

data Chunk a = Chunk
  { chunkLength :: !Int
  , chunkItems :: [a]
  }
  deriving (Eq, Ord, Read, Show)

instance FT.Measured Size (Chunk a) where
  measure = Size . chunkLength

newtype Rope a = Rope (FT.FingerTree Size (Chunk a))
  deriving (Show)

-- Chunk layout is an implementation detail.
instance Eq a => Eq (Rope a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (Rope a) where
  compare left right = compare (toList left) (toList right)

maxChunkSize :: Int
maxChunkSize = 64

empty :: Rope a
empty = Rope FT.empty

singleton :: a -> Rope a
singleton value = Rope (FT.singleton (Chunk 1 [value]))

fromList :: [a] -> Rope a
fromList = fromChunks . chunkify

-- Every imported chunk is normalized to the public maximum. The resulting
-- finger tree is measured by element count, not by chunk count.
fromChunks :: [[a]] -> Rope a
fromChunks sourceChunks = Rope (FT.fromList ownedChunks)
  where
    ownedChunks =
      [ Chunk (length chunk) chunk
      | source <- sourceChunks
      , chunk <- chunkify source
      ]

chunks :: Rope a -> [[a]]
chunks (Rope tree) = map chunkItems (FT.toList tree)

toList :: Rope a -> [a]
toList = concat . chunks

count :: Rope a -> Int
count (Rope tree) = getSize (FT.measureTree tree)

null :: Rope a -> Bool
null (Rope tree) = FT.null tree

append :: Rope a -> Rope a -> Rope a
append (Rope left) (Rope right) = Rope (FT.append left right)

-- Endpoint edits touch only the boundary chunk. Overflow adds one new chunk;
-- the untouched middle spine is retained.
cons :: a -> Rope a -> Rope a
cons value (Rope tree) =
  case FT.viewL tree of
    EmptyL -> singleton value
    chunk@(Chunk lengthValue values) :< rest
      | lengthValue < maxChunkSize -> Rope (FT.cons (Chunk (lengthValue + 1) (value : values)) rest)
      | otherwise -> Rope (FT.cons (Chunk 1 [value]) (FT.cons chunk rest))

snoc :: Rope a -> a -> Rope a
snoc (Rope tree) value =
  case FT.viewR tree of
    EmptyR -> singleton value
    rest :> chunk@(Chunk lengthValue values)
      | lengthValue < maxChunkSize -> Rope (FT.snoc rest (Chunk (lengthValue + 1) (values ++ [value])))
      | otherwise -> Rope (FT.snoc (FT.snoc rest chunk) (Chunk 1 [value]))

index :: Int -> Rope a -> Maybe a
index position rope@(Rope tree)
  | position < 0 || position >= count rope = Nothing
  | otherwise = do
      (left, chunk, _) <- FT.split (past position) tree
      listIndex (position - treeCount left) (chunkItems chunk)

setAt :: Int -> a -> Rope a -> Maybe (Rope a)
setAt position value rope@(Rope tree)
  | position < 0 || position >= count rope = Nothing
  | otherwise = do
      (left, chunk, right) <- FT.split (past position) tree
      let local = position - treeCount left
          updated = chunk { chunkItems = replaceAt local value (chunkItems chunk) }
      pure (Rope (joinWithChunks left [updated] right))

insertAt :: Int -> a -> Rope a -> Maybe (Rope a)
insertAt position value rope@(Rope tree)
  | position < 0 || position > count rope = Nothing
  | position == count rope = Just (snoc rope value)
  | otherwise = do
      (left, chunk, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local (chunkItems chunk)
          replacements = chunksFromValues (before ++ value : after)
      pure (Rope (joinWithChunks left replacements right))

deleteAt :: Int -> Rope a -> Maybe (Rope a)
deleteAt position rope@(Rope tree)
  | position < 0 || position >= count rope = Nothing
  | otherwise = do
      (left, chunk, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local (chunkItems chunk)
          replacements = chunksFromValues (before ++ drop 1 after)
      pure (Rope (joinWithChunks left replacements right))

splitAt :: Int -> Rope a -> Maybe (Rope a, Rope a)
splitAt position rope@(Rope tree)
  | position < 0 || position > count rope = Nothing
  | position == 0 = Just (empty, rope)
  | position == count rope = Just (rope, empty)
  | otherwise = do
      (left, chunk, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local (chunkItems chunk)
          leftTree = appendChunks left (chunksFromValues before)
          rightTree = prependChunks (chunksFromValues after) right
      pure (Rope leftTree, Rope rightTree)

slice :: Int -> Int -> Rope a -> Maybe (Rope a)
slice position lengthValue rope
  | not (isValidRange position lengthValue (count rope)) = Nothing
  | otherwise = do
      (_, suffix) <- splitAt position rope
      fst <$> splitAt lengthValue suffix

removeRange :: Int -> Int -> Rope a -> Maybe (Rope a)
removeRange position lengthValue rope
  | not (isValidRange position lengthValue (count rope)) = Nothing
  | otherwise = do
      (prefix, suffix) <- splitAt position rope
      (_, tailValue) <- splitAt lengthValue suffix
      pure (append prefix tailValue)

compact :: Rope a -> Rope a
compact = fromList . toList

past :: Int -> Size -> Bool
past position (Size seen) = seen > position

treeCount :: FT.FingerTree Size (Chunk a) -> Int
treeCount = getSize . FT.measureTree

chunksFromValues :: [a] -> [Chunk a]
chunksFromValues values = [Chunk (length part) part | part <- chunkify values]

appendChunks :: FT.FingerTree Size (Chunk a) -> [Chunk a] -> FT.FingerTree Size (Chunk a)
appendChunks = List.foldl' FT.snoc

prependChunks :: [Chunk a] -> FT.FingerTree Size (Chunk a) -> FT.FingerTree Size (Chunk a)
prependChunks values tree = foldr FT.cons tree values

joinWithChunks :: FT.FingerTree Size (Chunk a) -> [Chunk a] -> FT.FingerTree Size (Chunk a) -> FT.FingerTree Size (Chunk a)
joinWithChunks left middle right = FT.append (appendChunks left middle) right

chunkify :: [a] -> [[a]]
chunkify [] = []
chunkify values =
  let (chunk, rest) = P.splitAt maxChunkSize values
   in chunk : chunkify rest

replaceAt :: Int -> a -> [a] -> [a]
replaceAt 0 value (_ : rest) = value : rest
replaceAt position value (x : rest) = x : replaceAt (position - 1) value rest
replaceAt _ _ [] = []

listIndex :: Int -> [a] -> Maybe a
listIndex 0 (value : _) = Just value
listIndex position (_ : rest)
  | position > 0 = listIndex (position - 1) rest
listIndex _ _ = Nothing

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
