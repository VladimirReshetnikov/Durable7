module Data.Structures.Tungsten.Association
  ( PersistentAssociation
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , toList
  , count
  , null
  , policy
  , member
  , lookup
  , actualKey
  , first
  , last
  , getAt
  , indexOfKey
  , setItem
  , setItems
  , join
  , append
  , prepend
  , insertAt
  , delete
  , tryRemove
  , removeRange
  , keyTake
  , deleteAt
  , removeFirst
  , removeLast
  , slice
  , take
  , drop
  , reverse
  , keySort
  , keySortBy
  , sort
  , sortBy
  , keys
  , elems
  ) where

import Prelude hiding (drop, last, lookup, null, reverse, take)

import qualified Data.List as List

import Data.Structures.Hamt.Hashable (Hashable)
import qualified Data.Structures.Hamt.HashMap as HM
import Data.Structures.Hamt.HashMap (HashPolicy)

stampGap :: Int
stampGap = 2 ^ (20 :: Int)

data Slot v = Slot !Int v
  deriving (Eq, Ord, Read, Show)

data Entry k v = Entry !Int k v
  deriving (Read, Show)

data SeqTree a
  = Empty
  | Node !Int !Int (SeqTree a) a (SeqTree a)
  deriving (Eq, Ord, Read, Show)

data PersistentAssociation k v = PersistentAssociation !(SeqTree (Entry k v)) !(HM.HashMap k (Slot v))

empty :: (Eq k, Hashable k) => PersistentAssociation k v
empty = emptyWith HM.defaultPolicy

emptyWith :: HashPolicy k -> PersistentAssociation k v
emptyWith hashPolicy = PersistentAssociation Empty (HM.emptyWith hashPolicy)

fromList :: (Eq k, Hashable k, Eq v) => [(k, v)] -> PersistentAssociation k v
fromList = fromListWith HM.defaultPolicy

fromListWith :: Eq v => HashPolicy k -> [(k, v)] -> PersistentAssociation k v
fromListWith hashPolicy = setItems (emptyWith hashPolicy)

toList :: PersistentAssociation k v -> [(k, v)]
toList (PersistentAssociation entries _) = fmap entryPair (treeToList entries)

count :: PersistentAssociation k v -> Int
count (PersistentAssociation entries _) = treeSize entries

null :: PersistentAssociation k v -> Bool
null association = count association == 0

policy :: PersistentAssociation k v -> HashPolicy k
policy (PersistentAssociation _ index) = HM.policy index

member :: k -> PersistentAssociation k v -> Bool
member key (PersistentAssociation _ index) = HM.member key index

lookup :: k -> PersistentAssociation k v -> Maybe v
lookup key (PersistentAssociation _ index) =
  case HM.lookup key index of
    Just (Slot _ value) -> Just value
    Nothing -> Nothing

actualKey :: k -> PersistentAssociation k v -> Maybe k
actualKey key (PersistentAssociation _ index) = HM.actualKey key index

first :: PersistentAssociation k v -> Maybe (k, v)
first (PersistentAssociation entries _) = entryPair <$> treeFirst entries

last :: PersistentAssociation k v -> Maybe (k, v)
last (PersistentAssociation entries _) = entryPair <$> treeLast entries

getAt :: Int -> PersistentAssociation k v -> Maybe (k, v)
getAt position (PersistentAssociation entries _) = entryPair <$> treeIndex position entries

indexOfKey :: k -> PersistentAssociation k v -> Maybe Int
indexOfKey key (PersistentAssociation entries index) =
  case HM.lookup key index of
    Just (Slot stamp _) -> treeSearchStamp stamp entries
    Nothing -> Nothing

