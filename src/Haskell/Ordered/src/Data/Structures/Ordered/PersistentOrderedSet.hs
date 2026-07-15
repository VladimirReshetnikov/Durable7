module Data.Structures.Ordered.PersistentOrderedSet
  ( PersistentOrderedSet
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , size
  , null
  , policy
  , first
  , last
  , at
  , contains
  , actualValue
  , indexOf
  , add
  , addFirst
  , insertAt
  , moveToFirst
  , moveToLast
  , moveTo
  , delete
  , tryRemove
  , deleteAt
  , removeFirst
  , removeLast
  , clear
  , getRange
  , take
  , drop
  , reverse
  , sort
  , sortBy
  , union
  , intersection
  , difference
  , symmetricDifference
  , isSubsetOf
  , isProperSubsetOf
  , isSupersetOf
  , isProperSupersetOf
  , overlaps
  , setEquals
  , toList
  , sharesIndexWith
  , validStructure
  ) where

import Prelude hiding (drop, last, null, reverse, take)

import Data.Int (Int64)
import qualified Data.List as List
import Data.Maybe (fromMaybe)

import qualified Data.Structures.FingerTree.Deque as Deque
import Data.Structures.Hamt.Hashable (Hashable)
import Data.Structures.Hamt.HashMap (HashMap, HashPolicy)
import qualified Data.Structures.Hamt.HashMap as HashMap

-- | An immutable insertion-ordered set. The deque owns ordered representatives;
-- the CHAMP index owns comparer-defined membership and private sparse labels.
data PersistentOrderedSet a = PersistentOrderedSet !(Deque.Deque (Entry a)) !(HashMap a Int64)

data Entry a = Entry
  { entryStamp :: !Int64
  , entryItem :: a
  }

-- This spacing is a private order-maintenance detail, not a public cadence.
stampStride :: Int64
stampStride = 1048576

-- | Creates an empty set with the package's natural hash policy.
empty :: (Eq a, Hashable a) => PersistentOrderedSet a
empty = emptyWith HashMap.defaultPolicy

-- | Creates an empty set that retains the supplied runtime hash policy.
emptyWith :: HashPolicy a -> PersistentOrderedSet a
emptyWith hashPolicy = PersistentOrderedSet Deque.empty (HashMap.emptyWith hashPolicy)

-- | Builds a set in first-occurrence order with first-representative semantics.
fromList :: (Eq a, Hashable a) => [a] -> PersistentOrderedSet a
fromList = fromListWith HashMap.defaultPolicy

-- | Builds a set under a runtime policy, retaining the first representative of
-- each receiver-policy equivalence class.
fromListWith :: HashPolicy a -> [a] -> PersistentOrderedSet a
fromListWith hashPolicy items = buildFromItems hashPolicy (distinctInOrder hashPolicy items)

size :: PersistentOrderedSet a -> Int
size (PersistentOrderedSet order _) = Deque.count order

null :: PersistentOrderedSet a -> Bool
null setValue = size setValue == 0

policy :: PersistentOrderedSet a -> HashPolicy a
policy (PersistentOrderedSet _ stamps) = HashMap.policy stamps

first :: PersistentOrderedSet a -> Maybe a
first (PersistentOrderedSet order _) = entryItem <$> Deque.first order

last :: PersistentOrderedSet a -> Maybe a
last (PersistentOrderedSet order _) = entryItem <$> Deque.last order

at :: Int -> PersistentOrderedSet a -> Maybe a
at index (PersistentOrderedSet order _) = entryItem <$> Deque.index index order

contains :: a -> PersistentOrderedSet a -> Bool
contains item (PersistentOrderedSet _ stamps) = HashMap.member item stamps

-- | Recovers the first stored representative equivalent to the lookup value.
actualValue :: a -> PersistentOrderedSet a -> Maybe a
actualValue item (PersistentOrderedSet _ stamps) = HashMap.actualKey item stamps

-- | Returns the representative position, or -1 when its class is absent.
indexOf :: a -> PersistentOrderedSet a -> Int
indexOf item setValue@(PersistentOrderedSet _ stamps) =
  case HashMap.lookup item stamps of
    Nothing -> -1
    Just stamp -> indexOfStamp stamp setValue

-- | Appends an absent class. Equivalent existing values neither move nor
-- replace their first stored representative.
add :: a -> PersistentOrderedSet a -> PersistentOrderedSet a
add item setValue = insertCore (size setValue) item setValue

addFirst :: a -> PersistentOrderedSet a -> PersistentOrderedSet a
addFirst = insertCore 0

