{-# LANGUAGE BangPatterns #-}

module Durable7.Hamt.Patricia
  ( PatriciaKey
  , PatriciaMap
  , IntMap32
  , IntMap64
  , empty
  , singleton
  , fromList
  , size
  , null
  , validStructure
  , lookup
  , member
  , insert
  , delete
  , union
  , unionWith
  , unionWithKey
  , intersection
  , intersectionWith
  , intersectionWithKey
  , difference
  , toAscList
  , PatriciaCursor
  , PatriciaCursorSearch
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
  , PatriciaSet
  , IntSet32
  , IntSet64
  , emptySet
  , setFromList
  , setSize
  , setMember
  , setInsert
  , setDelete
  , setUnion
  , setIntersection
  , setDifference
  , setToAscList
  , PatriciaSetCursor
  , setCursor
  , setCursorAt
  , setCursorAtEnd
  , setLowerBoundCursor
  , setUpperBoundCursor
  , setCursorAtItem
  , setCursorCount
  , setCursorPosition
  , setCursorIsAtStart
  , setCursorIsAtEnd
  , setCursorPeekPrevious
  , setCursorPeekNext
  , setCursorMovePrevious
  , setCursorMoveNext
  , setCursorSeek
  , setCursorInsert
  , setCursorDeletePrevious
  , setCursorDeleteNext
  , setCursorSnapshot
  ) where

import Prelude hiding (lookup, null)
import Data.Bits ((.&.), complement, countLeadingZeros, shiftL, xor)
import Data.Int (Int32, Int64)
import Data.Word (Word32, Word64)

class (Eq k, Ord k) => PatriciaKey k where
  encodeKey :: k -> Word64

instance PatriciaKey Int32 where
  encodeKey key = fromIntegral ((fromIntegral key :: Word32) `xor` 0x80000000)

instance PatriciaKey Int64 where
  encodeKey key = (fromIntegral key :: Word64) `xor` 0x8000000000000000

data Node k v
  = Leaf !Word64 k v
  | Branch !Int !Word64 !Word64 !(Node k v) !(Node k v)

data PatriciaMap k v = PatriciaMap !Int !(Maybe (Node k v))
type IntMap32 v = PatriciaMap Int32 v
type IntMap64 v = PatriciaMap Int64 v

empty :: PatriciaMap k v
empty = PatriciaMap 0 Nothing

singleton :: PatriciaKey k => k -> v -> PatriciaMap k v
singleton key value = PatriciaMap 1 (Just (Leaf (encodeKey key) key value))

fromList :: PatriciaKey k => [(k, v)] -> PatriciaMap k v
fromList = foldl' (\m (key, value) -> insert key value m) empty

size :: PatriciaMap k v -> Int
size (PatriciaMap count _) = count

null :: PatriciaMap k v -> Bool
null = (== 0) . size

-- | Checks the map cardinality against every cached subtree cardinality.
validStructure :: PatriciaMap k v -> Bool
validStructure (PatriciaMap expected root) = case root of
  Nothing -> expected == 0
  Just node -> validNode node == Just expected

validNode :: Node k v -> Maybe Int
validNode (Leaf _ _ _) = Just 1
validNode (Branch cached _ _ left right) = do
  leftCount <- validNode left
  rightCount <- validNode right
  let actual = leftCount + rightCount
  if cached == actual then Just actual else Nothing

lookup :: PatriciaKey k => k -> PatriciaMap k v -> Maybe v
lookup key (PatriciaMap _ root) = root >>= lookupNode (encodeKey key)

member :: PatriciaKey k => k -> PatriciaMap k v -> Bool
member key = maybe False (const True) . lookup key

lookupNode :: Word64 -> Node k v -> Maybe v
lookupNode path (Leaf leafPath _ value)
  | path == leafPath = Just value
  | otherwise = Nothing
lookupNode path (Branch _ prefix mask left right)
  | prefixOf path mask /= prefix = Nothing
  | path .&. mask == 0 = lookupNode path left
  | otherwise = lookupNode path right

-- | Inserts or replaces a value. Because the unconstrained map does not require
-- @Eq v@, replacing a key rebuilds its leaf and search path even when the new
-- value happens to be equal to the old value.
insert :: PatriciaKey k => k -> v -> PatriciaMap k v -> PatriciaMap k v
insert key value (PatriciaMap count root) =
  let path = encodeKey key
      (root', added) = case root of
        Nothing -> (Leaf path key value, True)
        Just node -> insertNode path key value node
   in PatriciaMap (count + if added then 1 else 0) (Just root')

insertNode :: Word64 -> k -> v -> Node k v -> (Node k v, Bool)
insertNode path key value leaf@(Leaf oldPath oldKey _)
  | path == oldPath = (Leaf path oldKey value, False)
  | otherwise = (join oldPath leaf path (Leaf path key value), True)
insertNode path key value branch@(Branch _ prefix mask left right)
  | prefixOf path mask /= prefix = (join prefix branch path (Leaf path key value), True)
  | path .&. mask == 0 =
      let (child, added) = insertNode path key value left
       in (makeBranch prefix mask child right, added)
  | otherwise =
      let (child, added) = insertNode path key value right
       in (makeBranch prefix mask left child, added)

delete :: PatriciaKey k => k -> PatriciaMap k v -> PatriciaMap k v
delete key original@(PatriciaMap count root) = case root >>= deleteNode (encodeKey key) of
  Nothing -> case root of
    Just (Leaf path _ _) | path == encodeKey key -> PatriciaMap 0 Nothing
    _ -> original
  Just (node, changed) -> if changed then PatriciaMap (count - 1) node else original

deleteNode :: Word64 -> Node k v -> Maybe (Maybe (Node k v), Bool)
deleteNode path leaf@(Leaf leafPath _ _)
  | path == leafPath = Just (Nothing, True)
  | otherwise = Just (Just leaf, False)
deleteNode path branch@(Branch _ prefix mask left right)
  | prefixOf path mask /= prefix = Just (Just branch, False)
  | path .&. mask == 0 = do
      (child, changed) <- deleteNode path left
      pure (if changed then Just (maybe right (\node -> makeBranch prefix mask node right) child) else Just branch, changed)
  | otherwise = do
      (child, changed) <- deleteNode path right
      pure (if changed then Just (maybe left (makeBranch prefix mask left) child) else Just branch, changed)

union :: PatriciaKey k => PatriciaMap k v -> PatriciaMap k v -> PatriciaMap k v
union = unionWithKey (\_ _ right -> right)

-- | Unions two maps, combining a left and right value at every shared key.
unionWith :: PatriciaKey k => (v -> v -> v) -> PatriciaMap k v -> PatriciaMap k v -> PatriciaMap k v
unionWith combine = unionWithKey (\_ left right -> combine left right)

-- | Unions two maps, passing the key, left value, and right value to the combining function.
unionWithKey :: PatriciaKey k => (k -> v -> v -> v) -> PatriciaMap k v -> PatriciaMap k v -> PatriciaMap k v
unionWithKey _ left@(PatriciaMap _ _) (PatriciaMap 0 _) = left
unionWithKey _ (PatriciaMap 0 _) right@(PatriciaMap _ _) = right
unionWithKey combine (PatriciaMap _ left) (PatriciaMap _ right) =
  fromNode (unionWithKeyNodes combine left right)

intersection :: PatriciaKey k => PatriciaMap k v -> PatriciaMap k w -> PatriciaMap k v
intersection left@(PatriciaMap 0 _) _ = left
intersection _ (PatriciaMap 0 _) = empty
intersection (PatriciaMap _ left) (PatriciaMap _ right) = fromNode (intersectNodes left right)

-- | Intersects two maps, combining a left and right value at every shared key.
intersectionWith :: PatriciaKey k => (a -> b -> c) -> PatriciaMap k a -> PatriciaMap k b -> PatriciaMap k c
intersectionWith combine = intersectionWithKey (\_ left right -> combine left right)

-- | Intersects two maps, passing the key, left value, and right value to the combining function.
intersectionWithKey :: PatriciaKey k => (k -> a -> b -> c) -> PatriciaMap k a -> PatriciaMap k b -> PatriciaMap k c
intersectionWithKey _ (PatriciaMap 0 _) _ = empty
intersectionWithKey _ _ (PatriciaMap 0 _) = empty
intersectionWithKey combine (PatriciaMap _ left) (PatriciaMap _ right) =
  fromNode (intersectWithKeyNodes combine left right)

difference :: PatriciaKey k => PatriciaMap k v -> PatriciaMap k w -> PatriciaMap k v
difference left@(PatriciaMap 0 _) _ = left
difference left@(PatriciaMap _ _) (PatriciaMap 0 _) = left
difference (PatriciaMap _ left) (PatriciaMap _ right) = fromNode (exceptNodes left right)

fromNode :: Maybe (Node k v) -> PatriciaMap k v
fromNode root = PatriciaMap (maybe 0 nodeSize root) root

unionWithKeyNodes :: (k -> v -> v -> v) -> Maybe (Node k v) -> Maybe (Node k v) -> Maybe (Node k v)
unionWithKeyNodes _ Nothing right = right
unionWithKeyNodes _ left Nothing = left
unionWithKeyNodes combine (Just (Leaf path key value)) (Just right) =
  Just (putCombined combine True path key value right)
unionWithKeyNodes combine (Just left) (Just (Leaf path key value)) =
  Just (putCombined combine False path key value left)
unionWithKeyNodes combine (Just left@(Branch _ lp lm ll lr)) (Just right@(Branch _ rp rm rl rr))
  | lm == rm && lp == rp = Just (makeBranch lp lm
      (required (unionWithKeyNodes combine (Just ll) (Just rl)))
      (required (unionWithKeyNodes combine (Just lr) (Just rr))))
  | lm > rm && prefixOf rp lm == lp = Just $ if rp .&. lm == 0
      then makeBranch lp lm (required (unionWithKeyNodes combine (Just ll) (Just right))) lr
      else makeBranch lp lm ll (required (unionWithKeyNodes combine (Just lr) (Just right)))
  | rm > lm && prefixOf lp rm == rp = Just $ if lp .&. rm == 0
      then makeBranch rp rm (required (unionWithKeyNodes combine (Just left) (Just rl))) rr
      else makeBranch rp rm rl (required (unionWithKeyNodes combine (Just left) (Just rr)))
  | otherwise = Just (join lp left rp right)

putCombined :: (k -> v -> v -> v) -> Bool -> Word64 -> k -> v -> Node k v -> Node k v
putCombined combine incomingIsLeft path key value leaf@(Leaf oldPath oldKey oldValue)
  | path /= oldPath = join oldPath leaf path (Leaf path key value)
  | incomingIsLeft = Leaf path key (combine key value oldValue)
  | otherwise = Leaf path oldKey (combine oldKey oldValue value)
putCombined combine incomingIsLeft path key value branch@(Branch _ prefix mask left right)
  | prefixOf path mask /= prefix = join prefix branch path (Leaf path key value)
  | path .&. mask == 0 = makeBranch prefix mask (putCombined combine incomingIsLeft path key value left) right
  | otherwise = makeBranch prefix mask left (putCombined combine incomingIsLeft path key value right)

intersectNodes :: Maybe (Node k v) -> Maybe (Node k w) -> Maybe (Node k v)
intersectNodes Nothing _ = Nothing
intersectNodes _ Nothing = Nothing
intersectNodes (Just left@(Leaf path _ _)) (Just right)
  | containsPath path right = Just left
  | otherwise = Nothing
intersectNodes (Just left) (Just (Leaf path _ _)) = findPath path left
intersectNodes (Just left@(Branch _ lp lm ll lr)) (Just right@(Branch _ rp rm rl rr))
  | lm == rm && lp == rp = collapse lp lm (intersectNodes (Just ll) (Just rl)) (intersectNodes (Just lr) (Just rr))
  | lm > rm && prefixOf rp lm == lp = if rp .&. lm == 0 then intersectNodes (Just ll) (Just right) else intersectNodes (Just lr) (Just right)
  | rm > lm && prefixOf lp rm == rp = if lp .&. rm == 0 then intersectNodes (Just left) (Just rl) else intersectNodes (Just left) (Just rr)
  | otherwise = Nothing

intersectWithKeyNodes :: (k -> a -> b -> c) -> Maybe (Node k a) -> Maybe (Node k b) -> Maybe (Node k c)
intersectWithKeyNodes _ Nothing _ = Nothing
intersectWithKeyNodes _ _ Nothing = Nothing
intersectWithKeyNodes combine (Just (Leaf path key leftValue)) (Just right) = case findPath path right of
  Just (Leaf _ _ rightValue) -> Just (Leaf path key (combine key leftValue rightValue))
  _ -> Nothing
intersectWithKeyNodes combine (Just left) (Just (Leaf path _ rightValue)) = case findPath path left of
  Just (Leaf leftPath key leftValue) -> Just (Leaf leftPath key (combine key leftValue rightValue))
  _ -> Nothing
intersectWithKeyNodes combine (Just left@(Branch _ lp lm ll lr)) (Just right@(Branch _ rp rm rl rr))
  | lm == rm && lp == rp = collapse lp lm
      (intersectWithKeyNodes combine (Just ll) (Just rl))
      (intersectWithKeyNodes combine (Just lr) (Just rr))
  | lm > rm && prefixOf rp lm == lp = if rp .&. lm == 0
      then intersectWithKeyNodes combine (Just ll) (Just right)
      else intersectWithKeyNodes combine (Just lr) (Just right)
  | rm > lm && prefixOf lp rm == rp = if lp .&. rm == 0
      then intersectWithKeyNodes combine (Just left) (Just rl)
      else intersectWithKeyNodes combine (Just left) (Just rr)
  | otherwise = Nothing

exceptNodes :: Maybe (Node k v) -> Maybe (Node k w) -> Maybe (Node k v)
exceptNodes Nothing _ = Nothing
exceptNodes left Nothing = left
exceptNodes (Just left@(Leaf path _ _)) (Just right) = if containsPath path right then Nothing else Just left
exceptNodes (Just left) (Just (Leaf path _ _)) = removePath path left
exceptNodes (Just left@(Branch _ lp lm ll lr)) (Just right@(Branch _ rp rm rl rr))
  | lm == rm && lp == rp = collapse lp lm (exceptNodes (Just ll) (Just rl)) (exceptNodes (Just lr) (Just rr))
  | lm > rm && prefixOf rp lm == lp = if rp .&. lm == 0
      then collapse lp lm (exceptNodes (Just ll) (Just right)) (Just lr)
      else collapse lp lm (Just ll) (exceptNodes (Just lr) (Just right))
  | rm > lm && prefixOf lp rm == rp = if lp .&. rm == 0 then exceptNodes (Just left) (Just rl) else exceptNodes (Just left) (Just rr)
  | otherwise = Just left

containsPath :: Word64 -> Node k v -> Bool
containsPath path = maybe False (const True) . findPath path

findPath :: Word64 -> Node k v -> Maybe (Node k v)
findPath path leaf@(Leaf found _ _) = if path == found then Just leaf else Nothing
findPath path (Branch _ prefix mask left right)
  | prefixOf path mask /= prefix = Nothing
  | path .&. mask == 0 = findPath path left
  | otherwise = findPath path right

removePath :: Word64 -> Node k v -> Maybe (Node k v)
removePath path leaf@(Leaf found _ _) = if path == found then Nothing else Just leaf
removePath path branch@(Branch _ prefix mask left right)
  | prefixOf path mask /= prefix = Just branch
  | path .&. mask == 0 = collapse prefix mask (removePath path left) (Just right)
  | otherwise = collapse prefix mask (Just left) (removePath path right)

collapse :: Word64 -> Word64 -> Maybe (Node k v) -> Maybe (Node k v) -> Maybe (Node k v)
collapse _ _ Nothing right = right
collapse _ _ left Nothing = left
collapse prefix mask (Just left) (Just right) = Just (makeBranch prefix mask left right)

join :: Word64 -> Node k v -> Word64 -> Node k v -> Node k v
join leftPath left rightPath right =
  let differenceBits = leftPath `xor` rightPath
      mask = 1 `shiftL` (63 - countLeadingZeros differenceBits)
      prefix = prefixOf leftPath mask
   in if leftPath .&. mask == 0 then makeBranch prefix mask left right else makeBranch prefix mask right left

prefixOf :: Word64 -> Word64 -> Word64
prefixOf path mask = path .&. complement ((mask `shiftL` 1) - 1)

nodeSize :: Node k v -> Int
nodeSize (Leaf _ _ _) = 1
nodeSize (Branch count _ _ _ _) = count

makeBranch :: Word64 -> Word64 -> Node k v -> Node k v -> Node k v
makeBranch prefix mask left right = Branch (nodeSize left + nodeSize right) prefix mask left right

required :: Maybe a -> a
required (Just value) = value
required Nothing = error "Patricia invariant: union of nonempty nodes became empty"

toAscList :: PatriciaMap k v -> [(k, v)]
toAscList (PatriciaMap _ root) = maybe [] visit root
  where
    visit (Leaf _ key value) = [(key, value)]
    visit (Branch _ _ _ left right) = visit left ++ visit right

-- | An immutable root-plus-rank gap cursor over one Patricia map snapshot.
-- The constructor is intentionally hidden. A whole neighbor entry is wrapped
-- in 'Maybe', so a stored 'Nothing' payload remains distinguishable from a
-- cursor boundary.
data PatriciaCursor k v = PatriciaCursor !(PatriciaMap k v) !Int

-- | Exact-search result. A miss retains a usable lower-bound cursor.
data PatriciaCursorSearch k v = PatriciaCursorSearch !Bool !(PatriciaCursor k v)

cursor :: PatriciaMap k v -> PatriciaCursor k v
cursor values = PatriciaCursor values 0

cursorAt :: Int -> PatriciaMap k v -> Maybe (PatriciaCursor k v)
cursorAt position values
  | position < 0 || position > size values = Nothing
  | otherwise = Just (PatriciaCursor values position)

cursorAtEnd :: PatriciaMap k v -> PatriciaCursor k v
cursorAtEnd values = PatriciaCursor values (size values)

lowerBoundCursor :: PatriciaKey k => k -> PatriciaMap k v -> PatriciaCursor k v
lowerBoundCursor key values = PatriciaCursor values (fst (lowerBoundRank key values))

upperBoundCursor :: PatriciaKey k => k -> PatriciaMap k v -> PatriciaCursor k v
upperBoundCursor key values =
  let (position, found) = lowerBoundRank key values
   in PatriciaCursor values (position + if found then 1 else 0)

cursorAtKey :: PatriciaKey k => k -> PatriciaMap k v -> PatriciaCursorSearch k v
cursorAtKey key values =
  let (position, found) = lowerBoundRank key values
   in PatriciaCursorSearch found (PatriciaCursor values position)

cursorSearchFound :: PatriciaCursorSearch k v -> Bool
cursorSearchFound (PatriciaCursorSearch found _) = found

cursorSearchCursor :: PatriciaCursorSearch k v -> PatriciaCursor k v
cursorSearchCursor (PatriciaCursorSearch _ cursorValue) = cursorValue

cursorCount :: PatriciaCursor k v -> Int
cursorCount (PatriciaCursor values _) = size values

cursorPosition :: PatriciaCursor k v -> Int
cursorPosition (PatriciaCursor _ position) = position

cursorIsAtStart :: PatriciaCursor k v -> Bool
cursorIsAtStart cursorValue = cursorPosition cursorValue == 0

cursorIsAtEnd :: PatriciaCursor k v -> Bool
cursorIsAtEnd cursorValue = cursorPosition cursorValue == cursorCount cursorValue

cursorPeekPrevious :: PatriciaCursor k v -> Maybe (k, v)
cursorPeekPrevious cursorValue@(PatriciaCursor values position)
  | cursorIsAtStart cursorValue = Nothing
  | otherwise = entryAt (position - 1) values

cursorPeekNext :: PatriciaCursor k v -> Maybe (k, v)
cursorPeekNext (PatriciaCursor values position) = entryAt position values

cursorMovePrevious :: PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorMovePrevious cursorValue@(PatriciaCursor values position)
  | cursorIsAtStart cursorValue = Nothing
  | otherwise = Just (PatriciaCursor values (position - 1))

cursorMoveNext :: PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorMoveNext cursorValue@(PatriciaCursor values position)
  | cursorIsAtEnd cursorValue = Nothing
  | otherwise = Just (PatriciaCursor values (position + 1))

cursorSeek :: Int -> PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorSeek position cursorValue@(PatriciaCursor values _)
  | position < 0 || position > cursorCount cursorValue = Nothing
  | position == cursorPosition cursorValue = Just cursorValue
  | otherwise = Just (PatriciaCursor values position)

-- | Strictly inserts at a missing lower-bound gap. A duplicate key or a key
-- belonging at another gap returns 'Nothing'.
cursorInsert :: PatriciaKey k => k -> v -> PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorInsert key value (PatriciaCursor values position)
  | found || expected /= position = Nothing
  | otherwise = Just (PatriciaCursor (insert key value values) (position + 1))
  where
    (expected, found) = lowerBoundRank key values

-- | Updates the exact next key or inserts at a missing lower-bound gap. An
-- update keeps the gap fixed; an insertion returns the gap after the entry.
cursorPut :: PatriciaKey k => k -> v -> PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorPut key value (PatriciaCursor values position)
  | expected /= position = Nothing
  | otherwise = Just (PatriciaCursor (insert key value values) (position + if found then 0 else 1))
  where
    (expected, found) = lowerBoundRank key values

-- | Replaces the next value while retaining its stored key. As with ordinary
-- 'insert', this deliberately requires no @Eq v@ and rebuilds the present-key
-- path even when the replacement happens to be equal.
cursorSetNextValue :: PatriciaKey k => v -> PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorSetNextValue value (PatriciaCursor values position) = do
  (key, _) <- entryAt position values
  pure (PatriciaCursor (insert key value values) position)

cursorDeletePrevious :: PatriciaKey k => PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorDeletePrevious (PatriciaCursor values position) = do
  (key, _) <- entryAt (position - 1) values
  pure (PatriciaCursor (delete key values) (position - 1))

cursorDeleteNext :: PatriciaKey k => PatriciaCursor k v -> Maybe (PatriciaCursor k v)
cursorDeleteNext (PatriciaCursor values position) = do
  (key, _) <- entryAt position values
  pure (PatriciaCursor (delete key values) position)

cursorSnapshot :: PatriciaCursor k v -> PatriciaMap k v
cursorSnapshot (PatriciaCursor values _) = values

entryAt :: Int -> PatriciaMap k v -> Maybe (k, v)
entryAt index (PatriciaMap count root)
  | index < 0 || index >= count = Nothing
  | otherwise = root >>= nodeEntryAt index

nodeEntryAt :: Int -> Node k v -> Maybe (k, v)
nodeEntryAt index (Leaf _ key value)
  | index == 0 = Just (key, value)
  | otherwise = Nothing
nodeEntryAt index (Branch _ _ _ left right)
  | index < nodeSize left = nodeEntryAt index left
  | otherwise = nodeEntryAt (index - nodeSize left) right

lowerBoundRank :: PatriciaKey k => k -> PatriciaMap k v -> (Int, Bool)
lowerBoundRank key (PatriciaMap _ root) = maybe (0, False) (go 0) root
  where
    path = encodeKey key
    go rank (Leaf leafPath _ _)
      | leafPath < path = (rank + 1, False)
      | otherwise = (rank, leafPath == path)
    go rank (Branch count prefix mask left right)
      | prefixOf path mask /= prefix = if path < prefix then (rank, False) else (rank + count, False)
      | path .&. mask == 0 = go rank left
      | otherwise = go (rank + nodeSize left) right

newtype PatriciaSet k = PatriciaSet (PatriciaMap k ())
type IntSet32 = PatriciaSet Int32
type IntSet64 = PatriciaSet Int64

emptySet :: PatriciaSet k
emptySet = PatriciaSet empty
setFromList :: PatriciaKey k => [k] -> PatriciaSet k
setFromList = foldl' (flip setInsert) emptySet
setSize :: PatriciaSet k -> Int
setSize (PatriciaSet values) = size values
setMember :: PatriciaKey k => k -> PatriciaSet k -> Bool
setMember key (PatriciaSet values) = member key values
setInsert :: PatriciaKey k => k -> PatriciaSet k -> PatriciaSet k
setInsert key (PatriciaSet values) = PatriciaSet (insert key () values)
setDelete :: PatriciaKey k => k -> PatriciaSet k -> PatriciaSet k
setDelete key (PatriciaSet values) = PatriciaSet (delete key values)
setUnion :: PatriciaKey k => PatriciaSet k -> PatriciaSet k -> PatriciaSet k
setUnion (PatriciaSet left) (PatriciaSet right) = PatriciaSet (union left right)
setIntersection :: PatriciaKey k => PatriciaSet k -> PatriciaSet k -> PatriciaSet k
setIntersection (PatriciaSet left) (PatriciaSet right) = PatriciaSet (intersection left right)
setDifference :: PatriciaKey k => PatriciaSet k -> PatriciaSet k -> PatriciaSet k
setDifference (PatriciaSet left) (PatriciaSet right) = PatriciaSet (difference left right)
setToAscList :: PatriciaSet k -> [k]
setToAscList (PatriciaSet values) = map fst (toAscList values)

-- | Immutable root-plus-rank gap cursor over a Patricia set.
data PatriciaSetCursor k = PatriciaSetCursor !(PatriciaSet k) !Int

setCursor :: PatriciaSet k -> PatriciaSetCursor k
setCursor values = PatriciaSetCursor values 0

setCursorAt :: Int -> PatriciaSet k -> Maybe (PatriciaSetCursor k)
setCursorAt position values
  | position < 0 || position > setSize values = Nothing
  | otherwise = Just (PatriciaSetCursor values position)

setCursorAtEnd :: PatriciaSet k -> PatriciaSetCursor k
setCursorAtEnd values = PatriciaSetCursor values (setSize values)

setLowerBoundCursor :: PatriciaKey k => k -> PatriciaSet k -> PatriciaSetCursor k
setLowerBoundCursor key values@(PatriciaSet mapValue) =
  PatriciaSetCursor values (fst (lowerBoundRank key mapValue))

setUpperBoundCursor :: PatriciaKey k => k -> PatriciaSet k -> PatriciaSetCursor k
setUpperBoundCursor key values@(PatriciaSet mapValue) =
  let (position, found) = lowerBoundRank key mapValue
   in PatriciaSetCursor values (position + if found then 1 else 0)

-- | Returns an exact-key discriminator and a usable lower-bound cursor.
setCursorAtItem :: PatriciaKey k => k -> PatriciaSet k -> (Bool, PatriciaSetCursor k)
setCursorAtItem key values@(PatriciaSet mapValue) =
  let (position, found) = lowerBoundRank key mapValue
   in (found, PatriciaSetCursor values position)

setCursorCount :: PatriciaSetCursor k -> Int
setCursorCount (PatriciaSetCursor values _) = setSize values

setCursorPosition :: PatriciaSetCursor k -> Int
setCursorPosition (PatriciaSetCursor _ position) = position

setCursorIsAtStart :: PatriciaSetCursor k -> Bool
setCursorIsAtStart cursorValue = setCursorPosition cursorValue == 0

setCursorIsAtEnd :: PatriciaSetCursor k -> Bool
setCursorIsAtEnd cursorValue = setCursorPosition cursorValue == setCursorCount cursorValue

setCursorPeekPrevious :: PatriciaSetCursor k -> Maybe k
setCursorPeekPrevious cursorValue@(PatriciaSetCursor values position)
  | setCursorIsAtStart cursorValue = Nothing
  | otherwise = setEntryAt (position - 1) values

setCursorPeekNext :: PatriciaSetCursor k -> Maybe k
setCursorPeekNext (PatriciaSetCursor values position) = setEntryAt position values

setCursorMovePrevious :: PatriciaSetCursor k -> Maybe (PatriciaSetCursor k)
setCursorMovePrevious cursorValue@(PatriciaSetCursor values position)
  | setCursorIsAtStart cursorValue = Nothing
  | otherwise = Just (PatriciaSetCursor values (position - 1))

setCursorMoveNext :: PatriciaSetCursor k -> Maybe (PatriciaSetCursor k)
setCursorMoveNext cursorValue@(PatriciaSetCursor values position)
  | setCursorIsAtEnd cursorValue = Nothing
  | otherwise = Just (PatriciaSetCursor values (position + 1))

setCursorSeek :: Int -> PatriciaSetCursor k -> Maybe (PatriciaSetCursor k)
setCursorSeek position cursorValue@(PatriciaSetCursor values _)
  | position < 0 || position > setCursorCount cursorValue = Nothing
  | position == setCursorPosition cursorValue = Just cursorValue
  | otherwise = Just (PatriciaSetCursor values position)

-- | Adds at the current lower-bound gap. An exact duplicate preserves the
-- same logical cursor; a value belonging at another gap returns 'Nothing'.
setCursorInsert :: PatriciaKey k => k -> PatriciaSetCursor k -> Maybe (PatriciaSetCursor k)
setCursorInsert key cursorValue@(PatriciaSetCursor values@(PatriciaSet mapValue) position)
  | expected /= position = Nothing
  | found = Just cursorValue
  | otherwise = Just (PatriciaSetCursor (setInsert key values) (position + 1))
  where
    (expected, found) = lowerBoundRank key mapValue

setCursorDeletePrevious :: PatriciaKey k => PatriciaSetCursor k -> Maybe (PatriciaSetCursor k)
setCursorDeletePrevious (PatriciaSetCursor values position) = do
  key <- setEntryAt (position - 1) values
  pure (PatriciaSetCursor (setDelete key values) (position - 1))

setCursorDeleteNext :: PatriciaKey k => PatriciaSetCursor k -> Maybe (PatriciaSetCursor k)
setCursorDeleteNext (PatriciaSetCursor values position) = do
  key <- setEntryAt position values
  pure (PatriciaSetCursor (setDelete key values) position)

setCursorSnapshot :: PatriciaSetCursor k -> PatriciaSet k
setCursorSnapshot (PatriciaSetCursor values _) = values

setEntryAt :: Int -> PatriciaSet k -> Maybe k
setEntryAt index (PatriciaSet values) = fst <$> entryAt index values
