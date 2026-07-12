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
  , validStructure
  , sameTopology
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
  , MapDifference(..)
  , mapEquals
  , diff
  ) where

import Prelude hiding (lookup, null)

import Data.Bits (Bits((.&.), (.|.), complement), popCount, shiftL, shiftR)
import qualified Data.List as List
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
  | Branch !Word32 !Word32 [(Word32, k, v)] [Node k v]

data InsertResult k v
  = Inserted !(Node k v)
  | Replaced !(Node k v)
  | Duplicate

data RemoveResult k v
  = Missing
  | Removed !(Node k v) v

data AdjustResult k v
  = NotFound
  | Adjusted !(Node k v)

data MapDifference k v
  = EntryAdded k v
  | EntryRemoved k v
  | EntryChanged k v v
  deriving (Eq, Show)

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
fromListWith hashPolicy = List.foldl' (\m (k, v) -> insert k v m) (emptyWith hashPolicy)

size :: HashMap k v -> Int
size (HashMap _ count _) = count

-- | Checks cached cardinality and canonical node shape without relying on
-- key or value equality. In particular, collision buckets must contain at
-- least two entries; deletion demotes a singleton bucket to a 'Leaf'.
validStructure :: HashMap k v -> Bool
validStructure (HashMap _ expectedCount root) = nodeCountIfValid root == Just expectedCount

-- | Compares canonical node kinds, bitmap placement, stored hashes, and child
-- topology without consulting either map's key or value semantics.
sameTopology :: HashMap k v -> HashMap k' v' -> Bool
sameTopology (HashMap _ _ left) (HashMap _ _ right) = sameNodeTopology left right

sameNodeTopology :: Node k v -> Node k' v' -> Bool
sameNodeTopology EmptyNode EmptyNode = True
sameNodeTopology (Leaf leftHash _ _) (Leaf rightHash _ _) = leftHash == rightHash
sameNodeTopology (Collision leftHash leftEntries) (Collision rightHash rightEntries) =
  leftHash == rightHash && length leftEntries == length rightEntries
sameNodeTopology (Branch leftDataMap leftNodeMap leftPayloads leftChildren)
                 (Branch rightDataMap rightNodeMap rightPayloads rightChildren) =
  leftDataMap == rightDataMap
    && leftNodeMap == rightNodeMap
    && map payloadHash leftPayloads == map payloadHash rightPayloads
    && length leftChildren == length rightChildren
    && and (zipWith sameNodeTopology leftChildren rightChildren)
  where
    payloadHash (entryHash, _, _) = entryHash
sameNodeTopology _ _ = False

nodeCountIfValid :: Node k v -> Maybe Int
nodeCountIfValid EmptyNode = Just 0
nodeCountIfValid (Leaf _ _ _) = Just 1
nodeCountIfValid (Collision _ entries)
  | length entries >= 2 = Just (length entries)
  | otherwise = Nothing
nodeCountIfValid (Branch dataMap nodeMap payloads children)
  | dataMap .&. nodeMap /= 0 = Nothing
  | dataMap == 0 && nodeMap == 0 = Nothing
  | popCount dataMap /= length payloads = Nothing
  | popCount nodeMap /= length children = Nothing
  | any isLeaf children = Nothing
  | length payloads + length children < 2 && not (List.null payloads && singleBranch children) = Nothing
  | otherwise = (length payloads +) . sum <$> traverse nodeCountIfValid children
  where
    isLeaf (Leaf _ _ _) = True
    isLeaf _ = False
    singleBranch [Branch _ _ _ _] = True
    singleBranch _ = False

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
adjust updater key mapValue@(HashMap hashPolicy count root) =
  case adjustNode hashPolicy updater (hashFor hashPolicy key) key 0 root of
    NotFound -> mapValue
    Adjusted root' -> HashMap hashPolicy count root'

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

mapEquals :: Eq v => HashMap k v -> HashMap k v -> Bool
mapEquals left right =
  size left == size right && all (\(key, value) -> lookup key right == Just value) (toList left)