-- | Inserts at a final position. Invalid positions are rejected before policy
-- callbacks; duplicate values are identity-preserving logical no-ops.
insertAt :: Int -> a -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
insertAt index item setValue
  | index < 0 || index > size setValue = Nothing
  | otherwise = Just (insertCore index item setValue)

moveToFirst :: a -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
moveToFirst = moveExisting 0

moveToLast :: a -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
moveToLast item setValue = moveExisting (size setValue - 1) item setValue

-- | Moves an existing class to its final result position while retaining its
-- stored representative. Invalid positions and absent classes return Nothing.
moveTo :: Int -> a -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
moveTo index item setValue
  | index < 0 || index >= size setValue = Nothing
  | otherwise = moveExisting index item setValue

delete :: a -> PersistentOrderedSet a -> PersistentOrderedSet a
delete item setValue = fromMaybe setValue (tryRemove item setValue)

tryRemove :: a -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
tryRemove item setValue@(PersistentOrderedSet order stamps) = do
  (stamp, nextStamps) <- HashMap.tryRemove item stamps
  let index = indexOfStamp stamp setValue
  nextOrder <- Deque.deleteAt index order
  pure (wrap nextOrder nextStamps)

deleteAt :: Int -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
deleteAt index (PersistentOrderedSet order stamps) = do
  entry <- Deque.index index order
  (stamp, nextStamps) <- HashMap.tryRemove (entryItem entry) stamps
  if stamp /= entryStamp entry
    then invariantFailure
    else do
      nextOrder <- Deque.deleteAt index order
      pure (wrap nextOrder nextStamps)

removeFirst :: PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
removeFirst = deleteAt 0

removeLast :: PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
removeLast setValue = deleteAt (size setValue - 1) setValue

clear :: PersistentOrderedSet a -> PersistentOrderedSet a
clear setValue
  | null setValue = setValue
  | otherwise = emptyWith (policy setValue)

-- | Extracts a contiguous range. The full range reuses the receiver; an empty
-- range retains the receiver policy. Invalid ranges return Nothing.
getRange :: Int -> Int -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
getRange index count setValue@(PersistentOrderedSet order stamps)
  | index < 0 || index > total = Nothing
  | count < 0 || count > total - index = Nothing
  | count == total = Just setValue
  | count == 0 = Just (emptyWith (policy setValue))
  | otherwise = do
      (before, suffix) <- Deque.splitAt index order
      (kept, after) <- Deque.splitAt count suffix
      let removedCount = total - count
          nextStamps
            | count <= removedCount = buildIndex kept (policy setValue)
            | otherwise = removeEntries after (removeEntries before stamps)
      pure (PersistentOrderedSet kept nextStamps)
  where
    total = size setValue

take :: Int -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
take count setValue
  | count < 0 || count > size setValue = Nothing
  | otherwise = getRange 0 count setValue

drop :: Int -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
drop count setValue
  | count < 0 || count > size setValue = Nothing
  | otherwise = getRange count (size setValue - count) setValue

reverse :: PersistentOrderedSet a -> PersistentOrderedSet a
reverse setValue
  | size setValue <= 1 = setValue
  | otherwise = buildFromItems (policy setValue) (List.reverse (toList setValue))

sort :: Ord a => PersistentOrderedSet a -> PersistentOrderedSet a
sort = sortBy compare

-- | Performs one stable comparison reorder without changing membership policy.
-- Later additions append normally; this is not a comparison-sorted set.
sortBy :: (a -> a -> Ordering) -> PersistentOrderedSet a -> PersistentOrderedSet a
sortBy comparison setValue@(PersistentOrderedSet order _)
  | size setValue <= 1 = setValue
  | stampsAscending sorted = setValue
  | otherwise = buildFromItems (policy setValue) (map entryItem sorted)
  where
    sorted = List.sortBy compareEntry (Deque.toList order)
    compareEntry left right =
      case comparison (entryItem left) (entryItem right) of
        EQ -> compare (entryStamp left) (entryStamp right)
        result -> result

-- | Receiver-policy union: receiver order first, then new argument classes in
-- first-occurrence argument order.
union :: [a] -> PersistentOrderedSet a -> PersistentOrderedSet a
union other setValue@(PersistentOrderedSet _ stamps) =
  case additions of
    [] -> setValue
    _ -> buildFromItems (policy setValue) (toList setValue ++ additions)
  where
    (argumentItems, _) = normalize (policy setValue) other
    additions = filter (\item -> not (HashMap.member item stamps)) argumentItems

intersection :: [a] -> PersistentOrderedSet a -> PersistentOrderedSet a
intersection other setValue
  | length retained == size setValue = setValue
  | otherwise = buildFromItems (policy setValue) retained
  where
    (_, membership) = normalize (policy setValue) other
    retained = filter (`HashMap.member` membership) (toList setValue)

