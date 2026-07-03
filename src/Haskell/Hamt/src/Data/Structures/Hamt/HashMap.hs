{-# LANGUAGE BangPatterns #-}

module Data.Structures.Hamt.HashMap
  ( HashMap
  , HashPolicy(..)
  , defaultPolicy
  , empty
  , emptyWith
  , singleton
  , singletonWith
  , fromList
  , fromListWith
  , size
  , null
  , clear
  , policy
  , member
  , lookup
  , findWithDefault
  , actualKey
  , insert
  , insertNew
  , delete
  , tryRemove
  , adjust
  , mapValues
  , toList
  , keys
  , elems
  , foldrWithKey
  ) where

import Prelude hiding (lookup, null)

import Data.Bits (Bits((.&.), xor), popCount, shiftL, shiftR)
import Data.Word (Word32)

import Data.Structures.Hamt.Hashable (Hashable(..))

data HashPolicy k = HashPolicy
  { hashKey :: k -> Int
  , equalKeys :: k -> k -> Bool
  }

defaultPolicy :: (Eq k, Hashable k) => HashPolicy k
defaultPolicy = HashPolicy hash (==)

data HashMap k v = HashMap !(HashPolicy k) !Int !(Node k v)

data Node k v
  = EmptyNode
  | Leaf !Word32 k v
  | Collision !Word32 [(k, v)]
  | Branch !Word32 [Node k v]

data InsertResult k v
  = Inserted !(Node k v)
  | Replaced !(Node k v)
  | Duplicate

data RemoveResult k v
  = Missing
  | Removed !(Node k v) v

empty :: (Eq k, Hashable k) => HashMap k v
empty = emptyWith defaultPolicy

emptyWith :: HashPolicy k -> HashMap k v
emptyWith hashPolicy = HashMap hashPolicy 0 EmptyNode

singleton :: (Eq k, Hashable k) => k -> v -> HashMap k v
singleton key value = singletonWith defaultPolicy key value

singletonWith :: HashPolicy k -> k -> v -> HashMap k v
singletonWith hashPolicy key value = insert key value (emptyWith hashPolicy)

fromList :: (Eq k, Hashable k) => [(k, v)] -> HashMap k v
fromList = fromListWith defaultPolicy

fromListWith :: HashPolicy k -> [(k, v)] -> HashMap k v
fromListWith hashPolicy = foldl (\m (k, v) -> insert k v m) (emptyWith hashPolicy)

size :: HashMap k v -> Int
size (HashMap _ count _) = count

null :: HashMap k v -> Bool
null mapValue = size mapValue == 0

clear :: HashMap k v -> HashMap k v
clear (HashMap hashPolicy _ _) = HashMap hashPolicy 0 EmptyNode

policy :: HashMap k v -> HashPolicy k
policy (HashMap hashPolicy _ _) = hashPolicy

member :: k -> HashMap k v -> Bool
member key mapValue =
  case lookup key mapValue of
    Just _ -> True
    Nothing -> False

lookup :: k -> HashMap k v -> Maybe v
lookup key (HashMap hashPolicy _ root) = lookupNode hashPolicy (hashFor hashPolicy key) key 0 root

findWithDefault :: v -> k -> HashMap k v -> v
findWithDefault defaultValue key mapValue =
  case lookup key mapValue of
    Just value -> value
    Nothing -> defaultValue

actualKey :: k -> HashMap k v -> Maybe k
actualKey key (HashMap hashPolicy _ root) = actualKeyNode hashPolicy (hashFor hashPolicy key) key 0 root

insert :: k -> v -> HashMap k v -> HashMap k v
insert key value (HashMap hashPolicy count root) =
  case insertNode hashPolicy False (hashFor hashPolicy key) key value 0 root of
    Inserted root' -> HashMap hashPolicy (count + 1) root'
    Replaced root' -> HashMap hashPolicy count root'
    Duplicate -> HashMap hashPolicy count root

insertNew :: k -> v -> HashMap k v -> Maybe (HashMap k v)
insertNew key value (HashMap hashPolicy count root) =
  case insertNode hashPolicy True (hashFor hashPolicy key) key value 0 root of
    Inserted root' -> Just (HashMap hashPolicy (count + 1) root')
    Replaced root' -> Just (HashMap hashPolicy count root')
    Duplicate -> Nothing

