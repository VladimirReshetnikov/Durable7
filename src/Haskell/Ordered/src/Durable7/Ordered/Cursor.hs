-- | Immutable snapshot-plus-gap cursors for neutral insertion-ordered collections.
-- | Gap cursors over the insertion-ordered collections.
module Durable7.Ordered.Cursor
  ( OrderedCursorSearch(..)
  , OrderedCursorInsert(..)
  , PersistentOrderedSetCursor
  , orderedSetCursorAt
  , findOrderedSetCursor
  , orderedSetCursorPosition
  , orderedSetCursorSize
  , orderedSetCursorAtStart
  , orderedSetCursorAtEnd
  , orderedSetCursorPeekPrevious
  , orderedSetCursorPeekNext
  , orderedSetCursorMovePrevious
  , orderedSetCursorMoveNext
  , orderedSetCursorSeek
  , orderedSetCursorInsert
  , orderedSetCursorTryInsert
  , orderedSetCursorDeletePrevious
  , orderedSetCursorDeleteNext
  , orderedSetCursorSnapshot
  , PersistentOrderedMapCursor
  , orderedMapCursorAt
  , findOrderedMapCursor
  , orderedMapCursorPosition
  , orderedMapCursorSize
  , orderedMapCursorAtStart
  , orderedMapCursorAtEnd
  , orderedMapCursorPeekPrevious
  , orderedMapCursorPeekNext
  , orderedMapCursorMovePrevious
  , orderedMapCursorMoveNext
  , orderedMapCursorSeek
  , orderedMapCursorInsert
  , orderedMapCursorTryInsert
  , orderedMapCursorSetNextValue
  , orderedMapCursorDeletePrevious
  , orderedMapCursorDeleteNext
  , orderedMapCursorSnapshot
  , PersistentOrderedMultimapCursor
  , orderedMultimapCursorAt
  , findOrderedMultimapCursor
  , findOrderedMultimapGroupCursor
  , orderedMultimapCursorPosition
  , orderedMultimapCursorSize
  , orderedMultimapCursorAtStart
  , orderedMultimapCursorAtEnd
  , orderedMultimapCursorPeekPrevious
  , orderedMultimapCursorPeekNext
  , orderedMultimapCursorMovePrevious
  , orderedMultimapCursorMoveNext
  , orderedMultimapCursorSeek
  , orderedMultimapCursorInsert
  , orderedMultimapCursorTryInsert
  , orderedMultimapCursorDeletePrevious
  , orderedMultimapCursorDeleteNext
  , orderedMultimapCursorSnapshot
  ) where

import Durable7.Hamt.HashMap (equalKeys)
import Durable7.Ordered.PersistentOrderedMap (PersistentOrderedMap)
import qualified Durable7.Ordered.PersistentOrderedMap as OrderedMap
import Durable7.Ordered.PersistentOrderedMultimap (PersistentOrderedMultimap)
import qualified Durable7.Ordered.PersistentOrderedMultimap as OrderedMultimap
import Durable7.Ordered.PersistentOrderedSet (PersistentOrderedSet)
import qualified Durable7.Ordered.PersistentOrderedSet as OrderedSet

-- | Presence-discriminated cursor lookup.
data OrderedCursorSearch cursor = OrderedCursorSearch
  { orderedCursorFound :: !Bool
  , orderedSearchCursor :: cursor
  }

-- | Result of a non-overwriting cursor insertion.
data OrderedCursorInsert cursor = OrderedCursorInsert
  { orderedCursorAdded :: !Bool
  , orderedInsertionCursor :: cursor
  }

validPosition :: Int -> Int -> Bool
validPosition position total = position >= 0 && position <= total

checkedSuccessor :: Int -> Int
checkedSuccessor position
  | position == maxBound = error "ordered cursor position overflow"
  | otherwise = position + 1

-- Ordered set

-- | Immutable root-plus-explicit-order-position gap cursor.
data PersistentOrderedSetCursor a = PersistentOrderedSetCursor !(PersistentOrderedSet a) !Int

