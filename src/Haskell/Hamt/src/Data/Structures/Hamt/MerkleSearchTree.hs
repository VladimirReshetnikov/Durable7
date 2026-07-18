{-# LANGUAGE BangPatterns #-}

-- | A canonical immutable B=16 Merkle search tree with exact @MST2@ block bytes.
module Data.Structures.Hamt.MerkleSearchTree
  ( MerkleSearchTree
  , MerkleEntry
  , entryKey
  , entryValue
  , entryKeyBytes
  , entryValueBytes
  , entryLevel
  , MerkleBlockView(..)
  , MerklePersistenceBlockView(..)
  , MerkleShapeEntry(..)
  , MerkleMapDifference(..)
  , MerkleSearchTreeStatistics(..)
  , MerkleTreeError(..)
  , MerkleRangeError(..)
  , MerkleCursor
  , MerkleCursorSearch
  , MerkleCursorEditError(..)
  , empty
  , fromList
  , policy
  , size
  , null
  , height
  , blockCount
  , rootDigest
  , cursor
  , cursorAt
  , cursorAtEnd
  , lowerBoundCursor
  , upperBoundCursor
  , cursorAtKey
  , cursorSearchFound
  , cursorSearchCursor
  , cursorCount
  , cursorPosition
  , cursorIsAtStart
  , cursorIsAtEnd
  , cursorPeekPrevious
  , cursorPeekNext
  , cursorMovePrevious
  , cursorMoveNext
  , cursorSeek
  , cursorInsert
  , cursorPut
  , cursorSetNextValue
  , cursorDeletePrevious
  , cursorDeleteNext
  , cursorSnapshot
  , member
  , lookup
  , lookupEntry
  , insert
  , delete
  , clear
  , contentEquals
  , mapEqualsWith
  , diffWith
  , entries
  , toAscList
  , range
  , blocksPreorder
  , persistenceBlocksPreorder
  , shape
  , commonBlockCount
  , validateStructure
  ) where

import Prelude hiding (lookup, null)

import Data.Array (Array, (!), bounds, elems, listArray)
import qualified Data.ByteString as ByteString
import Data.ByteString (ByteString)
import Data.Int (Int32)
import Data.List (sortBy)
import qualified Data.Set as Set
import Data.Word (Word8)

import Data.Structures.Hamt.MerkleEncoding
  ( MerkleCodecError
  , MerkleDigest
  , MerkleSearchTreePolicy
  , compareMerkleKeys
  , compatibleMerklePolicies
  , digestBytes
  , encodeMerkleValue
  , hashBytes
  , hashMerkleKeyBytes
  , int32BigEndian
  , merkleDomainDigest
  , merkleEmptyDigest
  , merkleKeyCodec
  , merkleLevel
  , merkleValueCodec
  )

-- | One retained key/value representative and its canonical encodings.
data MerkleEntry k v = MerkleEntry
  { entryKey :: k
  , entryValue :: v
  , entryKeyBytes :: !ByteString
  , entryValueBytes :: !ByteString
  , entryLevel :: !Word8
  }

instance (Show k, Show v) => Show (MerkleEntry k v) where
  show entry = "MerkleEntry { key = " ++ show (entryKey entry)
    ++ ", value = " ++ show (entryValue entry)
    ++ ", level = " ++ show (entryLevel entry) ++ " }"

data MerkleNode k v = MerkleNode
  { nodeLevel :: !Word8
  , nodeEntries :: !(Array Int (MerkleEntry k v))
  , nodeChildren :: !(Array Int (Maybe (MerkleNode k v)))
  , nodeCount :: !Int
  , nodeHeight :: !Int
  , nodeBlockCount :: !Int
  , nodeMinimumKey :: k
  , nodeMaximumKey :: k
  , nodeBlockBytes :: !ByteString
  , nodeDigest :: !MerkleDigest
  }

-- | An immutable canonical wide Merkle search tree.
data MerkleSearchTree k v = MerkleSearchTree
  { treeRoot :: !(Maybe (MerkleNode k v))
  , policy :: !(MerkleSearchTreePolicy k v)
  }

instance (Show k, Show v) => Show (MerkleSearchTree k v) where
  show tree = "MerkleSearchTree { size = " ++ show (size tree)
    ++ ", height = " ++ show (height tree)
    ++ ", blockCount = " ++ show (blockCount tree)
    ++ ", rootDigest = " ++ show (rootDigest tree) ++ " }"

-- | One immutable content-addressed block in preorder.
data MerkleBlockView = MerkleBlockView
  { blockViewDigest :: !MerkleDigest
  , blockViewBytes :: !ByteString
  } deriving (Eq, Show)

-- | Trusted read-only block detail used by the persistence companion module.
--
-- Unlike wire decoding, this view exposes the already-retained entry representatives and child
-- addresses, so local proof and synchronization construction never calls codecs or applies
-- untrusted-input budgets to a tree that has already passed core construction.
data MerklePersistenceBlockView k v = MerklePersistenceBlockView
  { persistenceBlockDigest :: !MerkleDigest
  , persistenceBlockBytes :: !ByteString
  , persistenceBlockLevel :: !Word8
  , persistenceBlockSubtreeCount :: !Int
  , persistenceBlockEntries :: ![MerkleEntry k v]
  , persistenceBlockChildDigests :: ![MerkleDigest]
  }

-- | Per-entry block-shape diagnostics.
data MerkleShapeEntry k = MerkleShapeEntry
  { shapeLevel :: !Word8
  , shapeKey :: k
  , shapeEntriesInBlock :: !Int
  , shapeSubtreeCount :: !Int
  } deriving (Eq, Show)

-- | A semantic key-level map difference.
data MerkleMapDifference k v
  = MerkleEntryAdded k v
  | MerkleEntryRemoved k v
  | MerkleEntryChanged k v v
  deriving (Eq, Show)

-- | Validated representation statistics.
data MerkleSearchTreeStatistics = MerkleSearchTreeStatistics
  { statisticsCount :: !Int
  , statisticsBlockCount :: !Int
  , statisticsHeight :: !Int
  , statisticsMinimumEntriesPerBlock :: !Int
  , statisticsMaximumEntriesPerBlock :: !Int
  , statisticsMinimumBlockBytes :: !Int
  , statisticsMaximumBlockBytes :: !Int
  } deriving (Eq, Show)

-- | A core construction or operation failure.
data MerkleTreeError
  = MerkleTreeCodecError !MerkleCodecError
  | MerkleTreeSizeOverflow
  | IncompatibleMerklePolicy
  | InvalidMerkleRepresentation !String
  deriving (Eq, Show)

-- | An inclusive range had reversed bounds.
data MerkleRangeError = MerkleRangeError
  deriving (Eq, Show)

-- | An immutable tree-snapshot-plus-rank gap cursor in policy-comparer order.
data MerkleCursor k v = MerkleCursor !(MerkleSearchTree k v) !Int

-- | A presence-safe exact-key result whose cursor remains usable on a miss.
data MerkleCursorSearch k v = MerkleCursorSearch !Bool !(MerkleCursor k v)

-- | A cursor edit could not publish at the current ordered gap.
data MerkleCursorEditError
  = MerkleCursorDuplicateKey
  | MerkleCursorWrongGap !Int !Int
  | MerkleCursorTreeError !MerkleTreeError
  deriving (Eq, Show)

-- | Creates an empty tree retaining the supplied deterministic policy.
empty :: MerkleSearchTreePolicy k v -> MerkleSearchTree k v
empty treePolicy = MerkleSearchTree Nothing treePolicy

-- | Builds a canonical tree with first-equivalent-key and last-value semantics.
fromList
  :: [(k, v)]
  -> MerkleSearchTreePolicy k v
  -> Either MerkleTreeError (MerkleSearchTree k v)
fromList [] treePolicy = Right (empty treePolicy)
fromList source treePolicy = do
  let sequenced = zipWith (\sequenceNumber (key, value) -> (key, value, sequenceNumber)) [0 :: Int ..] source
      sorted = sortBy comparePending sequenced
      comparePending (leftKey, _, leftSequence) (rightKey, _, rightSequence) =
        case compareMerkleKeys treePolicy leftKey rightKey of
          EQ -> compare leftSequence rightSequence
          ordering -> ordering
      grouped = groupPending sorted
      groupPending [] = []
      groupPending ((firstKey, firstValue, _) : rest) =
        let (equivalent, remaining) = span (\(key, _, _) -> compareMerkleKeys treePolicy firstKey key == EQ) rest
            lastValue = foldl' (\_ (_, value, _) -> value) firstValue equivalent
         in (firstKey, lastValue) : groupPending remaining
  encoded <- traverse (uncurry (createEntry treePolicy)) grouped
  root <- buildCanonical treePolicy encoded
  Right (MerkleSearchTree root treePolicy)

-- | Returns the number of entries.
size :: MerkleSearchTree k v -> Int
size = maybe 0 nodeCount . treeRoot

-- | Returns whether the tree has no entries.
null :: MerkleSearchTree k v -> Bool
null = (== 0) . size

-- | Returns the deepest block-path length.
height :: MerkleSearchTree k v -> Int
height = maybe 0 nodeHeight . treeRoot

-- | Returns the number of content-addressed blocks.
blockCount :: MerkleSearchTree k v -> Int
blockCount = maybe 0 nodeBlockCount . treeRoot

-- | Returns the policy-bound root content address.
rootDigest :: MerkleSearchTree k v -> MerkleDigest
rootDigest tree = maybe (merkleEmptyDigest (policy tree)) nodeDigest (treeRoot tree)

-- | Creates a cursor at the gap before the first entry.
cursor :: MerkleSearchTree k v -> MerkleCursor k v
cursor tree = MerkleCursor tree 0

-- | Creates a cursor at a rank gap, or 'Nothing' outside @0 .. size@.
cursorAt :: Int -> MerkleSearchTree k v -> Maybe (MerkleCursor k v)
cursorAt position tree
  | position < 0 || position > size tree = Nothing
  | otherwise = Just (MerkleCursor tree position)

-- | Creates a cursor after the final entry.
cursorAtEnd :: MerkleSearchTree k v -> MerkleCursor k v
cursorAtEnd tree = MerkleCursor tree (size tree)

-- | Creates the lower-bound cursor for a key.
lowerBoundCursor :: k -> MerkleSearchTree k v -> MerkleCursor k v
lowerBoundCursor key tree = MerkleCursor tree (fst (lowerBoundRank key tree))

-- | Creates the upper-bound cursor for a key.
upperBoundCursor :: k -> MerkleSearchTree k v -> MerkleCursor k v
upperBoundCursor key tree =
  let (position, found) = lowerBoundRank key tree
   in MerkleCursor tree (position + if found then 1 else 0)

-- | Creates a usable lower-bound cursor and reports whether its next entry is exact.
cursorAtKey :: k -> MerkleSearchTree k v -> MerkleCursorSearch k v
cursorAtKey key tree =
  let (position, found) = lowerBoundRank key tree
   in MerkleCursorSearch found (MerkleCursor tree position)

cursorSearchFound :: MerkleCursorSearch k v -> Bool
cursorSearchFound (MerkleCursorSearch found _) = found

cursorSearchCursor :: MerkleCursorSearch k v -> MerkleCursor k v
cursorSearchCursor (MerkleCursorSearch _ cursorValue) = cursorValue

cursorCount :: MerkleCursor k v -> Int
cursorCount (MerkleCursor tree _) = size tree

cursorPosition :: MerkleCursor k v -> Int
cursorPosition (MerkleCursor _ position) = position

cursorIsAtStart :: MerkleCursor k v -> Bool
cursorIsAtStart cursorValue = cursorPosition cursorValue == 0

cursorIsAtEnd :: MerkleCursor k v -> Bool
cursorIsAtEnd cursorValue = cursorPosition cursorValue == cursorCount cursorValue

-- | Returns the retained entry immediately before the gap.
cursorPeekPrevious :: MerkleCursor k v -> Maybe (MerkleEntry k v)
cursorPeekPrevious cursorValue@(MerkleCursor tree position)
  | cursorIsAtStart cursorValue = Nothing
  | otherwise = entryAtRank (position - 1) tree

-- | Returns the retained entry immediately after the gap.
cursorPeekNext :: MerkleCursor k v -> Maybe (MerkleEntry k v)
cursorPeekNext (MerkleCursor tree position) = entryAtRank position tree

cursorMovePrevious :: MerkleCursor k v -> Maybe (MerkleCursor k v)
cursorMovePrevious cursorValue@(MerkleCursor tree position)
  | cursorIsAtStart cursorValue = Nothing
  | otherwise = Just (MerkleCursor tree (position - 1))

cursorMoveNext :: MerkleCursor k v -> Maybe (MerkleCursor k v)
cursorMoveNext cursorValue@(MerkleCursor tree position)
  | cursorIsAtEnd cursorValue = Nothing
  | otherwise = Just (MerkleCursor tree (position + 1))

cursorSeek :: Int -> MerkleCursor k v -> Maybe (MerkleCursor k v)
cursorSeek position cursorValue@(MerkleCursor tree _)
  | position < 0 || position > size tree = Nothing
  | position == cursorPosition cursorValue = Just cursorValue
  | otherwise = Just (MerkleCursor tree position)

-- | Strictly inserts a missing key at its lower-bound gap.
cursorInsert
  :: k -> v -> MerkleCursor k v
  -> Either MerkleCursorEditError (MerkleCursor k v)
cursorInsert key value (MerkleCursor tree position) =
  let (expected, found) = lowerBoundRank key tree
   in if found
        then Left MerkleCursorDuplicateKey
        else if expected /= position
          then Left (MerkleCursorWrongGap expected position)
          else do
            edited <- mapLeft MerkleCursorTreeError (insert key value tree)
            Right (MerkleCursor edited (position + 1))

-- | Updates an exact next entry or inserts at a missing lower-bound gap.
cursorPut
  :: k -> v -> MerkleCursor k v
  -> Either MerkleCursorEditError (MerkleCursor k v)
cursorPut key value (MerkleCursor tree position) =
  let (expected, found) = lowerBoundRank key tree
   in if expected /= position
        then Left (MerkleCursorWrongGap expected position)
        else do
          edited <- mapLeft MerkleCursorTreeError (insert key value tree)
          Right (MerkleCursor edited (position + if found then 0 else 1))

-- | Replaces the next value while retaining its key representative and current gap.
cursorSetNextValue
  :: v -> MerkleCursor k v
  -> Either MerkleTreeError (Maybe (MerkleCursor k v))
cursorSetNextValue value cursorValue@(MerkleCursor tree position) = case cursorPeekNext cursorValue of
  Nothing -> Right Nothing
  Just entry -> fmap (Just . flip MerkleCursor position) (insert (entryKey entry) value tree)

-- | Deletes the entry before the gap and moves the gap left.
cursorDeletePrevious
  :: MerkleCursor k v
  -> Either MerkleTreeError (Maybe (MerkleCursor k v))
cursorDeletePrevious cursorValue@(MerkleCursor tree position) = case cursorPeekPrevious cursorValue of
  Nothing -> Right Nothing
  Just entry -> fmap (Just . flip MerkleCursor (position - 1)) (delete (entryKey entry) tree)

-- | Deletes the entry after the gap and keeps the gap fixed.
cursorDeleteNext
  :: MerkleCursor k v
  -> Either MerkleTreeError (Maybe (MerkleCursor k v))
cursorDeleteNext cursorValue@(MerkleCursor tree position) = case cursorPeekNext cursorValue of
  Nothing -> Right Nothing
  Just entry -> fmap (Just . flip MerkleCursor position) (delete (entryKey entry) tree)

-- | Returns this cursor version's canonical immutable tree.
cursorSnapshot :: MerkleCursor k v -> MerkleSearchTree k v
cursorSnapshot (MerkleCursor tree _) = tree

-- | Returns whether an equivalent key is present.
member :: k -> MerkleSearchTree k v -> Bool
member key = maybe False (const True) . lookupEntry key

-- | Returns the value retained for an equivalent key.
lookup :: k -> MerkleSearchTree k v -> Maybe v
lookup key = fmap entryValue . lookupEntry key

-- | Returns the retained entry, including the first equivalent key representative.
lookupEntry :: k -> MerkleSearchTree k v -> Maybe (MerkleEntry k v)
lookupEntry key tree = go (treeRoot tree)
  where
    go Nothing = Nothing
    go (Just node) =
      let (position, found) = findPosition (policy tree) (nodeEntries node) key
       in if found then Just (nodeEntries node ! position) else go (nodeChildren node ! position)

-- | Adds or replaces an entry while copying only affected blocks.
insert :: k -> v -> MerkleSearchTree k v -> Either MerkleTreeError (MerkleSearchTree k v)
insert key value tree = case lookupEntry key tree of
  Just existing -> do
    valueBytes <- mapCodecError (encodeMerkleValue (merkleValueCodec (policy tree)) value)
    if entryValueBytes existing == valueBytes
      then Right tree
      else do
        let replacement = existing { entryValue = value, entryValueBytes = valueBytes }
        root <- case treeRoot tree of
          Nothing -> Left (InvalidMerkleRepresentation "an existing entry requires a root block")
          Just node -> Just <$> updateValue (policy tree) node key replacement
        Right tree { treeRoot = root }
  Nothing -> do
    entry <- createEntry (policy tree) key value
    root <- insertNode (policy tree) (treeRoot tree) entry
    Right tree { treeRoot = Just root }

-- | Removes an equivalent key and contracts empty block shells.
delete :: k -> MerkleSearchTree k v -> Either MerkleTreeError (MerkleSearchTree k v)
delete key tree = do
  (root, removed) <- removeNode (policy tree) (treeRoot tree) key
  Right (if removed then tree { treeRoot = root } else tree)

-- | Returns an empty tree retaining the same policy.
clear :: MerkleSearchTree k v -> MerkleSearchTree k v
clear tree
  | null tree = tree
  | otherwise = empty (policy tree)

-- | Compares policy domains and root content addresses in O(1).
contentEquals :: MerkleSearchTree k v -> MerkleSearchTree k v -> Bool
contentEquals left right = merkleDomainDigest (policy left) == merkleDomainDigest (policy right)
  && rootDigest left == rootDigest right

-- | Compares semantic contents using a caller-supplied value relation.
mapEqualsWith :: (v -> v -> Bool) -> MerkleSearchTree k v -> MerkleSearchTree k v -> Bool
mapEqualsWith valuesEqual left right
  | size left /= size right = False
  | merkleDomainDigest (policy left) /= merkleDomainDigest (policy right) = False
  | otherwise = and (zipWith entriesEqual (entries left) (entries right))
  where
    entriesEqual leftEntry rightEntry =
      compareMerkleKeys (policy left) (entryKey leftEntry) (entryKey rightEntry) == EQ
        && valuesEqual (entryValue leftEntry) (entryValue rightEntry)

-- | Computes a root-digest-pruned ordered semantic diff.
diffWith
  :: (v -> v -> Bool)
  -> MerkleSearchTree k v
  -> MerkleSearchTree k v
  -> Either MerkleTreeError [MerkleMapDifference k v]
diffWith valuesEqual left right
  | not (compatibleMerklePolicies (policy left) (policy right)) = Left IncompatibleMerklePolicy
  | otherwise = Right (diffNodes (treeRoot left) (treeRoot right))
  where
    diffNodes Nothing Nothing = []
    diffNodes (Just leftNode) (Just rightNode)
      | nodeDigest leftNode == nodeDigest rightNode = []
      | sameSeparators leftNode rightNode = concat
          [ diffNodes (nodeChildren leftNode ! index) (nodeChildren rightNode ! index)
              ++ changedAt index
          | index <- [0 .. arrayLength (nodeEntries leftNode) - 1]
          ] ++ diffNodes
            (nodeChildren leftNode ! arrayLength (nodeEntries leftNode))
            (nodeChildren rightNode ! arrayLength (nodeEntries rightNode))
      | otherwise = merge (enumerateNode leftNode) (enumerateNode rightNode)
      where
        changedAt index =
          let oldEntry = nodeEntries leftNode ! index
              newEntry = nodeEntries rightNode ! index
           in if valuesEqual (entryValue oldEntry) (entryValue newEntry)
                then []
                else [MerkleEntryChanged (entryKey oldEntry) (entryValue oldEntry) (entryValue newEntry)]
    diffNodes Nothing (Just rightNode) =
      map (\entry -> MerkleEntryAdded (entryKey entry) (entryValue entry)) (enumerateNode rightNode)
    diffNodes (Just leftNode) Nothing =
      map (\entry -> MerkleEntryRemoved (entryKey entry) (entryValue entry)) (enumerateNode leftNode)

    sameSeparators leftNode rightNode =
      nodeLevel leftNode == nodeLevel rightNode
        && arrayLength (nodeEntries leftNode) == arrayLength (nodeEntries rightNode)
        && and
          [ compareMerkleKeys (policy left)
              (entryKey (nodeEntries leftNode ! index))
              (entryKey (nodeEntries rightNode ! index)) == EQ
          | index <- [0 .. arrayLength (nodeEntries leftNode) - 1]
          ]

    merge [] remainingRight = map (\entry -> MerkleEntryAdded (entryKey entry) (entryValue entry)) remainingRight
    merge remainingLeft [] = map (\entry -> MerkleEntryRemoved (entryKey entry) (entryValue entry)) remainingLeft
    merge leftEntries@(leftEntry : leftRest) rightEntries@(rightEntry : rightRest) =
      case compareMerkleKeys (policy left) (entryKey leftEntry) (entryKey rightEntry) of
        LT -> MerkleEntryRemoved (entryKey leftEntry) (entryValue leftEntry) : merge leftRest rightEntries
        GT -> MerkleEntryAdded (entryKey rightEntry) (entryValue rightEntry) : merge leftEntries rightRest
        EQ
          | valuesEqual (entryValue leftEntry) (entryValue rightEntry) -> merge leftRest rightRest
          | otherwise -> MerkleEntryChanged (entryKey leftEntry) (entryValue leftEntry) (entryValue rightEntry) : merge leftRest rightRest

-- | Returns entries in comparer order.
entries :: MerkleSearchTree k v -> [MerkleEntry k v]
entries tree = maybe [] enumerateNode (treeRoot tree)

-- | Returns key/value pairs in comparer order.
toAscList :: MerkleSearchTree k v -> [(k, v)]
toAscList = map (\entry -> (entryKey entry, entryValue entry)) . entries

-- | Eagerly validates and enumerates an inclusive comparer-ordered key range.
range :: k -> k -> MerkleSearchTree k v -> Either MerkleRangeError [MerkleEntry k v]
range minimumKey maximumKey tree
  | compareMerkleKeys (policy tree) minimumKey maximumKey == GT = Left MerkleRangeError
  | otherwise = Right (maybe [] visit (treeRoot tree))
  where
    visit node
      | compareMerkleKeys (policy tree) (nodeMaximumKey node) minimumKey == LT = []
      | compareMerkleKeys (policy tree) (nodeMinimumKey node) maximumKey == GT = []
      | otherwise = concat
          [ maybe [] visit (nodeChildren node ! index)
              ++ let entry = nodeEntries node ! index
                  in [entry | inRange entry]
          | index <- [0 .. arrayLength (nodeEntries node) - 1]
          ] ++ maybe [] visit (nodeChildren node ! arrayLength (nodeEntries node))
    inRange entry = compareMerkleKeys (policy tree) minimumKey (entryKey entry) /= GT
      && compareMerkleKeys (policy tree) (entryKey entry) maximumKey /= GT

-- | Enumerates exact immutable block bytes in canonical preorder.
blocksPreorder :: MerkleSearchTree k v -> [MerkleBlockView]
blocksPreorder tree = maybe [] go (treeRoot tree)
  where
    go node = MerkleBlockView (nodeDigest node) (nodeBlockBytes node)
      : concatMap (maybe [] go) (elems (nodeChildren node))

-- | Enumerates trusted block topology in canonical preorder for persistence algorithms.
persistenceBlocksPreorder :: MerkleSearchTree k v -> [MerklePersistenceBlockView k v]
persistenceBlocksPreorder tree = maybe [] go (treeRoot tree)
  where
    go node = MerklePersistenceBlockView
      { persistenceBlockDigest = nodeDigest node
      , persistenceBlockBytes = nodeBlockBytes node
      , persistenceBlockLevel = nodeLevel node
      , persistenceBlockSubtreeCount = nodeCount node
      , persistenceBlockEntries = elems (nodeEntries node)
      , persistenceBlockChildDigests =
          map (maybe (merkleEmptyDigest (policy tree)) nodeDigest) (elems (nodeChildren node))
      } : concatMap (maybe [] go) (elems (nodeChildren node))

-- | Returns one shape record per entry in block preorder.
shape :: MerkleSearchTree k v -> [MerkleShapeEntry k]
shape tree = maybe [] go (treeRoot tree)
  where
    go node = map (\entry -> MerkleShapeEntry (nodeLevel node) (entryKey entry) (arrayLength (nodeEntries node)) (nodeCount node)) (elems (nodeEntries node))
      ++ concatMap (maybe [] go) (elems (nodeChildren node))

-- | Counts common content-addressed blocks, including independently reconstructed ones.
commonBlockCount :: MerkleSearchTree k v -> MerkleSearchTree k v -> Int
commonBlockCount left right = Set.size (Set.intersection leftDigests rightDigests)
  where
    leftDigests = Set.fromList (map blockViewDigest (blocksPreorder left))
    rightDigests = Set.fromList (map blockViewDigest (blocksPreorder right))

-- | Validates ordering, hash levels, metadata, exact block bytes, and content addresses.
validateStructure :: MerkleSearchTree k v -> Either String MerkleSearchTreeStatistics
validateStructure tree = case treeRoot tree of
  Nothing -> Right (MerkleSearchTreeStatistics 0 0 0 0 0 0 0)
  Just root -> do
    accumulation <- validateNode (policy tree) root
    if validationCount accumulation /= size tree || validationBlocks accumulation /= blockCount tree
      then Left "Merkle root metadata disagrees with validated contents"
      else Right (statisticsFromValidation (height tree) accumulation)

createEntry :: MerkleSearchTreePolicy k v -> k -> v -> Either MerkleTreeError (MerkleEntry k v)
createEntry treePolicy key value = do
  keyBytes <- mapCodecError (encodeMerkleValue (merkleKeyCodec treePolicy) key)
  valueBytes <- mapCodecError (encodeMerkleValue (merkleValueCodec treePolicy) value)
  let level = merkleLevel (hashMerkleKeyBytes treePolicy keyBytes)
  Right (MerkleEntry key value keyBytes valueBytes level)

buildCanonical
  :: MerkleSearchTreePolicy k v
  -> [MerkleEntry k v]
  -> Either MerkleTreeError (Maybe (MerkleNode k v))
buildCanonical _ [] = Right Nothing
buildCanonical treePolicy source = buildSegments source [] []
  where
    maximumLevel = maximum (map entryLevel source)
    buildSegments remaining blockEntries children =
      let (segment, suffix) = break ((== maximumLevel) . entryLevel) remaining
       in do
        child <- buildCanonical treePolicy segment
        case suffix of
          [] -> Just <$> newNode treePolicy maximumLevel blockEntries (children ++ [child])
          separator : rest -> buildSegments rest (blockEntries ++ [separator]) (children ++ [child])

updateValue
  :: MerkleSearchTreePolicy k v
  -> MerkleNode k v
  -> k
  -> MerkleEntry k v
  -> Either MerkleTreeError (MerkleNode k v)
updateValue treePolicy node key replacement =
  let (position, found) = findPosition treePolicy (nodeEntries node) key
   in if found
        then newNode treePolicy (nodeLevel node) (replaceAt position replacement (elems (nodeEntries node))) (elems (nodeChildren node))
        else case nodeChildren node ! position of
          Nothing -> Left (InvalidMerkleRepresentation "an existing key path ended at an empty interval")
          Just child -> do
            updated <- updateValue treePolicy child key replacement
            newNode treePolicy (nodeLevel node) (elems (nodeEntries node)) (replaceAt position (Just updated) (elems (nodeChildren node)))

insertNode
  :: MerkleSearchTreePolicy k v
  -> Maybe (MerkleNode k v)
  -> MerkleEntry k v
  -> Either MerkleTreeError (MerkleNode k v)
insertNode treePolicy Nothing entry = newNode treePolicy (entryLevel entry) [entry] [Nothing, Nothing]
insertNode treePolicy (Just node) entry
  | entryLevel entry > nodeLevel node = do
      (left, right) <- splitNode treePolicy (Just node) (entryKey entry)
      newNode treePolicy (entryLevel entry) [entry] [left, right]
  | entryLevel entry < nodeLevel node = do
      let (position, _) = findPosition treePolicy (nodeEntries node) (entryKey entry)
      child <- insertNode treePolicy (nodeChildren node ! position) entry
      newNode treePolicy (nodeLevel node) (elems (nodeEntries node)) (replaceAt position (Just child) (elems (nodeChildren node)))
  | otherwise = do
      let (position, _) = findPosition treePolicy (nodeEntries node) (entryKey entry)
      (left, right) <- splitNode treePolicy (nodeChildren node ! position) (entryKey entry)
      let updatedEntries = insertAt position entry (elems (nodeEntries node))
          children = elems (nodeChildren node)
          updatedChildren = insertAt (position + 1) right (replaceAt position left children)
      newNode treePolicy (nodeLevel node) updatedEntries updatedChildren

splitNode
  :: MerkleSearchTreePolicy k v
  -> Maybe (MerkleNode k v)
  -> k
  -> Either MerkleTreeError (Maybe (MerkleNode k v), Maybe (MerkleNode k v))
splitNode _ Nothing _ = Right (Nothing, Nothing)
splitNode treePolicy (Just node) key = do
  let (position, _) = findPosition treePolicy (nodeEntries node) key
  (childLeft, childRight) <- splitNode treePolicy (nodeChildren node ! position) key
  let sourceEntries = elems (nodeEntries node)
      sourceChildren = elems (nodeChildren node)
      leftEntries = take position sourceEntries
      leftChildren = replaceAt position childLeft (take (position + 1) sourceChildren)
      rightEntries = drop position sourceEntries
      rightChildren = replaceAt 0 childRight (drop position sourceChildren)
  left <- createOrCollapse treePolicy (nodeLevel node) leftEntries leftChildren
  right <- createOrCollapse treePolicy (nodeLevel node) rightEntries rightChildren
  Right (left, right)

removeNode
  :: MerkleSearchTreePolicy k v
  -> Maybe (MerkleNode k v)
  -> k
  -> Either MerkleTreeError (Maybe (MerkleNode k v), Bool)
removeNode _ Nothing _ = Right (Nothing, False)
removeNode treePolicy (Just node) key =
  let (position, found) = findPosition treePolicy (nodeEntries node) key
   in if not found
        then do
          (child, removed) <- removeNode treePolicy (nodeChildren node ! position) key
          if not removed
            then Right (Just node, False)
            else do
              rebuilt <- newNode treePolicy (nodeLevel node) (elems (nodeEntries node)) (replaceAt position child (elems (nodeChildren node)))
              Right (Just rebuilt, True)
        else do
          let sourceEntries = elems (nodeEntries node)
              sourceChildren = elems (nodeChildren node)
          joined <- joinNodes treePolicy (sourceChildren !! position) (sourceChildren !! (position + 1))
          let updatedEntries = removeAt position sourceEntries
              updatedChildren = removeAt (position + 1) (replaceAt position joined sourceChildren)
          rebuilt <- createOrCollapse treePolicy (nodeLevel node) updatedEntries updatedChildren
          Right (rebuilt, True)

joinNodes
  :: MerkleSearchTreePolicy k v
  -> Maybe (MerkleNode k v)
  -> Maybe (MerkleNode k v)
  -> Either MerkleTreeError (Maybe (MerkleNode k v))
joinNodes _ Nothing Nothing = Right Nothing
joinNodes _ (Just node) Nothing = Right (Just node)
joinNodes _ Nothing (Just node) = Right (Just node)
joinNodes treePolicy (Just left) (Just right)
  | nodeLevel left > nodeLevel right = do
      let children = elems (nodeChildren left)
          lastIndex = length children - 1
      joined <- joinNodes treePolicy (children !! lastIndex) (Just right)
      Just <$> newNode treePolicy (nodeLevel left) (elems (nodeEntries left)) (replaceAt lastIndex joined children)
  | nodeLevel left < nodeLevel right = do
      let children = elems (nodeChildren right)
      joined <- joinNodes treePolicy (Just left) (nodeChildren right ! 0)
      Just <$> newNode treePolicy (nodeLevel right) (elems (nodeEntries right)) (replaceAt 0 joined children)
  | otherwise = do
      let leftEntries = elems (nodeEntries left)
          rightEntries = elems (nodeEntries right)
          leftChildren = elems (nodeChildren left)
          rightChildren = elems (nodeChildren right)
      middle <- joinNodes treePolicy
        (nodeChildren left ! arrayLength (nodeEntries left))
        (nodeChildren right ! 0)
      let children = take (length leftEntries) leftChildren ++ [middle] ++ drop 1 rightChildren
      Just <$> newNode treePolicy (nodeLevel left) (leftEntries ++ rightEntries) children

createOrCollapse
  :: MerkleSearchTreePolicy k v
  -> Word8
  -> [MerkleEntry k v]
  -> [Maybe (MerkleNode k v)]
  -> Either MerkleTreeError (Maybe (MerkleNode k v))
createOrCollapse _ _ [] [child] = Right child
createOrCollapse treePolicy level nodeEntryList childList = Just <$> newNode treePolicy level nodeEntryList childList

newNode
  :: MerkleSearchTreePolicy k v
  -> Word8
  -> [MerkleEntry k v]
  -> [Maybe (MerkleNode k v)]
  -> Either MerkleTreeError (MerkleNode k v)
newNode treePolicy level nodeEntryList childList
  | level > 64 || nullList nodeEntryList || length childList /= length nodeEntryList + 1 =
      Left (InvalidMerkleRepresentation "an MST block must contain a valid level, entries, and one extra child interval")
  | any ((>= level) . nodeLevel) presentChildren =
      Left (InvalidMerkleRepresentation "an MST child level must be lower than its parent level")
  | otherwise = do
      let entryCount = length nodeEntryList
          count = entryCount + sum (map nodeCount presentChildren)
          blockHeight = 1 + maximum (0 : map nodeHeight presentChildren)
          blocks = 1 + sum (map nodeBlockCount presentChildren)
      if count > fromIntegral (maxBound :: Int32) || entryCount > fromIntegral (maxBound :: Int32)
        then Left MerkleTreeSizeOverflow
        else do
          blockBytes <- encodeBlock treePolicy level count nodeEntryList childList
          case (nodeEntryList, childList, reverse nodeEntryList, reverse childList) of
            (firstEntry : _, firstChild : _, lastEntry : _, lastChild : _) ->
              let minimumKey = maybe (entryKey firstEntry) nodeMinimumKey firstChild
                  maximumKey = maybe (entryKey lastEntry) nodeMaximumKey lastChild
               in Right MerkleNode
                    { nodeLevel = level
                    , nodeEntries = arrayFromList nodeEntryList
                    , nodeChildren = arrayFromList childList
                    , nodeCount = count
                    , nodeHeight = blockHeight
                    , nodeBlockCount = blocks
                    , nodeMinimumKey = minimumKey
                    , nodeMaximumKey = maximumKey
                    , nodeBlockBytes = blockBytes
                    , nodeDigest = hashBytes blockBytes
                    }
            _ -> Left (InvalidMerkleRepresentation "an MST block cannot be empty")
  where
    presentChildren = [child | Just child <- childList]

encodeBlock
  :: MerkleSearchTreePolicy k v
  -> Word8
  -> Int
  -> [MerkleEntry k v]
  -> [Maybe (MerkleNode k v)]
  -> Either MerkleTreeError ByteString
encodeBlock treePolicy level subtreeCount nodeEntryList childList = do
  traverse_ checkEntryLength nodeEntryList
  Right (ByteString.concat
    [ asciiBytes "MST2"
    , ByteString.singleton 1
    , digestBytes (merkleDomainDigest treePolicy)
    , ByteString.singleton level
    , int32BigEndian (fromIntegral subtreeCount)
    , int32BigEndian (fromIntegral (length nodeEntryList))
    , ByteString.concat (map encodeEntry nodeEntryList)
    , ByteString.concat (map (digestBytes . maybe (merkleEmptyDigest treePolicy) nodeDigest) childList)
    ])
  where
    checkEntryLength entry
      | ByteString.length (entryKeyBytes entry) > fromIntegral (maxBound :: Int32) = Left MerkleTreeSizeOverflow
      | ByteString.length (entryValueBytes entry) > fromIntegral (maxBound :: Int32) = Left MerkleTreeSizeOverflow
      | otherwise = Right ()
    encodeEntry entry = ByteString.concat
      [ int32BigEndian (fromIntegral (ByteString.length (entryKeyBytes entry)))
      , entryKeyBytes entry
      , int32BigEndian (fromIntegral (ByteString.length (entryValueBytes entry)))
      , entryValueBytes entry
      ]

findPosition :: MerkleSearchTreePolicy k v -> Array Int (MerkleEntry k v) -> k -> (Int, Bool)
findPosition treePolicy entryArray key = go 0 (arrayLength entryArray)
  where
    go low high
      | low >= high = (low, low < arrayLength entryArray && compareMerkleKeys treePolicy (entryKey (entryArray ! low)) key == EQ)
      | otherwise =
          let middle = (low + high) `div` 2
           in if compareMerkleKeys treePolicy (entryKey (entryArray ! middle)) key == LT
                then go (middle + 1) high
                else go low middle

entryAtRank :: Int -> MerkleSearchTree k v -> Maybe (MerkleEntry k v)
entryAtRank rank tree
  | rank < 0 || rank >= size tree = Nothing
  | otherwise = treeRoot tree >>= go rank
  where
    go target node = scan 0 target
      where
        entryCount = arrayLength (nodeEntries node)
        scan index remaining
          | index > entryCount = Nothing
          | otherwise =
              let child = nodeChildren node ! index
                  childCount = maybe 0 nodeCount child
               in if remaining < childCount
                    then child >>= go remaining
                    else if index == entryCount
                      then Nothing
                      else if remaining == childCount
                        then Just (nodeEntries node ! index)
                        else scan (index + 1) (remaining - childCount - 1)

lowerBoundRank :: k -> MerkleSearchTree k v -> (Int, Bool)
lowerBoundRank key tree = go 0 (treeRoot tree)
  where
    go rank Nothing = (rank, False)
    go rank (Just node) =
      let (position, found) = findPosition (policy tree) (nodeEntries node) key
          preceding = sum
            [ maybe 0 nodeCount (nodeChildren node ! index) + 1
            | index <- [0 .. position - 1]
            ]
          nextRank = rank + preceding
       in if found
            then (nextRank + maybe 0 nodeCount (nodeChildren node ! position), True)
            else go nextRank (nodeChildren node ! position)

mapLeft :: (a -> b) -> Either a value -> Either b value
mapLeft transform result = case result of
  Left failure -> Left (transform failure)
  Right value -> Right value

enumerateNode :: MerkleNode k v -> [MerkleEntry k v]
enumerateNode node = concat
  [ maybe [] enumerateNode (nodeChildren node ! index)
      ++ [nodeEntries node ! index]
  | index <- [0 .. arrayLength (nodeEntries node) - 1]
  ] ++ maybe [] enumerateNode (nodeChildren node ! arrayLength (nodeEntries node))

data Validation = Validation
  { validationCount :: !Int
  , validationBlocks :: !Int
  , validationMinimumEntries :: !Int
  , validationMaximumEntries :: !Int
  , validationMinimumBytes :: !Int
  , validationMaximumBytes :: !Int
  }

validateNode :: MerkleSearchTreePolicy k v -> MerkleNode k v -> Either String Validation
validateNode treePolicy node = do
  let nodeEntryList = elems (nodeEntries node)
      childList = elems (nodeChildren node)
      presentChildren = [child | Just child <- childList]
      entryCount = length nodeEntryList
  require (nodeLevel node <= 64) "Merkle block level exceeds 64"
  require (entryCount > 0 && length childList == entryCount + 1) "Merkle block entry/child arity is invalid"
  require (all ((== nodeLevel node) . entryLevel) nodeEntryList) "Merkle entry level disagrees with its block"
  require (strictlyOrdered treePolicy (map entryKey nodeEntryList)) "Merkle block keys are not strictly ordered"
  require (all ((< nodeLevel node) . nodeLevel) presentChildren) "Merkle child level does not descend"
  traverse_ validateEntry nodeEntryList
  traverse_ (validateInterval treePolicy nodeEntryList childList) [0 .. entryCount]
  childrenValidation <- traverse (validateNode treePolicy) presentChildren
  let expectedCount = entryCount + sum (map validationCount childrenValidation)
      expectedHeight = 1 + maximum (0 : map nodeHeight presentChildren)
      expectedBlocks = 1 + sum (map validationBlocks childrenValidation)
  require (nodeCount node == expectedCount) "Merkle cached subtree count is invalid"
  require (nodeHeight node == expectedHeight) "Merkle cached height is invalid"
  require (nodeBlockCount node == expectedBlocks) "Merkle cached block count is invalid"
  expectedBlock <- either (Left . show) Right (encodeBlock treePolicy (nodeLevel node) expectedCount nodeEntryList childList)
  require (nodeBlockBytes node == expectedBlock) "Merkle block bytes are not canonical"
  require (nodeDigest node == hashBytes expectedBlock) "Merkle block digest is invalid"
  case (nodeEntryList, childList, reverse nodeEntryList, reverse childList) of
    (firstEntry : _, firstChild : _, lastEntry : _, lastChild : _) -> do
      let expectedMinimum = maybe (entryKey firstEntry) nodeMinimumKey firstChild
          expectedMaximum = maybe (entryKey lastEntry) nodeMaximumKey lastChild
      require (compareMerkleKeys treePolicy (nodeMinimumKey node) expectedMinimum == EQ) "Merkle cached minimum key is invalid"
      require (compareMerkleKeys treePolicy (nodeMaximumKey node) expectedMaximum == EQ) "Merkle cached maximum key is invalid"
    _ -> Left "Merkle block cannot be empty"
  Right (combineValidation entryCount (ByteString.length expectedBlock) childrenValidation)
  where
    validateEntry entry = do
      keyBytes <- either (Left . show) Right (encodeMerkleValue (merkleKeyCodec treePolicy) (entryKey entry))
      valueBytes <- either (Left . show) Right (encodeMerkleValue (merkleValueCodec treePolicy) (entryValue entry))
      require (keyBytes == entryKeyBytes entry) "Merkle cached key encoding is invalid"
      require (valueBytes == entryValueBytes entry) "Merkle cached value encoding is invalid"
      require (merkleLevel (hashMerkleKeyBytes treePolicy keyBytes) == entryLevel entry) "Merkle cached key level is invalid"

validateInterval
  :: MerkleSearchTreePolicy k v
  -> [MerkleEntry k v]
  -> [Maybe (MerkleNode k v)]
  -> Int
  -> Either String ()
validateInterval treePolicy nodeEntryList childList index = case childList !! index of
  Nothing -> Right ()
  Just child -> do
    if index > 0
      then require (compareMerkleKeys treePolicy (entryKey (nodeEntryList !! (index - 1))) (nodeMinimumKey child) == LT)
        "Merkle child minimum crosses its lower separator"
      else Right ()
    if index < length nodeEntryList
      then require (compareMerkleKeys treePolicy (nodeMaximumKey child) (entryKey (nodeEntryList !! index)) == LT)
        "Merkle child maximum crosses its upper separator"
      else Right ()

strictlyOrdered :: MerkleSearchTreePolicy k v -> [k] -> Bool
strictlyOrdered _ [] = True
strictlyOrdered _ [_] = True
strictlyOrdered treePolicy (left : right : rest) =
  compareMerkleKeys treePolicy left right == LT && strictlyOrdered treePolicy (right : rest)

combineValidation :: Int -> Int -> [Validation] -> Validation
combineValidation ownEntries ownBytes children = Validation
  { validationCount = ownEntries + sum (map validationCount children)
  , validationBlocks = 1 + sum (map validationBlocks children)
  , validationMinimumEntries = minimum (ownEntries : map validationMinimumEntries children)
  , validationMaximumEntries = maximum (ownEntries : map validationMaximumEntries children)
  , validationMinimumBytes = minimum (ownBytes : map validationMinimumBytes children)
  , validationMaximumBytes = maximum (ownBytes : map validationMaximumBytes children)
  }

statisticsFromValidation :: Int -> Validation -> MerkleSearchTreeStatistics
statisticsFromValidation rootHeight validation = MerkleSearchTreeStatistics
  { statisticsCount = validationCount validation
  , statisticsBlockCount = validationBlocks validation
  , statisticsHeight = rootHeight
  , statisticsMinimumEntriesPerBlock = validationMinimumEntries validation
  , statisticsMaximumEntriesPerBlock = validationMaximumEntries validation
  , statisticsMinimumBlockBytes = validationMinimumBytes validation
  , statisticsMaximumBlockBytes = validationMaximumBytes validation
  }

mapCodecError :: Either MerkleCodecError a -> Either MerkleTreeError a
mapCodecError = either (Left . MerkleTreeCodecError) Right

require :: Bool -> String -> Either String ()
require True _ = Right ()
require False message = Left message

traverse_ :: (a -> Either e b) -> [a] -> Either e ()
traverse_ action = foldl' step (Right ())
  where
    step result value = result >> action value >> Right ()

arrayFromList :: [a] -> Array Int a
arrayFromList values = listArray (0, length values - 1) values

arrayLength :: Array Int a -> Int
arrayLength values = let (lower, upper) = bounds values in max 0 (upper - lower + 1)

replaceAt :: Int -> a -> [a] -> [a]
replaceAt index value source = take index source ++ [value] ++ drop (index + 1) source

insertAt :: Int -> a -> [a] -> [a]
insertAt index value source = take index source ++ [value] ++ drop index source

removeAt :: Int -> [a] -> [a]
removeAt index source = take index source ++ drop (index + 1) source

nullList :: [a] -> Bool
nullList [] = True
nullList _ = False

asciiBytes :: String -> ByteString
asciiBytes = ByteString.pack . map (fromIntegral . fromEnum)