delete :: k -> HashMap k v -> HashMap k v
delete key mapValue =
  case tryRemove key mapValue of
    Just (_, rest) -> rest
    Nothing -> mapValue

tryRemove :: k -> HashMap k v -> Maybe (v, HashMap k v)
tryRemove key (HashMap hashPolicy count root) =
  case removeNode hashPolicy (hashFor hashPolicy key) key 0 root of
    Missing -> Nothing
    Removed root' value -> Just (value, HashMap hashPolicy (count - 1) root')

adjust :: (v -> v) -> k -> HashMap k v -> HashMap k v
adjust updater key mapValue =
  case lookup key mapValue of
    Just value -> insert key (updater value) mapValue
    Nothing -> mapValue

mapValues :: (v -> w) -> HashMap k v -> HashMap k w
mapValues mapper (HashMap hashPolicy count root) = HashMap hashPolicy count (mapNode mapper root)

toList :: HashMap k v -> [(k, v)]
toList (HashMap _ _ root) = foldrNode (:) [] root

keys :: HashMap k v -> [k]
keys = fmap fst . toList

elems :: HashMap k v -> [v]
elems = fmap snd . toList

foldrWithKey :: (k -> v -> b -> b) -> b -> HashMap k v -> b
foldrWithKey folder seed (HashMap _ _ root) = foldrNode (\(k, v) acc -> folder k v acc) seed root

lookupNode :: HashPolicy k -> Word32 -> k -> Int -> Node k v -> Maybe v
lookupNode _ _ _ _ EmptyNode = Nothing
lookupNode hashPolicy hashValue key _ (Leaf leafHash leafKey value)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey = Just value
  | otherwise = Nothing
lookupNode hashPolicy hashValue key _ (Collision collisionHash entries)
  | hashValue == collisionHash = lookupCollision hashPolicy key entries
  | otherwise = Nothing
lookupNode hashPolicy hashValue key shift (Branch bitmap children)
  | bitmap .&. bit == 0 = Nothing
  | otherwise = lookupNode hashPolicy hashValue key (shift + bitsPerLevel) (children !! childIndex bitmap bit)
  where
    bit = bitFor hashValue shift

lookupCollision :: HashPolicy k -> k -> [(k, v)] -> Maybe v
lookupCollision _ _ [] = Nothing
lookupCollision hashPolicy key ((candidate, value) : rest)
  | equalKeys hashPolicy key candidate = Just value
  | otherwise = lookupCollision hashPolicy key rest

actualKeyNode :: HashPolicy k -> Word32 -> k -> Int -> Node k v -> Maybe k
actualKeyNode _ _ _ _ EmptyNode = Nothing
actualKeyNode hashPolicy hashValue key _ (Leaf leafHash leafKey _)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey = Just leafKey
  | otherwise = Nothing
actualKeyNode hashPolicy hashValue key _ (Collision collisionHash entries)
  | hashValue == collisionHash = actualKeyCollision hashPolicy key entries
  | otherwise = Nothing
actualKeyNode hashPolicy hashValue key shift (Branch bitmap children)
  | bitmap .&. bit == 0 = Nothing
  | otherwise = actualKeyNode hashPolicy hashValue key (shift + bitsPerLevel) (children !! childIndex bitmap bit)
  where
    bit = bitFor hashValue shift

actualKeyCollision :: HashPolicy k -> k -> [(k, v)] -> Maybe k
actualKeyCollision _ _ [] = Nothing
actualKeyCollision hashPolicy key ((candidate, _) : rest)
  | equalKeys hashPolicy key candidate = Just candidate
  | otherwise = actualKeyCollision hashPolicy key rest