-- | A cursor at the given gap of the set.
orderedSetCursorAt :: Int -> PersistentOrderedSet a -> Maybe (PersistentOrderedSetCursor a)
orderedSetCursorAt position value
  | validPosition position (OrderedSet.size value) = Just (PersistentOrderedSetCursor value position)
  | otherwise = Nothing

-- | Focuses an equality class; a miss returns the append-position cursor.
findOrderedSetCursor :: a -> PersistentOrderedSet a -> OrderedCursorSearch (PersistentOrderedSetCursor a)
findOrderedSetCursor item value =
  let index = OrderedSet.indexOf item value
   in OrderedCursorSearch (index >= 0) (PersistentOrderedSetCursor value (if index < 0 then OrderedSet.size value else index))

-- | The cursor's gap position.
orderedSetCursorPosition :: PersistentOrderedSetCursor a -> Int
orderedSetCursorPosition (PersistentOrderedSetCursor _ position) = position

-- | Number of elements in the version the cursor is positioned in.
orderedSetCursorSize :: PersistentOrderedSetCursor a -> Int
orderedSetCursorSize (PersistentOrderedSetCursor value _) = OrderedSet.size value

-- | A cursor before the first element.
orderedSetCursorAtStart :: PersistentOrderedSetCursor a -> Bool
orderedSetCursorAtStart cursor = orderedSetCursorPosition cursor == 0

-- | A cursor after the last element.
orderedSetCursorAtEnd :: PersistentOrderedSetCursor a -> Bool
orderedSetCursorAtEnd cursor = orderedSetCursorPosition cursor == orderedSetCursorSize cursor

-- | The element immediately before the gap, or `Nothing` at the start.
orderedSetCursorPeekPrevious :: PersistentOrderedSetCursor a -> Maybe a
orderedSetCursorPeekPrevious (PersistentOrderedSetCursor value position)
  | position == 0 = Nothing
  | otherwise = OrderedSet.at (position - 1) value

-- | The element immediately after the gap, or `Nothing` at the end.
orderedSetCursorPeekNext :: PersistentOrderedSetCursor a -> Maybe a
orderedSetCursorPeekNext cursor@(PersistentOrderedSetCursor value position)
  | orderedSetCursorAtEnd cursor = Nothing
  | otherwise = OrderedSet.at position value

-- | A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
-- the same version.
orderedSetCursorMovePrevious :: PersistentOrderedSetCursor a -> Maybe (PersistentOrderedSetCursor a)
orderedSetCursorMovePrevious (PersistentOrderedSetCursor value position)
  | position == 0 = Nothing
  | otherwise = Just (PersistentOrderedSetCursor value (position - 1))

-- | A cursor one position later. The receiver is unchanged.
orderedSetCursorMoveNext :: PersistentOrderedSetCursor a -> Maybe (PersistentOrderedSetCursor a)
orderedSetCursorMoveNext cursor@(PersistentOrderedSetCursor value position)
  | orderedSetCursorAtEnd cursor = Nothing
  | otherwise = Just (PersistentOrderedSetCursor value (position + 1))

-- | A cursor at the given position within the same set version.
orderedSetCursorSeek :: Int -> PersistentOrderedSetCursor a -> Maybe (PersistentOrderedSetCursor a)
orderedSetCursorSeek position (PersistentOrderedSetCursor value _) = orderedSetCursorAt position value

-- | Inserts at the gap. An equivalent stored representative preserves the gap.
orderedSetCursorInsert :: a -> PersistentOrderedSetCursor a -> PersistentOrderedSetCursor a
orderedSetCursorInsert item cursor@(PersistentOrderedSetCursor value position)
  | OrderedSet.contains item value = cursor
  | otherwise =
      case OrderedSet.insertAt position item value of
        Just next -> PersistentOrderedSetCursor next (checkedSuccessor position)
        Nothing -> error "validated ordered-set cursor insertion failed"

-- | Inserts the element at the gap unless it is present, producing a new version the returned
-- cursor is positioned in.
orderedSetCursorTryInsert :: a -> PersistentOrderedSetCursor a -> OrderedCursorInsert (PersistentOrderedSetCursor a)
orderedSetCursorTryInsert item cursor@(PersistentOrderedSetCursor value _)
  | OrderedSet.contains item value = OrderedCursorInsert False cursor
  | otherwise = OrderedCursorInsert True (orderedSetCursorInsert item cursor)