difference :: [a] -> PersistentOrderedSet a -> PersistentOrderedSet a
difference other setValue
  | length retained == size setValue = setValue
  | otherwise = buildFromItems (policy setValue) retained
  where
    (_, membership) = normalize (policy setValue) other
    retained = filter (\item -> not (HashMap.member item membership)) (toList setValue)

symmetricDifference :: [a] -> PersistentOrderedSet a -> PersistentOrderedSet a
symmetricDifference other setValue@(PersistentOrderedSet _ stamps)
  | List.null argumentItems = setValue
  | otherwise = buildFromItems (policy setValue) (receiverOnly ++ argumentOnly)
  where
    (argumentItems, membership) = normalize (policy setValue) other
    receiverOnly = filter (\item -> not (HashMap.member item membership)) (toList setValue)
    argumentOnly = filter (\item -> not (HashMap.member item stamps)) argumentItems

isSubsetOf :: [a] -> PersistentOrderedSet a -> Bool
isSubsetOf other setValue =
  size setValue <= length argumentItems
    && all (`HashMap.member` membership) (toList setValue)
  where
    (argumentItems, membership) = normalize (policy setValue) other

isProperSubsetOf :: [a] -> PersistentOrderedSet a -> Bool
isProperSubsetOf other setValue =
  size setValue < length argumentItems
    && all (`HashMap.member` membership) (toList setValue)
  where
    (argumentItems, membership) = normalize (policy setValue) other

isSupersetOf :: [a] -> PersistentOrderedSet a -> Bool
isSupersetOf other setValue@(PersistentOrderedSet _ stamps) =
  size setValue >= length argumentItems
    && all (`HashMap.member` stamps) argumentItems
  where
    (argumentItems, _) = normalize (policy setValue) other

isProperSupersetOf :: [a] -> PersistentOrderedSet a -> Bool
isProperSupersetOf other setValue@(PersistentOrderedSet _ stamps) =
  size setValue > length argumentItems
    && all (`HashMap.member` stamps) argumentItems
  where
    (argumentItems, _) = normalize (policy setValue) other

overlaps :: [a] -> PersistentOrderedSet a -> Bool
overlaps other setValue@(PersistentOrderedSet _ stamps) =
  any (`HashMap.member` stamps) argumentItems
  where
    (argumentItems, _) = normalize (policy setValue) other

setEquals :: [a] -> PersistentOrderedSet a -> Bool
setEquals other setValue@(PersistentOrderedSet _ stamps) =
  size setValue == length argumentItems
    && all (`HashMap.member` stamps) argumentItems
  where
    (argumentItems, _) = normalize (policy setValue) other

toList :: PersistentOrderedSet a -> [a]
toList (PersistentOrderedSet order _) = map entryItem (Deque.toList order)

-- | Tests exact CHAMP-root reuse for identity-sensitive persistence checks.
sharesIndexWith :: PersistentOrderedSet a -> PersistentOrderedSet a -> Bool
sharesIndexWith (PersistentOrderedSet _ left) (PersistentOrderedSet _ right) =
  HashMap.sharesRootWith left right

-- | Checks both substrate invariants and the Ordered-owned dual-index contract.
validStructure :: PersistentOrderedSet a -> Bool
validStructure (PersistentOrderedSet order stamps) =
  HashMap.validStructure stamps
    && Deque.count order == HashMap.size stamps
    && stampsAscending entries
    && all entryMatches entries
  where
    entries = Deque.toList order
    hashPolicy = HashMap.policy stamps
    entryMatches entry =
      HashMap.lookup (entryItem entry) stamps == Just (entryStamp entry)
        && case HashMap.actualKey (entryItem entry) stamps of
             Just stored -> HashMap.equalKeys hashPolicy stored (entryItem entry)
             Nothing -> False

insertCore :: Int -> a -> PersistentOrderedSet a -> PersistentOrderedSet a
insertCore index item setValue@(PersistentOrderedSet order stamps) =
  case pickStamp order index of
    Just stamp
      | HashMap.member item stamps -> setValue
      | otherwise ->
          let nextOrder = insertEntry index (Entry stamp item) order
              nextStamps = HashMap.insert item stamp stamps
           in PersistentOrderedSet nextOrder nextStamps
    Nothing
      | HashMap.member item stamps -> setValue
      | otherwise -> rebuildInserted order index item (policy setValue)