diff :: Eq v => HashMap k v -> HashMap k v -> [MapDifference k v]
diff left right = removedOrChanged ++ added
  where
    removedOrChanged = concatMap classifyLeft (toList left)
    classifyLeft (key, before) = case lookup key right of
      Nothing -> [EntryRemoved key before]
      Just after | before /= after -> [EntryChanged key before after]
      _ -> []
    added = [EntryAdded key value | (key, value) <- toList right, not (member key left)]

lookupNode :: HashPolicy k -> Word32 -> k -> Int -> Node k v -> Maybe v
lookupNode _ _ _ _ EmptyNode = Nothing
lookupNode hashPolicy hashValue key _ (Leaf leafHash leafKey value)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey = Just value
  | otherwise = Nothing
lookupNode hashPolicy hashValue key _ (Collision collisionHash entries)
  | hashValue == collisionHash = lookupCollision hashPolicy key entries
  | otherwise = Nothing
lookupNode hashPolicy hashValue key shift (Branch dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let (_, candidate, value) = payloads !! childIndex dataMap bit
       in if equalKeys hashPolicy key candidate then Just value else Nothing
  | nodeMap .&. bit /= 0 =
      lookupNode hashPolicy hashValue key (shift + bitsPerLevel) (children !! childIndex nodeMap bit)
  | otherwise = Nothing
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
actualKeyNode hashPolicy hashValue key shift (Branch dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let (_, candidate, _) = payloads !! childIndex dataMap bit
       in if equalKeys hashPolicy key candidate then Just candidate else Nothing
  | nodeMap .&. bit /= 0 =
      actualKeyNode hashPolicy hashValue key (shift + bitsPerLevel) (children !! childIndex nodeMap bit)
  | otherwise = Nothing
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
insertNode hashPolicy addOnly hashValue key value shift (Branch dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let dataIndex = childIndex dataMap bit
          (leafHash, leafKey, leafValue) = payloads !! dataIndex
       in if hashValue == leafHash && equalKeys hashPolicy key leafKey
            then if addOnly
              then Duplicate
              else Replaced (Branch dataMap nodeMap (replaceAt dataIndex (leafHash, leafKey, value) payloads) children)
            else
              let child = mergeTwo (shift + bitsPerLevel) leafHash (Leaf leafHash leafKey leafValue) hashValue (Leaf hashValue key value)
               in Inserted
                    (Branch
                      (dataMap .&. complement bit)
                      (nodeMap .|. bit)
                      (removeAt dataIndex payloads)
                      (insertAt (childIndex nodeMap bit) child children))
  | nodeMap .&. bit /= 0 =
      let nodeIndex = childIndex nodeMap bit
       in case insertNode hashPolicy addOnly hashValue key value (shift + bitsPerLevel) (children !! nodeIndex) of
            Inserted child -> Inserted (Branch dataMap nodeMap payloads (replaceAt nodeIndex child children))
            Replaced child -> Replaced (Branch dataMap nodeMap payloads (replaceAt nodeIndex child children))
            Duplicate -> Duplicate
  | otherwise =
      Inserted (Branch (dataMap .|. bit) nodeMap (insertAt (childIndex dataMap bit) (hashValue, key, value) payloads) children)
  where
    bit = bitFor hashValue shift

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
removeNode hashPolicy hashValue key shift branch@(Branch dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let index = childIndex dataMap bit
          (leafHash, leafKey, value) = payloads !! index
       in if leafHash == hashValue && equalKeys hashPolicy key leafKey
            then Removed (normalizeBranch (dataMap .&. complement bit) nodeMap (removeAt index payloads) children) value
            else Missing
  | nodeMap .&. bit == 0 = keep branch
  | otherwise =
      let index = childIndex nodeMap bit
       in case removeNode hashPolicy hashValue key (shift + bitsPerLevel) (children !! index) of
            Missing -> keep branch
            Removed EmptyNode value ->
              Removed (normalizeBranch dataMap (nodeMap .&. complement bit) payloads (removeAt index children)) value
            Removed child value ->
              case singletonPayload child of
                Just payload -> Removed
                  (normalizeBranch
                    (dataMap .|. bit)
                    (nodeMap .&. complement bit)
                    (insertAt (childIndex dataMap bit) payload payloads)
                    (removeAt index children)) value
                Nothing -> Removed (normalizeBranch dataMap nodeMap payloads (replaceAt index child children)) value
  where
    bit = bitFor hashValue shift
    keep _ = Missing

removeCollision :: HashPolicy k -> k -> [(k, v)] -> Maybe (v, [(k, v)])
removeCollision _ _ [] = Nothing
removeCollision hashPolicy key ((candidate, value) : rest)
  | equalKeys hashPolicy key candidate = Just (value, rest)
  | otherwise =
      case removeCollision hashPolicy key rest of
        Nothing -> Nothing
        Just (removed, remaining) -> Just (removed, (candidate, value) : remaining)

-- Updates an existing value and rebuilds exactly the visited trie spine.
-- The old lookup-plus-insert implementation traversed the same hash path
-- twice and repeated the collision-bucket search.
adjustNode :: HashPolicy k -> (v -> v) -> Word32 -> k -> Int -> Node k v -> AdjustResult k v
adjustNode _ _ _ _ _ EmptyNode = NotFound
adjustNode hashPolicy updater hashValue key _ (Leaf leafHash leafKey value)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey =
      let !updated = updater value
       in Adjusted (Leaf leafHash leafKey updated)
  | otherwise = NotFound
adjustNode hashPolicy updater hashValue key _ (Collision collisionHash entries)
  | hashValue /= collisionHash = NotFound
  | otherwise =
      case adjustCollision hashPolicy updater key entries of
        Nothing -> NotFound
        Just entries' -> Adjusted (Collision collisionHash entries')
adjustNode hashPolicy updater hashValue key shift (Branch dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let index = childIndex dataMap bit
          (leafHash, leafKey, value) = payloads !! index
       in if leafHash == hashValue && equalKeys hashPolicy key leafKey
            then let !updated = updater value
                  in Adjusted (Branch dataMap nodeMap (replaceAt index (leafHash, leafKey, updated) payloads) children)
            else NotFound
  | nodeMap .&. bit /= 0 =
      let index = childIndex nodeMap bit
       in case adjustNode hashPolicy updater hashValue key (shift + bitsPerLevel) (children !! index) of
            NotFound -> NotFound
            Adjusted child -> Adjusted (Branch dataMap nodeMap payloads (replaceAt index child children))
  | otherwise = NotFound
  where
    bit = bitFor hashValue shift

adjustCollision :: HashPolicy k -> (v -> v) -> k -> [(k, v)] -> Maybe [(k, v)]
adjustCollision _ _ _ [] = Nothing
adjustCollision hashPolicy updater key ((candidate, value) : rest)
  | equalKeys hashPolicy key candidate =
      let !updated = updater value
       in Just ((candidate, updated) : rest)
  | otherwise = ((candidate, value) :) <$> adjustCollision hashPolicy updater key rest

entriesToNode :: Word32 -> [(k, v)] -> Node k v
entriesToNode _ [] = EmptyNode
entriesToNode hashValue [(key, value)] = Leaf hashValue key value
entriesToNode hashValue entries = Collision hashValue entries

mergeTwo :: Int -> Word32 -> Node k v -> Word32 -> Node k v -> Node k v
mergeTwo shift leftHash leftNode rightHash rightNode
  | shift >= hashBits = Collision leftHash (nodeEntries leftNode ++ nodeEntries rightNode)
  | leftBit == rightBit = Branch 0 leftBit [] [mergeTwo (shift + bitsPerLevel) leftHash leftNode rightHash rightNode]
  | otherwise = makeBranch leftBit leftNode rightBit rightNode
  where
    leftBit = bitFor leftHash shift
    rightBit = bitFor rightHash shift

nodeEntries :: Node k v -> [(k, v)]
nodeEntries EmptyNode = []
nodeEntries (Leaf _ key value) = [(key, value)]
nodeEntries (Collision _ entries) = entries
nodeEntries (Branch _ _ payloads children) =
  [(key, value) | (_, key, value) <- payloads] ++ concatMap nodeEntries children

makeBranch :: Word32 -> Node k v -> Word32 -> Node k v -> Node k v
makeBranch leftBit leftNode rightBit rightNode =
  Branch dataMap nodeMap payloads children
  where
    slots = if leftBit < rightBit then [(leftBit, leftNode), (rightBit, rightNode)] else [(rightBit, rightNode), (leftBit, leftNode)]
    payloadSlots = [(bit, hashValue, key, value) | (bit, Leaf hashValue key value) <- slots]
    nodeSlots = [(bit, node) | (bit, node) <- slots, not (isLeaf node)]
    dataMap = foldr ((.|.) . fst4) 0 payloadSlots
    nodeMap = foldr ((.|.) . fst) 0 nodeSlots
    payloads = [(hashValue, key, value) | (_, hashValue, key, value) <- payloadSlots]
    children = map snd nodeSlots
    fst4 (a, _, _, _) = a
    isLeaf (Leaf _ _ _) = True
    isLeaf _ = False

normalizeBranch :: Word32 -> Word32 -> [(Word32, k, v)] -> [Node k v] -> Node k v
normalizeBranch 0 0 _ _ = EmptyNode
normalizeBranch _ 0 [(hashValue, key, value)] [] = Leaf hashValue key value
normalizeBranch 0 _ [] [child@(Collision _ _)] = child
normalizeBranch dataMap nodeMap payloads children = Branch dataMap nodeMap payloads children

singletonPayload :: Node k v -> Maybe (Word32, k, v)
singletonPayload (Leaf hashValue key value) = Just (hashValue, key, value)
singletonPayload (Collision hashValue [(key, value)]) = Just (hashValue, key, value)
singletonPayload (Branch _ 0 [(hashValue, key, value)] []) = Just (hashValue, key, value)
singletonPayload _ = Nothing

mapNode :: (v -> w) -> Node k v -> Node k w
mapNode _ EmptyNode = EmptyNode
mapNode mapper (Leaf hashValue key value) =
  let !mapped = mapper value
   in Leaf hashValue key mapped
mapNode mapper (Collision hashValue entries) =
  let !mappedEntries = mapEntriesStrict entries
   in Collision hashValue mappedEntries
  where
    mapEntriesStrict [] = []
    mapEntriesStrict ((key, value) : rest) =
      let !mapped = mapper value
          !mappedRest = mapEntriesStrict rest
       in (key, mapped) : mappedRest
mapNode mapper (Branch dataMap nodeMap payloads children) =
  let !mappedPayloads = mapPayloadsStrict payloads
      !mappedChildren = mapNodesStrict children
   in Branch dataMap nodeMap mappedPayloads mappedChildren
  where
    mapPayloadsStrict [] = []
    mapPayloadsStrict ((hashValue, key, value) : rest) =
      let !mapped = mapper value
          !mappedRest = mapPayloadsStrict rest
       in (hashValue, key, mapped) : mappedRest
    mapNodesStrict [] = []
    mapNodesStrict (child : rest) =
      let !mapped = mapNode mapper child
          !mappedRest = mapNodesStrict rest
       in mapped : mappedRest

foldrNode :: ((k, v) -> b -> b) -> b -> Node k v -> b
foldrNode _ seed EmptyNode = seed
foldrNode folder seed (Leaf _ key value) = folder (key, value) seed
foldrNode folder seed (Collision _ entries) = foldr folder seed entries
foldrNode folder seed (Branch _ _ payloads children) =
  let childSeed = foldr (flip (foldrNode folder)) seed children
   in foldr (\(_, key, value) acc -> folder (key, value) acc) childSeed payloads

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