-- | Removes the element before the gap, producing a new version the returned cursor is positioned
-- in.
orderedSetCursorDeletePrevious :: PersistentOrderedSetCursor a -> Maybe (PersistentOrderedSetCursor a)
orderedSetCursorDeletePrevious (PersistentOrderedSetCursor value position)
  | position == 0 = Nothing
  | otherwise = (`PersistentOrderedSetCursor` (position - 1)) <$> OrderedSet.deleteAt (position - 1) value

-- | Removes the element after the gap, producing a new version the returned cursor is positioned
-- in.
orderedSetCursorDeleteNext :: PersistentOrderedSetCursor a -> Maybe (PersistentOrderedSetCursor a)
orderedSetCursorDeleteNext cursor@(PersistentOrderedSetCursor value position)
  | orderedSetCursorAtEnd cursor = Nothing
  | otherwise = (`PersistentOrderedSetCursor` position) <$> OrderedSet.deleteAt position value

-- | The set version this cursor is positioned in.
orderedSetCursorSnapshot :: PersistentOrderedSetCursor a -> PersistentOrderedSet a
orderedSetCursorSnapshot (PersistentOrderedSetCursor value _) = value

-- Ordered map

-- | Immutable root-plus-explicit-order-position map gap cursor.
data PersistentOrderedMapCursor k v = PersistentOrderedMapCursor !(PersistentOrderedMap k v) !Int

-- | A cursor at the given gap of the map.
orderedMapCursorAt :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorAt position value
  | validPosition position (OrderedMap.size value) = Just (PersistentOrderedMapCursor value position)
  | otherwise = Nothing

-- | Focuses an equivalent key; a miss returns the append-position cursor.
findOrderedMapCursor :: k -> PersistentOrderedMap k v -> OrderedCursorSearch (PersistentOrderedMapCursor k v)
findOrderedMapCursor key value =
  let index = OrderedMap.indexOf key value
   in OrderedCursorSearch (index >= 0) (PersistentOrderedMapCursor value (if index < 0 then OrderedMap.size value else index))

-- | The cursor's gap position.
orderedMapCursorPosition :: PersistentOrderedMapCursor k v -> Int
orderedMapCursorPosition (PersistentOrderedMapCursor _ position) = position

-- | Number of entries in the version the cursor is positioned in.
orderedMapCursorSize :: PersistentOrderedMapCursor k v -> Int
orderedMapCursorSize (PersistentOrderedMapCursor value _) = OrderedMap.size value

-- | A cursor before the first entry.
orderedMapCursorAtStart :: PersistentOrderedMapCursor k v -> Bool
orderedMapCursorAtStart cursor = orderedMapCursorPosition cursor == 0

-- | A cursor after the last entry.
orderedMapCursorAtEnd :: PersistentOrderedMapCursor k v -> Bool
orderedMapCursorAtEnd cursor = orderedMapCursorPosition cursor == orderedMapCursorSize cursor

-- | The entry immediately before the gap, or `Nothing` at the start.
orderedMapCursorPeekPrevious :: PersistentOrderedMapCursor k v -> Maybe (k, v)
orderedMapCursorPeekPrevious (PersistentOrderedMapCursor value position)
  | position == 0 = Nothing
  | otherwise = OrderedMap.entryAt (position - 1) value

-- | The entry immediately after the gap, or `Nothing` at the end.
orderedMapCursorPeekNext :: PersistentOrderedMapCursor k v -> Maybe (k, v)
orderedMapCursorPeekNext cursor@(PersistentOrderedMapCursor value position)
  | orderedMapCursorAtEnd cursor = Nothing
  | otherwise = OrderedMap.entryAt position value

-- | A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
-- the same version.
orderedMapCursorMovePrevious :: PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorMovePrevious (PersistentOrderedMapCursor value position)
  | position == 0 = Nothing
  | otherwise = Just (PersistentOrderedMapCursor value (position - 1))

-- | A cursor one position later. The receiver is unchanged.
orderedMapCursorMoveNext :: PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorMoveNext cursor@(PersistentOrderedMapCursor value position)
  | orderedMapCursorAtEnd cursor = Nothing
  | otherwise = Just (PersistentOrderedMapCursor value (position + 1))