insertNode :: HashPolicy k -> Bool -> Word32 -> k -> v -> Int -> Node k v -> InsertResult k v
insertNode _ _ hashValue key value _ EmptyNode = Inserted (Leaf hashValue key value)
insertNode hashPolicy addOnly hashValue key value shift (Leaf leafHash leafKey leafValue)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey =
      if addOnly then Duplicate else Replaced (Leaf leafHash leafKey value)
  | otherwise = Inserted (mergeTwo shift leafHash (Leaf leafHash leafKey leafValue) hashValue (Leaf hashValue key value))
insertNode hashPolicy addOnly hashValue key value shift (Collision collisionHash entries)
  | hashValue == collisionHash =
      case insertCollision hashPolicy addOnly key value entries of
        InsertedEntries entries' -> Inserted (Collision collisionHash entries')
        ReplacedEntries entries' -> Replaced (Collision collisionHash entries')
        DuplicateEntry -> Duplicate
  | otherwise = Inserted (mergeTwo shift collisionHash (Collision collisionHash entries) hashValue (Leaf hashValue key value))
insertNode hashPolicy addOnly hashValue key value shift (Branch bitmap children)
  | bitmap .&. bit == 0 =
      Inserted (Branch (bitmap `xor` bit) (insertAt index (Leaf hashValue key value) children))
  | otherwise =
      case insertNode hashPolicy addOnly hashValue key value (shift + bitsPerLevel) (children !! index) of
        Inserted child -> Inserted (Branch bitmap (replaceAt index child children))
        Replaced child -> Replaced (Branch bitmap (replaceAt index child children))
        Duplicate -> Duplicate
  where
    bit = bitFor hashValue shift
    index = childIndex bitmap bit

data CollisionInsert k v
  = InsertedEntries [(k, v)]
  | ReplacedEntries [(k, v)]
  | DuplicateEntry

insertCollision :: HashPolicy k -> Bool -> k -> v -> [(k, v)] -> CollisionInsert k v
insertCollision _ _ key value [] = InsertedEntries [(key, value)]
insertCollision hashPolicy addOnly key value ((candidate, oldValue) : rest)
  | equalKeys hashPolicy key candidate =
      if addOnly then DuplicateEntry else ReplacedEntries ((candidate, value) : rest)
  | otherwise =
      case insertCollision hashPolicy addOnly key value rest of
        InsertedEntries entries -> InsertedEntries ((candidate, oldValue) : entries)
        ReplacedEntries entries -> ReplacedEntries ((candidate, oldValue) : entries)
        DuplicateEntry -> DuplicateEntry

removeNode :: HashPolicy k -> Word32 -> k -> Int -> Node k v -> RemoveResult k v
removeNode _ _ _ _ EmptyNode = Missing
removeNode hashPolicy hashValue key _ (Leaf leafHash leafKey value)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey = Removed EmptyNode value
  | otherwise = Missing
removeNode hashPolicy hashValue key _ (Collision collisionHash entries)
  | hashValue /= collisionHash = Missing
  | otherwise =
      case removeCollision hashPolicy key entries of
        Nothing -> Missing
        Just (value, remaining) -> Removed (entriesToNode collisionHash remaining) value
removeNode hashPolicy hashValue key shift branch@(Branch bitmap children)
  | bitmap .&. bit == 0 = keep branch
  | otherwise =
      case removeNode hashPolicy hashValue key (shift + bitsPerLevel) (children !! index) of
        Missing -> keep branch
        Removed EmptyNode value -> Removed (normalizeBranch (bitmap `xor` bit) (removeAt index children)) value
        Removed child value -> Removed (normalizeBranch bitmap (replaceAt index child children)) value
  where
    bit = bitFor hashValue shift
    index = childIndex bitmap bit
    keep _ = Missing

