{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}

module Data.Structures.FingerTree.Rope
  ( Rope
  , RopeCursor
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
  , insertRangeAt
  , deleteAt
  , splitAt
  , slice
  , removeRange
  , compact
  , cursor
  , cursorAt
  , cursorCount
  , cursorNull
  , cursorPosition
  , isAtStart
  , isAtEnd
  , peekPrevious
  , peekNext
  , movePrevious
  , moveNext
  , seek
  , insert
  , insertRange
  , deletePrevious
  , deleteNext
  , replaceNext
  , snapshot
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

-- | An immutable positional editing cursor over one retained 'Rope' snapshot.
--
-- The strict position is a validated gap in @0 .. cursorCount@. Movement and
-- edits return new cursors, so retained values branch independently. This is a
-- snapshot-plus-gap semantic checkpoint, not the C# focused zipper.
data RopeCursor a = RopeCursor !(Rope a) !Int

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
fromList values = fromChunks [values]

-- Every imported chunk is normalized to the public maximum. The resulting
-- finger tree is measured by element count, not by chunk count.
fromChunks :: [[a]] -> Rope a
fromChunks sourceChunks = Rope (List.foldl' appendOwnedChunk FT.empty ownedChunks)
  where
    ownedChunks =
      [ Chunk (length chunk) chunk
      | source <- sourceChunks
      , chunk <- chunkify source
      ]
    appendOwnedChunk tree chunk =
      checkedAdd (treeCount tree) (chunkLength chunk) `seq` FT.snoc tree chunk

chunks :: Rope a -> [[a]]
chunks (Rope tree) = map chunkItems (FT.toList tree)

toList :: Rope a -> [a]
toList = concat . chunks

count :: Rope a -> Int
count (Rope tree) = getSize (FT.measureTree tree)

null :: Rope a -> Bool
null (Rope tree) = FT.null tree

append :: Rope a -> Rope a -> Rope a
append leftRope@(Rope left) rightRope@(Rope right) =
  checkedAdd (count leftRope) (count rightRope) `seq` Rope (FT.append left right)

-- Endpoint edits touch only the boundary chunk. Overflow adds one new chunk;
-- the untouched middle spine is retained.
cons :: a -> Rope a -> Rope a
cons value rope@(Rope tree) =
  checkedAdd (count rope) 1 `seq`
  case FT.viewL tree of
    EmptyL -> singleton value
    chunk@(Chunk lengthValue values) :< rest
      | lengthValue < maxChunkSize -> Rope (FT.cons (Chunk (lengthValue + 1) (value : values)) rest)
      | otherwise -> Rope (FT.cons (Chunk 1 [value]) (FT.cons chunk rest))

snoc :: Rope a -> a -> Rope a
snoc rope@(Rope tree) value =
  checkedAdd (count rope) 1 `seq`
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
  | otherwise =
      checkedAdd (count rope) 1 `seq`
      if position == count rope
        then Just (snoc rope value)
        else do
          (left, chunk, right) <- FT.split (past position) tree
          let local = position - treeCount left
              (before, after) = P.splitAt local (chunkItems chunk)
              replacements = chunksFromValues (before ++ value : after)
          pure (Rope (joinWithChunks left replacements right))

-- | Inserts a list at a validated gap. An empty list returns the original
-- rope. A valid insertion whose total count cannot fit in 'Int' throws before
-- constructing a result; all inputs remain reusable.
insertRangeAt :: Int -> [a] -> Rope a -> Maybe (Rope a)
insertRangeAt position values rope
  | position < 0 || position > count rope = Nothing
  | otherwise = insertRopeAt position (fromList values) rope

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

-- | Creates a cursor at the gap before the first element. O(1).
cursor :: Rope a -> RopeCursor a
cursor rope = RopeCursor rope 0

-- | Creates a cursor at a gap in @0 .. count rope@. O(1).
cursorAt :: Int -> Rope a -> Maybe (RopeCursor a)
cursorAt position rope
  | position < 0 || position > count rope = Nothing
  | otherwise = Just (RopeCursor rope position)

-- | Returns the retained snapshot's element count. O(1).
cursorCount :: RopeCursor a -> Int
cursorCount (RopeCursor rope _) = count rope

-- | Reports whether the retained snapshot is empty. O(1).
cursorNull :: RopeCursor a -> Bool
cursorNull (RopeCursor rope _) = null rope

-- | Returns the validated gap position. O(1).
cursorPosition :: RopeCursor a -> Int
cursorPosition (RopeCursor _ position) = position

-- | Reports whether the gap precedes the first element. O(1).
isAtStart :: RopeCursor a -> Bool
isAtStart cursorValue = cursorPosition cursorValue == 0

-- | Reports whether the gap follows the last element. O(1).
isAtEnd :: RopeCursor a -> Bool
isAtEnd cursorValue = cursorPosition cursorValue == cursorCount cursorValue

-- | Returns the element immediately before the gap, if one exists. O(log n).
peekPrevious :: RopeCursor a -> Maybe a
peekPrevious cursorValue@(RopeCursor rope position)
  | isAtStart cursorValue = Nothing
  | otherwise = index (position - 1) rope

-- | Returns the element immediately after the gap, if one exists. O(log n).
peekNext :: RopeCursor a -> Maybe a
peekNext cursorValue@(RopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = index position rope

-- | Moves one gap toward the start, or returns 'Nothing' at the start. O(1).
movePrevious :: RopeCursor a -> Maybe (RopeCursor a)
movePrevious cursorValue@(RopeCursor rope position)
  | isAtStart cursorValue = Nothing
  | otherwise = Just (RopeCursor rope (position - 1))

-- | Moves one gap toward the end, or returns 'Nothing' at the end. O(1).
moveNext :: RopeCursor a -> Maybe (RopeCursor a)
moveNext cursorValue@(RopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = Just (RopeCursor rope (position + 1))

-- | Moves to a validated absolute gap. Seeking to the current gap preserves
-- the unchanged cursor value and retained snapshot. O(1).
seek :: Int -> RopeCursor a -> Maybe (RopeCursor a)
seek position cursorValue@(RopeCursor rope _)
  | position < 0 || position > cursorCount cursorValue = Nothing
  | position == cursorPosition cursorValue = Just cursorValue
  | otherwise = Just (RopeCursor rope position)

-- | Inserts one element at the gap and returns the gap after it. O(log n)
-- plus bounded chunk work.
insert :: a -> RopeCursor a -> RopeCursor a
insert value (RopeCursor rope position) =
  let nextPosition = checkedAdd position 1
   in case insertAt position value rope of
        Just edited -> RopeCursor edited nextPosition
        Nothing -> error "Data.Structures.FingerTree.Rope.insert: invalid cursor invariant"

-- | Inserts a list at the gap in order and returns the gap after it. An empty
-- list preserves the unchanged cursor value and retained snapshot. Inserting @m@ elements costs
-- O(m + log n).
insertRange :: [a] -> RopeCursor a -> RopeCursor a
insertRange [] cursorValue = cursorValue
insertRange values (RopeCursor rope position) =
  let !middle = fromList values
      !added = count middle
      !nextPosition = checkedAdd position added
   in case insertRopeAt position middle rope of
        Just edited -> RopeCursor edited nextPosition
        Nothing -> error "Data.Structures.FingerTree.Rope.insertRange: invalid cursor invariant"

-- | Deletes the element before the gap and moves left, or returns 'Nothing'
-- at the start. O(log n) plus bounded chunk work.
deletePrevious :: RopeCursor a -> Maybe (RopeCursor a)
deletePrevious cursorValue@(RopeCursor rope position)
  | isAtStart cursorValue = Nothing
  | otherwise = RopeCursor <$> deleteAt (position - 1) rope <*> pure (position - 1)

-- | Deletes the element after the gap without moving it, or returns 'Nothing'
-- at the end. O(log n) plus bounded chunk work.
deleteNext :: RopeCursor a -> Maybe (RopeCursor a)
deleteNext cursorValue@(RopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = RopeCursor <$> deleteAt position rope <*> pure position

-- | Unconditionally replaces the element after the gap without moving it, or
-- returns 'Nothing' at the end. No element equality is required or consulted.
replaceNext :: a -> RopeCursor a -> Maybe (RopeCursor a)
replaceNext value cursorValue@(RopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = RopeCursor <$> setAt position value rope <*> pure position

-- | Returns the exact immutable rope snapshot retained by the cursor. O(1).
snapshot :: RopeCursor a -> Rope a
snapshot (RopeCursor rope _) = rope

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

insertRopeAt :: Int -> Rope a -> Rope a -> Maybe (Rope a)
insertRopeAt _ middle rope | null middle = Just rope
insertRopeAt position middle rope =
  let added = count middle
   in checkedAdd (count rope) added `seq`
        case splitAt position rope of
          Just (left, right) -> Just (append (append left middle) right)
          Nothing -> Nothing

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 || right > maxBound - left =
      error "Data.Structures.FingerTree.Rope: length overflow"
  | otherwise = left + right

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