-- | A cursor at the given position within the same map version.
orderedMapCursorSeek :: Int -> PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorSeek position (PersistentOrderedMapCursor value _) = orderedMapCursorAt position value

-- | Strictly inserts an absent entry at the gap and returns its following gap.
orderedMapCursorInsert :: k -> v -> PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorInsert key item (PersistentOrderedMapCursor value position) = do
  next <- OrderedMap.insertAt position key item value
  pure (PersistentOrderedMapCursor next (checkedSuccessor position))

-- | A duplicate focuses the stored entry without overwriting its payload.
orderedMapCursorTryInsert :: k -> v -> PersistentOrderedMapCursor k v -> OrderedCursorInsert (PersistentOrderedMapCursor k v)
orderedMapCursorTryInsert key item cursor@(PersistentOrderedMapCursor value _) =
  let index = OrderedMap.indexOf key value
   in if index >= 0
        then OrderedCursorInsert False (PersistentOrderedMapCursor value index)
        else case orderedMapCursorInsert key item cursor of
          Just next -> OrderedCursorInsert True next
          Nothing -> error "validated ordered-map cursor insertion failed"

-- | Replaces the value of the entry after the gap, producing a new version the returned cursor is
-- positioned in.
orderedMapCursorSetNextValue :: v -> PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorSetNextValue item cursor@(PersistentOrderedMapCursor value position) = do
  (key, _) <- orderedMapCursorPeekNext cursor
  pure (PersistentOrderedMapCursor (OrderedMap.set key item value) position)

-- | Removes the entry before the gap, producing a new version the returned cursor is positioned in.
orderedMapCursorDeletePrevious :: PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorDeletePrevious (PersistentOrderedMapCursor value position)
  | position == 0 = Nothing
  | otherwise = (`PersistentOrderedMapCursor` (position - 1)) <$> OrderedMap.deleteAt (position - 1) value

-- | Removes the entry after the gap, producing a new version the returned cursor is positioned in.
orderedMapCursorDeleteNext :: PersistentOrderedMapCursor k v -> Maybe (PersistentOrderedMapCursor k v)
orderedMapCursorDeleteNext cursor@(PersistentOrderedMapCursor value position)
  | orderedMapCursorAtEnd cursor = Nothing
  | otherwise = (`PersistentOrderedMapCursor` position) <$> OrderedMap.deleteAt position value

-- | The map version this cursor is positioned in.
orderedMapCursorSnapshot :: PersistentOrderedMapCursor k v -> PersistentOrderedMap k v
orderedMapCursorSnapshot (PersistentOrderedMapCursor value _) = value

-- Ordered multimap

-- | Immutable root-plus-flattened-key-grouped-pair-rank gap cursor.
data PersistentOrderedMultimapCursor k v =
  PersistentOrderedMultimapCursor !(PersistentOrderedMultimap k v) !Int