moveExisting :: Int -> a -> PersistentOrderedSet a -> Maybe (PersistentOrderedSet a)
moveExisting finalIndex item setValue@(PersistentOrderedSet order stamps) = do
  oldStamp <- HashMap.lookup item stamps
  let oldIndex = indexOfStamp oldStamp setValue
  if oldIndex == finalIndex
    then Just setValue
    else do
      oldEntry <- Deque.index oldIndex order
      trimmed <- Deque.deleteAt oldIndex order
      let stored = entryItem oldEntry
      case pickStamp trimmed finalIndex of
        Nothing -> Just (rebuildInserted trimmed finalIndex stored (policy setValue))
        Just newStamp ->
          let nextOrder = insertEntry finalIndex (Entry newStamp stored) trimmed
              nextStamps = HashMap.insert stored newStamp stamps
           in Just (PersistentOrderedSet nextOrder nextStamps)

indexOfStamp :: Int64 -> PersistentOrderedSet a -> Int
indexOfStamp stamp (PersistentOrderedSet order _) =
  let probe = Entry stamp (error "Ordered stamp probe item was evaluated")
      index = Deque.sortedLowerBoundBy compareStamp probe order
   in case Deque.index index order of
        Just entry | entryStamp entry == stamp -> index
        _ -> invariantFailure
  where
    compareStamp left right = compare (entryStamp left) (entryStamp right)

pickStamp :: Deque.Deque (Entry a) -> Int -> Maybe Int64
pickStamp order index
  | Deque.null order = Just 0
  | index == 0 = do
      firstEntry <- Deque.first order
      let value = entryStamp firstEntry
      if value < minBound + stampStride then Nothing else Just (value - stampStride)
  | index == Deque.count order = do
      lastEntry <- Deque.last order
      let value = entryStamp lastEntry
      if value > maxBound - stampStride then Nothing else Just (value + stampStride)
  | otherwise = do
      leftEntry <- Deque.index (index - 1) order
      rightEntry <- Deque.index index order
      let left = toInteger (entryStamp leftEntry)
          right = toInteger (entryStamp rightEntry)
          gap = right - left
      if gap < 2
        then Nothing
        else Just (fromInteger (left + gap `div` 2))

insertEntry :: Int -> Entry a -> Deque.Deque (Entry a) -> Deque.Deque (Entry a)
insertEntry index entry order =
  fromMaybe invariantFailure (Deque.insertAt index entry order)

rebuildInserted :: Deque.Deque (Entry a) -> Int -> a -> HashPolicy a -> PersistentOrderedSet a
rebuildInserted order index item hashPolicy =
  let (before, after) = List.splitAt index (map entryItem (Deque.toList order))
   in buildFromItems hashPolicy (before ++ (item : after))

buildFromItems :: HashPolicy a -> [a] -> PersistentOrderedSet a
buildFromItems hashPolicy items =
  let entries = zipWith makeEntry [0 :: Int ..] items
      pairs = map (\entry -> (entryItem entry, entryStamp entry)) entries
   in PersistentOrderedSet
        (Deque.fromList entries)
        (HashMap.fromListWith hashPolicy pairs)
  where
    makeEntry index item = Entry (stampForIndex index) item

buildIndex :: Deque.Deque (Entry a) -> HashPolicy a -> HashMap a Int64
buildIndex order hashPolicy =
  HashMap.fromListWith hashPolicy
    [ (entryItem entry, entryStamp entry)
    | entry <- Deque.toList order
    ]

removeEntries :: Deque.Deque (Entry a) -> HashMap a Int64 -> HashMap a Int64
removeEntries order stamps =
  List.foldl'
    (\current entry -> HashMap.delete (entryItem entry) current)
    stamps
    (Deque.toList order)

wrap :: Deque.Deque (Entry a) -> HashMap a Int64 -> PersistentOrderedSet a
wrap = PersistentOrderedSet

distinctInOrder :: HashPolicy a -> [a] -> [a]
distinctInOrder hashPolicy items = fst (normalize hashPolicy items)

normalize :: HashPolicy a -> [a] -> ([a], HashMap a ())
normalize hashPolicy items =
  let (itemsReversed, membership) = List.foldl' addDistinct ([], HashMap.emptyWith hashPolicy) items
   in (List.reverse itemsReversed, membership)
  where
    addDistinct state@(kept, membership) item
      | HashMap.member item membership = state
      | otherwise = (item : kept, HashMap.insert item () membership)

stampsAscending :: [Entry a] -> Bool
stampsAscending entries =
  and (zipWith (\left right -> entryStamp left < entryStamp right) entries (List.drop 1 entries))

stampForIndex :: Int -> Int64
stampForIndex index
  | stamp > toInteger (maxBound :: Int64) = error "PersistentOrderedSet stamp space exhausted"
  | otherwise = fromInteger stamp
  where
    stamp = toInteger index * toInteger stampStride

invariantFailure :: a
invariantFailure = error "PersistentOrderedSet membership and order indexes disagree"
