{-# LANGUAGE BangPatterns #-}

module Data.Structures.Hamt.Patricia
  ( PatriciaKey
  , PatriciaMap
  , IntMap32
  , IntMap64
  , empty
  , singleton
  , fromList
  , size
  , null
  , lookup
  , member
  , insert
  , delete
  , union
  , intersection
  , difference
  , toAscList
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
  | Branch !Word64 !Word64 !(Node k v) !(Node k v)

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

lookup :: PatriciaKey k => k -> PatriciaMap k v -> Maybe v
lookup key (PatriciaMap _ root) = root >>= lookupNode (encodeKey key)

member :: PatriciaKey k => k -> PatriciaMap k v -> Bool
member key = maybe False (const True) . lookup key

lookupNode :: Word64 -> Node k v -> Maybe v
lookupNode path (Leaf leafPath _ value)
  | path == leafPath = Just value
  | otherwise = Nothing
lookupNode path (Branch prefix mask left right)
  | prefixOf path mask /= prefix = Nothing
  | path .&. mask == 0 = lookupNode path left
  | otherwise = lookupNode path right

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
insertNode path key value branch@(Branch prefix mask left right)
  | prefixOf path mask /= prefix = (join prefix branch path (Leaf path key value), True)
  | path .&. mask == 0 =
      let (child, added) = insertNode path key value left
       in (Branch prefix mask child right, added)
  | otherwise =
      let (child, added) = insertNode path key value right
       in (Branch prefix mask left child, added)

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
deleteNode path branch@(Branch prefix mask left right)
  | prefixOf path mask /= prefix = Just (Just branch, False)
  | path .&. mask == 0 = do
      (child, changed) <- deleteNode path left
      pure (if changed then Just (maybe right (\node -> Branch prefix mask node right) child) else Just branch, changed)
  | otherwise = do
      (child, changed) <- deleteNode path right
      pure (if changed then Just (maybe left (Branch prefix mask left) child) else Just branch, changed)

union :: PatriciaKey k => PatriciaMap k v -> PatriciaMap k v -> PatriciaMap k v
union (PatriciaMap _ left) (PatriciaMap _ right) = fromNode (unionNodes left right)

intersection :: PatriciaKey k => PatriciaMap k v -> PatriciaMap k w -> PatriciaMap k v
intersection (PatriciaMap _ left) (PatriciaMap _ right) = fromNode (intersectNodes left right)

difference :: PatriciaKey k => PatriciaMap k v -> PatriciaMap k w -> PatriciaMap k v
difference (PatriciaMap _ left) (PatriciaMap _ right) = fromNode (exceptNodes left right)

fromNode :: Maybe (Node k v) -> PatriciaMap k v
fromNode root = PatriciaMap (maybe 0 countNode root) root

unionNodes :: Maybe (Node k v) -> Maybe (Node k v) -> Maybe (Node k v)
unionNodes Nothing right = right
unionNodes left Nothing = left
unionNodes (Just (Leaf path key value)) (Just right) = Just (putExisting True path key value right)
unionNodes (Just left) (Just (Leaf path key value)) = Just (putExisting False path key value left)
unionNodes (Just left@(Branch lp lm ll lr)) (Just right@(Branch rp rm rl rr))
  | lm == rm && lp == rp = Just (Branch lp lm (required (unionNodes (Just ll) (Just rl))) (required (unionNodes (Just lr) (Just rr))))
  | lm > rm && prefixOf rp lm == lp = Just $ if rp .&. lm == 0
      then Branch lp lm (required (unionNodes (Just ll) (Just right))) lr
      else Branch lp lm ll (required (unionNodes (Just lr) (Just right)))
  | rm > lm && prefixOf lp rm == rp = Just $ if lp .&. rm == 0
      then Branch rp rm (required (unionNodes (Just left) (Just rl))) rr
      else Branch rp rm rl (required (unionNodes (Just left) (Just rr)))
  | otherwise = Just (join lp left rp right)

putExisting :: Bool -> Word64 -> k -> v -> Node k v -> Node k v
putExisting prefer path key value leaf@(Leaf oldPath oldKey _)
  | path == oldPath = if prefer then leaf else Leaf path oldKey value
  | otherwise = join oldPath leaf path (Leaf path key value)
putExisting prefer path key value branch@(Branch prefix mask left right)
  | prefixOf path mask /= prefix = join prefix branch path (Leaf path key value)
  | path .&. mask == 0 = Branch prefix mask (putExisting prefer path key value left) right
  | otherwise = Branch prefix mask left (putExisting prefer path key value right)

intersectNodes :: Maybe (Node k v) -> Maybe (Node k w) -> Maybe (Node k v)
intersectNodes Nothing _ = Nothing
intersectNodes _ Nothing = Nothing
intersectNodes (Just left@(Leaf path _ _)) (Just right)
  | containsPath path right = Just left
  | otherwise = Nothing
intersectNodes (Just left) (Just (Leaf path _ _)) = findPath path left
intersectNodes (Just left@(Branch lp lm ll lr)) (Just right@(Branch rp rm rl rr))
  | lm == rm && lp == rp = collapse lp lm (intersectNodes (Just ll) (Just rl)) (intersectNodes (Just lr) (Just rr))
  | lm > rm && prefixOf rp lm == lp = if rp .&. lm == 0 then intersectNodes (Just ll) (Just right) else intersectNodes (Just lr) (Just right)
  | rm > lm && prefixOf lp rm == rp = if lp .&. rm == 0 then intersectNodes (Just left) (Just rl) else intersectNodes (Just left) (Just rr)
  | otherwise = Nothing

exceptNodes :: Maybe (Node k v) -> Maybe (Node k w) -> Maybe (Node k v)
exceptNodes Nothing _ = Nothing
exceptNodes left Nothing = left
exceptNodes (Just left@(Leaf path _ _)) (Just right) = if containsPath path right then Nothing else Just left
exceptNodes (Just left) (Just (Leaf path _ _)) = removePath path left
exceptNodes (Just left@(Branch lp lm ll lr)) (Just right@(Branch rp rm rl rr))
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
findPath path (Branch prefix mask left right)
  | prefixOf path mask /= prefix = Nothing
  | path .&. mask == 0 = findPath path left
  | otherwise = findPath path right

removePath :: Word64 -> Node k v -> Maybe (Node k v)
removePath path leaf@(Leaf found _ _) = if path == found then Nothing else Just leaf
removePath path branch@(Branch prefix mask left right)
  | prefixOf path mask /= prefix = Just branch
  | path .&. mask == 0 = collapse prefix mask (removePath path left) (Just right)
  | otherwise = collapse prefix mask (Just left) (removePath path right)

collapse :: Word64 -> Word64 -> Maybe (Node k v) -> Maybe (Node k v) -> Maybe (Node k v)
collapse _ _ Nothing right = right
collapse _ _ left Nothing = left
collapse prefix mask (Just left) (Just right) = Just (Branch prefix mask left right)

join :: Word64 -> Node k v -> Word64 -> Node k v -> Node k v
join leftPath left rightPath right =
  let differenceBits = leftPath `xor` rightPath
      mask = 1 `shiftL` (63 - countLeadingZeros differenceBits)
      prefix = prefixOf leftPath mask
   in if leftPath .&. mask == 0 then Branch prefix mask left right else Branch prefix mask right left

prefixOf :: Word64 -> Word64 -> Word64
prefixOf path mask = path .&. complement ((mask `shiftL` 1) - 1)

countNode :: Node k v -> Int
countNode (Leaf _ _ _) = 1
countNode (Branch _ _ left right) = countNode left + countNode right

required :: Maybe a -> a
required (Just value) = value
required Nothing = error "Patricia invariant: union of nonempty nodes became empty"

toAscList :: PatriciaMap k v -> [(k, v)]
toAscList (PatriciaMap _ root) = maybe [] visit root
  where
    visit (Leaf _ key value) = [(key, value)]
    visit (Branch _ _ left right) = visit left ++ visit right

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