-- | A cursor at the given gap of the multimap.
orderedMultimapCursorAt :: Int -> PersistentOrderedMultimap k v -> Maybe (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorAt position value
  | validPosition position (OrderedMultimap.size value) = Just (PersistentOrderedMultimapCursor value position)
  | otherwise = Nothing

-- | Focuses an equivalent pair; a miss returns the append-position cursor.
findOrderedMultimapCursor :: k -> v -> PersistentOrderedMultimap k v -> OrderedCursorSearch (PersistentOrderedMultimapCursor k v)
findOrderedMultimapCursor key item value =
  let index = orderedMultimapIndexOf key item value
   in OrderedCursorSearch (index >= 0) (PersistentOrderedMultimapCursor value (if index < 0 then OrderedMultimap.size value else index))

-- | Focuses the first pair of an equivalent key group; a miss returns the end cursor.
findOrderedMultimapGroupCursor :: k -> PersistentOrderedMultimap k v -> OrderedCursorSearch (PersistentOrderedMultimapCursor k v)
findOrderedMultimapGroupCursor key value = search 0 (OrderedMultimap.toList value)
  where
    equivalent = equalKeys (OrderedMultimap.keyPolicy value)
    search _ [] = OrderedCursorSearch False (PersistentOrderedMultimapCursor value (OrderedMultimap.size value))
    search index ((storedKey, _) : rest)
      | equivalent storedKey key = OrderedCursorSearch True (PersistentOrderedMultimapCursor value index)
      | otherwise = search (checkedSuccessor index) rest

-- | The cursor's gap position.
orderedMultimapCursorPosition :: PersistentOrderedMultimapCursor k v -> Int
orderedMultimapCursorPosition (PersistentOrderedMultimapCursor _ position) = position

-- | Number of pairs in the version the cursor is positioned in.
orderedMultimapCursorSize :: PersistentOrderedMultimapCursor k v -> Int
orderedMultimapCursorSize (PersistentOrderedMultimapCursor value _) = OrderedMultimap.size value

-- | A cursor before the first pair.
orderedMultimapCursorAtStart :: PersistentOrderedMultimapCursor k v -> Bool
orderedMultimapCursorAtStart cursor = orderedMultimapCursorPosition cursor == 0

-- | A cursor after the last pair.
orderedMultimapCursorAtEnd :: PersistentOrderedMultimapCursor k v -> Bool
orderedMultimapCursorAtEnd cursor = orderedMultimapCursorPosition cursor == orderedMultimapCursorSize cursor

-- | The pair immediately before the gap, or `Nothing` at the start.
orderedMultimapCursorPeekPrevious :: PersistentOrderedMultimapCursor k v -> Maybe (k, v)
orderedMultimapCursorPeekPrevious (PersistentOrderedMultimapCursor value position)
  | position == 0 = Nothing
  | otherwise = listAt (position - 1) (OrderedMultimap.toList value)

-- | The pair immediately after the gap, or `Nothing` at the end.
orderedMultimapCursorPeekNext :: PersistentOrderedMultimapCursor k v -> Maybe (k, v)
orderedMultimapCursorPeekNext cursor@(PersistentOrderedMultimapCursor value position)
  | orderedMultimapCursorAtEnd cursor = Nothing
  | otherwise = listAt position (OrderedMultimap.toList value)

-- | A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
-- the same version.
orderedMultimapCursorMovePrevious :: PersistentOrderedMultimapCursor k v -> Maybe (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorMovePrevious (PersistentOrderedMultimapCursor value position)
  | position == 0 = Nothing
  | otherwise = Just (PersistentOrderedMultimapCursor value (position - 1))

-- | A cursor one position later. The receiver is unchanged.
orderedMultimapCursorMoveNext :: PersistentOrderedMultimapCursor k v -> Maybe (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorMoveNext cursor@(PersistentOrderedMultimapCursor value position)
  | orderedMultimapCursorAtEnd cursor = Nothing
  | otherwise = Just (PersistentOrderedMultimapCursor value (position + 1))

-- | A cursor at the given position within the same multimap version.
orderedMultimapCursorSeek :: Int -> PersistentOrderedMultimapCursor k v -> Maybe (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorSeek position (PersistentOrderedMultimapCursor value _) = orderedMultimapCursorAt position value

-- | Inserts according to grouped semantics; an equivalent pair preserves this gap.
--
-- 'OrderedMultimap.insert' appends the value to the end of the key's group, or
-- appends a fresh last group when the key is absent, so the following gap is a
-- group boundary.  Deriving it from the group boundaries rather than re-scanning
-- for the pair keeps the function total: a value that is not reflexive under the
-- value policy, such as a @NaN@, is one the collection accepts but a content
-- re-scan can never find again, and the pure signature offers callers no failure
-- channel for that.
orderedMultimapCursorInsert :: k -> v -> PersistentOrderedMultimapCursor k v -> PersistentOrderedMultimapCursor k v
orderedMultimapCursorInsert key item cursor@(PersistentOrderedMultimapCursor value _)
  | OrderedMultimap.member key item value = cursor
  | otherwise =
      let next = OrderedMultimap.insert key item value
       in PersistentOrderedMultimapCursor next (orderedMultimapGroupEnd key next)

-- | Inserts the pair at the gap unless it is present, producing a new version the returned cursor
-- is positioned in.
orderedMultimapCursorTryInsert :: k -> v -> PersistentOrderedMultimapCursor k v -> OrderedCursorInsert (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorTryInsert key item cursor@(PersistentOrderedMultimapCursor value _)
  | OrderedMultimap.member key item value = OrderedCursorInsert False cursor
  | otherwise = OrderedCursorInsert True (orderedMultimapCursorInsert key item cursor)

-- | Removes the pair before the gap, producing a new version the returned cursor is positioned in.
orderedMultimapCursorDeletePrevious :: PersistentOrderedMultimapCursor k v -> Maybe (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorDeletePrevious cursor@(PersistentOrderedMultimapCursor value position) = do
  (key, item) <- orderedMultimapCursorPeekPrevious cursor
  next <- orderedMultimapDeletedPair key item value
  pure (PersistentOrderedMultimapCursor next (position - 1))

-- | Removes the pair after the gap, producing a new version the returned cursor is positioned in.
orderedMultimapCursorDeleteNext :: PersistentOrderedMultimapCursor k v -> Maybe (PersistentOrderedMultimapCursor k v)
orderedMultimapCursorDeleteNext cursor@(PersistentOrderedMultimapCursor value position) = do
  (key, item) <- orderedMultimapCursorPeekNext cursor
  next <- orderedMultimapDeletedPair key item value
  pure (PersistentOrderedMultimapCursor next position)

-- | The multimap version this cursor is positioned in.
orderedMultimapCursorSnapshot :: PersistentOrderedMultimapCursor k v -> PersistentOrderedMultimap k v
orderedMultimapCursorSnapshot (PersistentOrderedMultimapCursor value _) = value

-- | Removes one pair and reports the successor version, or 'Nothing' when the
-- collection did not change.
--
-- 'OrderedMultimap.delete' locates the pair by content and returns its receiver
-- on a miss, which a peeked pair still hits whenever the stored value is not
-- reflexive under the value policy (a @NaN@, for instance).  Publishing that
-- receiver would report a successful deletion that removed nothing and leave
-- 'orderedMultimapCursorPeekNext' returning the same pair forever, so the pair
-- count validates the correspondence before a version is published.
orderedMultimapDeletedPair :: k -> v -> PersistentOrderedMultimap k v -> Maybe (PersistentOrderedMultimap k v)
orderedMultimapDeletedPair key item value
  | OrderedMultimap.size next == OrderedMultimap.size value = Nothing
  | otherwise = Just next
  where
    next = OrderedMultimap.delete key item value

-- | Pair rank of the gap immediately after the last pair of an equivalent key
-- group, or the end gap when no such group is present.  Key groups are
-- contiguous in the flattened enumeration, so this walks the leading pairs once
-- and consults only the key policy.
orderedMultimapGroupEnd :: k -> PersistentOrderedMultimap k v -> Int
orderedMultimapGroupEnd key value = beforeGroup 0 (OrderedMultimap.toList value)
  where
    sameKey = equalKeys (OrderedMultimap.keyPolicy value)
    beforeGroup index [] = index
    beforeGroup index ((storedKey, _) : rest)
      | sameKey storedKey key = withinGroup (checkedSuccessor index) rest
      | otherwise = beforeGroup (checkedSuccessor index) rest
    withinGroup index [] = index
    withinGroup index ((storedKey, _) : rest)
      | sameKey storedKey key = withinGroup (checkedSuccessor index) rest
      | otherwise = index

orderedMultimapIndexOf :: k -> v -> PersistentOrderedMultimap k v -> Int
orderedMultimapIndexOf key item value = search 0 (OrderedMultimap.toList value)
  where
    sameKey = equalKeys (OrderedMultimap.keyPolicy value)
    sameValue = equalKeys (OrderedMultimap.valuePolicy value)
    search _ [] = -1
    search index ((storedKey, storedValue) : rest)
      | sameKey storedKey key && sameValue storedValue item = index
      | otherwise = search (checkedSuccessor index) rest

listAt :: Int -> [a] -> Maybe a
listAt index values
  | index < 0 = Nothing
  | otherwise = go index values
  where
    go _ [] = Nothing
    go 0 (value : _) = Just value
    go remaining (_ : rest) = go (remaining - 1) rest