setItem :: Eq v => k -> v -> PersistentAssociation k v -> PersistentAssociation k v
setItem key value association@(PersistentAssociation entries index) =
  case HM.lookup key index of
    Nothing -> appendNew key value association
    -- Equal-value update returns the receiver (spec: no-op identity for the
    -- association's SetItem, test-locked in the C# reference).
    Just (Slot _ storedValue) | storedValue == value -> association
    Just (Slot stamp _) ->
      let position = expectStamp stamp entries
          Entry _ storedKey _ = expectIndex position entries
          entries' = expectTree (treeSet position (Entry stamp storedKey value) entries)
          index' = HM.insert key (Slot stamp value) index
       in PersistentAssociation entries' index'

setItems :: Eq v => PersistentAssociation k v -> [(k, v)] -> PersistentAssociation k v
setItems = List.foldl' (\association (key, value) -> setItem key value association)

join :: Eq v => PersistentAssociation k v -> PersistentAssociation k v -> PersistentAssociation k v
join association other = setItems association (toList other)

append :: Eq v => k -> v -> PersistentAssociation k v -> PersistentAssociation k v
append key value association@(PersistentAssociation entries index) =
  case HM.lookup key index of
    Nothing -> appendNew key value association
    Just (Slot stamp storedValue) ->
      let position = expectStamp stamp entries
       in -- Rule-2 terminal no-op fast path (matches the C# reference): a key
          -- already last with an equal value returns the receiver, keeping
          -- the stored key instance and consuming no stamp.
          if position == treeSize entries - 1 && storedValue == value
            then association
            else
              let entries' = expectTree (treeDelete position entries)
                  index' = HM.delete key index
               in insertEntry entries' index' (treeSize entries') key value association

prepend :: Eq v => k -> v -> PersistentAssociation k v -> PersistentAssociation k v
prepend key value association@(PersistentAssociation entries index) =
  case HM.lookup key index of
    Nothing -> insertEntry entries index 0 key value association
    Just (Slot stamp storedValue) ->
      let position = expectStamp stamp entries
       in -- Rule-2 terminal no-op fast path; see append.
          if position == 0 && storedValue == value
            then association
            else
              let entries' = expectTree (treeDelete position entries)
                  index' = HM.delete key index
               in insertEntry entries' index' 0 key value association

insertAt :: Int -> k -> v -> PersistentAssociation k v -> Maybe (PersistentAssociation k v)
insertAt position key value association@(PersistentAssociation entries index)
  | position < 0 || position > treeSize entries = Nothing
  | otherwise =
      let (entries', index', insertPosition) =
            case HM.lookup key index of
              Nothing -> (entries, index, position)
              Just (Slot stamp _) ->
                let oldPosition = expectStamp stamp entries
                    adjusted = if oldPosition < position then position - 1 else position
                 in (expectTree (treeDelete oldPosition entries), HM.delete key index, adjusted)
       in Just (insertEntry entries' index' insertPosition key value association)

delete :: k -> PersistentAssociation k v -> PersistentAssociation k v
delete key association =
  case tryRemove key association of
    Just (_, rest) -> rest
    Nothing -> association

tryRemove :: k -> PersistentAssociation k v -> Maybe (v, PersistentAssociation k v)
tryRemove key (PersistentAssociation entries index) =
  case HM.tryRemove key index of
    Nothing -> Nothing
    Just (Slot stamp value, index') ->
      let entries' = expectTree (treeDelete (expectStamp stamp entries) entries)
       in Just (value, PersistentAssociation entries' index')

removeRange :: [k] -> PersistentAssociation k v -> PersistentAssociation k v
removeRange keysToRemove association = List.foldl' (flip delete) association keysToRemove

keyTake :: [k] -> PersistentAssociation k v -> PersistentAssociation k v
keyTake requested association@(PersistentAssociation _ index) = go requested (emptyWith (policy association))
  where
    go [] result = result
    go (key : rest) result
      | member key result = go rest result
      | otherwise =
          case HM.lookup key index of
            Just (Slot _ value) ->
              case HM.actualKey key index of
                Just storedKey -> go rest (appendNew storedKey value result)
                Nothing -> go rest result
            Nothing -> go rest result

deleteAt :: Int -> PersistentAssociation k v -> Maybe (PersistentAssociation k v)
deleteAt position (PersistentAssociation entries index) =
  case treeIndex position entries of
    Nothing -> Nothing
    Just (Entry _ key _) ->
      let entries' = expectTree (treeDelete position entries)
          index' = HM.delete key index
       in Just (PersistentAssociation entries' index')

removeFirst :: PersistentAssociation k v -> Maybe (PersistentAssociation k v)
removeFirst = deleteAt 0

removeLast :: PersistentAssociation k v -> Maybe (PersistentAssociation k v)
removeLast association = deleteAt (count association - 1) association

slice :: Int -> Int -> PersistentAssociation k v -> Maybe (PersistentAssociation k v)
slice position lengthValue association@(PersistentAssociation entries index)
  | position < 0 || lengthValue < 0 || position > treeSize entries || lengthValue > treeSize entries - position = Nothing
  | lengthValue == treeSize entries = Just association
  | otherwise =
      let (before, rest) = treeSplit position entries
          (kept, after) = treeSplit lengthValue rest
          removedCount = treeSize entries - treeSize kept
          index' =
            if treeSize kept <= removedCount
              then List.foldl' (\m entry -> HM.insert (entryKey entry) (entrySlot entry) m) (HM.emptyWith (policy association)) (treeToList kept)
              else List.foldl' (flip HM.delete) index (fmap entryKey (treeToList before ++ treeToList after))
       in Just (PersistentAssociation kept index')

take :: Int -> PersistentAssociation k v -> Maybe (PersistentAssociation k v)
take lengthValue = slice 0 lengthValue

drop :: Int -> PersistentAssociation k v -> Maybe (PersistentAssociation k v)
drop lengthValue association
  | lengthValue < 0 || lengthValue > count association = Nothing
  | otherwise = slice lengthValue (count association - lengthValue) association

reverse :: PersistentAssociation k v -> PersistentAssociation k v
reverse association
  | count association <= 1 = association
  | otherwise = rebuilt association (List.reverse (treeToList entries))
  where
    PersistentAssociation entries _ = association

keySort :: Ord k => PersistentAssociation k v -> PersistentAssociation k v
keySort = keySortBy compare

keySortBy :: (k -> k -> Ordering) -> PersistentAssociation k v -> PersistentAssociation k v
keySortBy comparison association =
  rebuilt association (List.sortBy compareEntries (treeToList entries))
  where
    PersistentAssociation entries _ = association
    compareEntries left right =
      case comparison (entryKey left) (entryKey right) of
        EQ -> compare (entryStamp left) (entryStamp right)
        order -> order

sort :: Ord v => PersistentAssociation k v -> PersistentAssociation k v
sort = sortBy compare

sortBy :: (v -> v -> Ordering) -> PersistentAssociation k v -> PersistentAssociation k v
sortBy comparison association =
  rebuilt association (List.sortBy compareEntries (treeToList entries))
  where
    PersistentAssociation entries _ = association
    compareEntries left right =
      case comparison (entryValue left) (entryValue right) of
        EQ -> compare (entryStamp left) (entryStamp right)
        order -> order

keys :: PersistentAssociation k v -> [k]
keys = fmap fst . toList

elems :: PersistentAssociation k v -> [v]
elems = fmap snd . toList

appendNew :: k -> v -> PersistentAssociation k v -> PersistentAssociation k v
appendNew key value association@(PersistentAssociation entries index) =
  insertEntry entries index (treeSize entries) key value association

insertEntry :: SeqTree (Entry k v) -> HM.HashMap k (Slot v) -> Int -> k -> v -> PersistentAssociation k v -> PersistentAssociation k v
insertEntry entries index position key value association =
  case pickStamp entries position of
    Nothing -> relabeled association entries position (Entry 0 key value)
    Just stamp ->
      let entry = Entry stamp key value
          entries' = expectTree (treeInsert position entry entries)
          index' = HM.insert key (Slot stamp value) index
       in PersistentAssociation entries' index'

relabeled :: PersistentAssociation k v -> SeqTree (Entry k v) -> Int -> Entry k v -> PersistentAssociation k v
relabeled association entries position inserted =
  rebuilt association (insertListAt position inserted (treeToList entries))

rebuilt :: PersistentAssociation k v -> [Entry k v] -> PersistentAssociation k v
rebuilt association ordered = PersistentAssociation entries index
  where
    relabeledEntries = zipWith relabel [0 ..] ordered
    entries = treeFromList relabeledEntries
    index = List.foldl' (\m entry -> HM.insert (entryKey entry) (entrySlot entry) m) (HM.emptyWith (policy association)) relabeledEntries
    relabel position (Entry _ key value) = Entry (position * stampGap) key value

pickStamp :: SeqTree (Entry k v) -> Int -> Maybe Int
pickStamp entries position
  | treeSize entries == 0 = Just 0
  | position == 0 =
      case treeFirst entries of
        Just (Entry firstStamp _ _)
          | toInteger firstStamp < toInteger (minBound :: Int) + toInteger stampGap -> Nothing
          | otherwise -> Just (firstStamp - stampGap)
        Nothing -> Just 0
  | position == treeSize entries =
      case treeLast entries of
        Just (Entry lastStamp _ _)
          | toInteger lastStamp > toInteger (maxBound :: Int) - toInteger stampGap -> Nothing
          | otherwise -> Just (lastStamp + stampGap)
        Nothing -> Just 0
  | otherwise =
      let Entry left _ _ = expectIndex (position - 1) entries
          Entry right _ _ = expectIndex position entries
          gap = toInteger right - toInteger left
       in if gap < 2
            then Nothing
            else Just (fromInteger ((toInteger left + toInteger right) `div` 2))

entryStamp :: Entry k v -> Int
entryStamp (Entry stamp _ _) = stamp

entryKey :: Entry k v -> k
entryKey (Entry _ key _) = key

entryValue :: Entry k v -> v
entryValue (Entry _ _ value) = value

entrySlot :: Entry k v -> Slot v
entrySlot (Entry stamp _ value) = Slot stamp value

entryPair :: Entry k v -> (k, v)
entryPair (Entry _ key value) = (key, value)

expectStamp :: Int -> SeqTree (Entry k v) -> Int
expectStamp stamp entries =
  case treeSearchStamp stamp entries of
    Just position -> position
    Nothing -> error "association stamp recorded in index is absent from order tree"

expectIndex :: Int -> SeqTree a -> a
expectIndex position entries =
  case treeIndex position entries of
    Just value -> value
    Nothing -> error "validated order-tree index was absent"

expectTree :: Maybe (SeqTree a) -> SeqTree a
expectTree =
  maybe (error "validated order-tree operation failed") id

treeSize :: SeqTree a -> Int
treeSize Empty = 0
treeSize (Node size _ _ _ _) = size

treeHeight :: SeqTree a -> Int
treeHeight Empty = 0
treeHeight (Node _ height _ _ _) = height

treeNode :: SeqTree a -> a -> SeqTree a -> SeqTree a
treeNode left value right = Node (treeSize left + 1 + treeSize right) (max (treeHeight left) (treeHeight right) + 1) left value right

treeBalance :: SeqTree a -> SeqTree a
treeBalance Empty = Empty
treeBalance node@(Node _ _ left value right)
  | treeHeight left > treeHeight right + 1 =
      case left of
        Empty -> node
        Node _ _ leftLeft _ leftRight
          | treeHeight leftRight > treeHeight leftLeft -> rotateRight (treeNode (rotateLeft left) value right)
          | otherwise -> rotateRight node
  | treeHeight right > treeHeight left + 1 =
      case right of
        Empty -> node
        Node _ _ rightLeft _ rightRight
          | treeHeight rightLeft > treeHeight rightRight -> rotateLeft (treeNode left value (rotateRight right))
          | otherwise -> rotateLeft node
  | otherwise = node

rotateRight :: SeqTree a -> SeqTree a
rotateRight (Node _ _ (Node _ _ leftLeft leftValue leftRight) value right) =
  treeNode leftLeft leftValue (treeNode leftRight value right)
rotateRight tree = tree

rotateLeft :: SeqTree a -> SeqTree a
rotateLeft (Node _ _ left value (Node _ _ rightLeft rightValue rightRight)) =
  treeNode (treeNode left value rightLeft) rightValue rightRight
rotateLeft tree = tree

treeConcat :: SeqTree a -> SeqTree a -> SeqTree a
treeConcat Empty right = right
treeConcat left Empty = left
treeConcat left@(Node _ _ leftLeft leftValue leftRight) right
  | treeHeight left > treeHeight right + 1 = treeBalance (treeNode leftLeft leftValue (treeConcat leftRight right))
treeConcat left right@(Node _ _ rightLeft rightValue rightRight)
  | treeHeight right > treeHeight left + 1 = treeBalance (treeNode (treeConcat left rightLeft) rightValue rightRight)
treeConcat left right =
  case treeRemoveFirst right of
    Just (value, right') -> treeBalance (treeNode left value right')
    Nothing -> left

treeRemoveFirst :: SeqTree a -> Maybe (a, SeqTree a)
treeRemoveFirst Empty = Nothing
treeRemoveFirst (Node _ _ Empty value right) = Just (value, right)
treeRemoveFirst (Node _ _ left value right) =
  case treeRemoveFirst left of
    Just (firstValue, left') -> Just (firstValue, treeBalance (treeNode left' value right))
    Nothing -> Just (value, right)

treeSplit :: Int -> SeqTree a -> (SeqTree a, SeqTree a)
treeSplit _ Empty = (Empty, Empty)
treeSplit position (Node _ _ left value right)
  | position <= leftSize =
      let (before, after) = treeSplit position left
       in (before, treeConcat after (treeNode Empty value right))
  | position == leftSize + 1 = (treeNode left value Empty, right)
  | otherwise =
      let (before, after) = treeSplit (position - leftSize - 1) right
       in (treeConcat (treeNode left value Empty) before, after)
  where
    leftSize = treeSize left

treeInsert :: Int -> a -> SeqTree a -> Maybe (SeqTree a)
treeInsert position value tree
  | position < 0 || position > treeSize tree = Nothing
  | otherwise =
      let (left, right) = treeSplit position tree
       in Just (treeConcat (treeConcat left (treeNode Empty value Empty)) right)

treeDelete :: Int -> SeqTree a -> Maybe (SeqTree a)
treeDelete position tree
  | position < 0 || position >= treeSize tree = Nothing
  | otherwise =
      let (before, rest) = treeSplit position tree
          (_, after) = treeSplit 1 rest
       in Just (treeConcat before after)

treeSet :: Int -> a -> SeqTree a -> Maybe (SeqTree a)
treeSet _ _ Empty = Nothing
treeSet position value (Node _ _ left old right)
  | position < 0 = Nothing
  | position < leftSize = (\left' -> treeBalance (treeNode left' old right)) <$> treeSet position value left
  | position == leftSize = Just (treeNode left value right)
  | otherwise = (\right' -> treeBalance (treeNode left old right')) <$> treeSet (position - leftSize - 1) value right
  where
    leftSize = treeSize left

treeIndex :: Int -> SeqTree a -> Maybe a
treeIndex _ Empty = Nothing
treeIndex position (Node _ _ left value right)
  | position < 0 = Nothing
  | position < leftSize = treeIndex position left
  | position == leftSize = Just value
  | otherwise = treeIndex (position - leftSize - 1) right
  where
    leftSize = treeSize left

treeFirst :: SeqTree a -> Maybe a
treeFirst Empty = Nothing
treeFirst (Node _ _ Empty value _) = Just value
treeFirst (Node _ _ left _ _) = treeFirst left

treeLast :: SeqTree a -> Maybe a
treeLast Empty = Nothing
treeLast (Node _ _ _ value Empty) = Just value
treeLast (Node _ _ _ _ right) = treeLast right

treeToList :: SeqTree a -> [a]
treeToList tree = go tree []
  where
    go Empty rest = rest
    go (Node _ _ left value right) rest = go left (value : go right rest)

treeFromList :: [a] -> SeqTree a
treeFromList values = fst (build (length values) values)
  where
    build 0 rest = (Empty, rest)
    build n rest =
      let leftCount = n `div` 2
          rightCount = n - leftCount - 1
          (left, afterLeft) = build leftCount rest
       in case afterLeft of
            [] -> (left, [])
            value : afterValue ->
              let (right, remaining) = build rightCount afterValue
               in (treeNode left value right, remaining)

-- Searches the stamp-ascending entry tree by stamp directly, avoiding the
-- previous sentinel entries with undefined key/value fields (and the
-- stamp-only Eq/Ord instances they required).
treeSearchStamp :: Int -> SeqTree (Entry k v) -> Maybe Int
treeSearchStamp stamp = go 0
  where
    go _ Empty = Nothing
    go offset (Node _ _ left value right) =
      case compare stamp (entryStamp value) of
        LT -> go offset left
        EQ -> Just (offset + treeSize left)
        GT -> go (offset + treeSize left + 1) right

insertListAt :: Int -> a -> [a] -> [a]
insertListAt 0 value values = value : values
insertListAt position value (x : rest) = x : insertListAt (position - 1) value rest
insertListAt _ value [] = [value]