removeCollision :: HashPolicy k -> k -> [(k, v)] -> Maybe (v, [(k, v)])
removeCollision _ _ [] = Nothing
removeCollision hashPolicy key ((candidate, value) : rest)
  | equalKeys hashPolicy key candidate = Just (value, rest)
  | otherwise =
      case removeCollision hashPolicy key rest of
        Nothing -> Nothing
        Just (removed, remaining) -> Just (removed, (candidate, value) : remaining)

entriesToNode :: Word32 -> [(k, v)] -> Node k v
entriesToNode _ [] = EmptyNode
entriesToNode hashValue [(key, value)] = Leaf hashValue key value
entriesToNode hashValue entries = Collision hashValue entries

mergeTwo :: Int -> Word32 -> Node k v -> Word32 -> Node k v -> Node k v
mergeTwo shift leftHash leftNode rightHash rightNode
  | shift >= hashBits = Collision leftHash (nodeEntries leftNode ++ nodeEntries rightNode)
  | leftBit == rightBit = Branch leftBit [mergeTwo (shift + bitsPerLevel) leftHash leftNode rightHash rightNode]
  | leftBit < rightBit = Branch (leftBit `xor` rightBit) [leftNode, rightNode]
  | otherwise = Branch (leftBit `xor` rightBit) [rightNode, leftNode]
  where
    leftBit = bitFor leftHash shift
    rightBit = bitFor rightHash shift

nodeEntries :: Node k v -> [(k, v)]
nodeEntries EmptyNode = []
nodeEntries (Leaf _ key value) = [(key, value)]
nodeEntries (Collision _ entries) = entries
nodeEntries (Branch _ children) = concatMap nodeEntries children

normalizeBranch :: Word32 -> [Node k v] -> Node k v
normalizeBranch 0 _ = EmptyNode
normalizeBranch _ [child@(Leaf _ _ _)] = child
normalizeBranch _ [child@(Collision _ _)] = child
normalizeBranch bitmap children = Branch bitmap children

mapNode :: (v -> w) -> Node k v -> Node k w
mapNode _ EmptyNode = EmptyNode
mapNode mapper (Leaf hashValue key value) = Leaf hashValue key (mapper value)
mapNode mapper (Collision hashValue entries) = Collision hashValue [(key, mapper value) | (key, value) <- entries]
mapNode mapper (Branch bitmap children) = Branch bitmap (map (mapNode mapper) children)

foldrNode :: ((k, v) -> b -> b) -> b -> Node k v -> b
foldrNode _ seed EmptyNode = seed
foldrNode folder seed (Leaf _ key value) = folder (key, value) seed
foldrNode folder seed (Collision _ entries) = foldr folder seed entries
foldrNode folder seed (Branch _ children) = foldr (flip (foldrNode folder)) seed children

hashFor :: HashPolicy k -> k -> Word32
hashFor hashPolicy key = fromIntegral (hashKey hashPolicy key)

bitFor :: Word32 -> Int -> Word32
bitFor hashValue shift = 1 `shiftL` fromIntegral ((hashValue `shiftR` shift) .&. 0x1f)

childIndex :: Word32 -> Word32 -> Int
childIndex bitmap bit = popCount (bitmap .&. (bit - 1))

replaceAt :: Int -> a -> [a] -> [a]
replaceAt 0 value (_ : rest) = value : rest
replaceAt index value (x : rest) = x : replaceAt (index - 1) value rest
replaceAt _ _ [] = []

insertAt :: Int -> a -> [a] -> [a]
insertAt 0 value values = value : values
insertAt index value (x : rest) = x : insertAt (index - 1) value rest
insertAt _ value [] = [value]

removeAt :: Int -> [a] -> [a]
removeAt 0 (_ : rest) = rest
removeAt index (x : rest) = x : removeAt (index - 1) rest
removeAt _ [] = []

bitsPerLevel :: Int
bitsPerLevel = 5

hashBits :: Int
hashBits = 32
