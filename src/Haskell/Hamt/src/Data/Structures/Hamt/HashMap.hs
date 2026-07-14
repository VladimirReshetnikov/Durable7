{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MagicHash #-}

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
  , sharesRootWith
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
  , union
  , intersection
  , difference
  , symmetricDifference
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
import GHC.Exts (isTrue#, reallyUnsafePtrEquality#)

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
  | Collision !Int !Word32 [(k, v)]
  | Branch !Int !Word32 !Word32 [(Word32, k, v)] [Node k v]

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
validStructure (HashMap _ expectedCount root) = nodeCountIfValid 0 0 0 root == Just expectedCount

-- | Compares canonical node kinds, bitmap placement, stored hashes, collision
-- key sets, and child topology. Both maps must use compatible key policies;
-- values remain irrelevant to topology.
sameTopology :: HashMap k v -> HashMap k v' -> Bool
sameTopology (HashMap leftPolicy _ left) (HashMap _ _ right) =
  sameNodeTopology leftPolicy left right

-- | Reports whether two maps retain the exact same immutable root node.
-- This is stronger than 'sameTopology' and is useful when validating no-op
-- updates and clean transient publication.
sharesRootWith :: HashMap k v -> HashMap k v -> Bool
sharesRootWith (HashMap _ _ left) (HashMap _ _ right) = ptrEq left right

sameNodeTopology :: HashPolicy k -> Node k v -> Node k v' -> Bool
sameNodeTopology _ EmptyNode EmptyNode = True
sameNodeTopology _ (Leaf leftHash _ _) (Leaf rightHash _ _) = leftHash == rightHash
sameNodeTopology hashPolicy (Collision _ leftHash leftEntries) (Collision _ rightHash rightEntries) =
  leftHash == rightHash
    && length leftEntries == length rightEntries
    && all (hasEquivalentKey rightEntries . fst) leftEntries
  where
    hasEquivalentKey entries key = any (equalKeys hashPolicy key . fst) entries
sameNodeTopology hashPolicy (Branch _ leftDataMap leftNodeMap leftPayloads leftChildren)
                               (Branch _ rightDataMap rightNodeMap rightPayloads rightChildren) =
  leftDataMap == rightDataMap
    && leftNodeMap == rightNodeMap
    && map payloadHash leftPayloads == map payloadHash rightPayloads
    && length leftChildren == length rightChildren
    && and (zipWith (sameNodeTopology hashPolicy) leftChildren rightChildren)
  where
    payloadHash (entryHash, _, _) = entryHash
sameNodeTopology _ _ _ = False

nodeCountIfValid :: Int -> Word32 -> Word32 -> Node k v -> Maybe Int
nodeCountIfValid _ _ _ EmptyNode = Just 0
nodeCountIfValid _ prefix prefixMask (Leaf entryHash _ _)
  | hashHasPrefix entryHash prefix prefixMask = Just 1
  | otherwise = Nothing
nodeCountIfValid _ prefix prefixMask (Collision cachedCount entryHash entries)
  | length entries >= 2
      && cachedCount == length entries
      && hashHasPrefix entryHash prefix prefixMask = Just cachedCount
  | otherwise = Nothing
nodeCountIfValid shift prefix _prefixMask (Branch cachedCount dataMap nodeMap payloads children)
  | shift > 30 = Nothing
  | dataMap .&. nodeMap /= 0 = Nothing
  | dataMap == 0 && nodeMap == 0 = Nothing
  | popCount dataMap /= length payloads = Nothing
  | popCount nodeMap /= length children = Nothing
  | any isLeaf children = Nothing
  | length payloads + length children < 2 && not (List.null payloads && singleBranch children) = Nothing
  | any invalidTerminalSlot (dataSlots ++ nodeSlots) = Nothing
  | not (and (zipWith payloadRoutesTo dataSlots payloads)) = Nothing
  | otherwise = do
      childCount <- sum <$> traverse validateChild (zip nodeSlots children)
      let actualCount = length payloads + childCount
      if cachedCount == actualCount then Just cachedCount else Nothing
  where
    dataSlots = bitmapSlots dataMap
    nodeSlots = bitmapSlots nodeMap
    nextMask = nextPrefixMask shift
    slotPrefix slot = prefix .|. (fromIntegral slot `shiftL` shift)
    payloadRoutesTo slot (entryHash, _, _) =
      hashHasPrefix entryHash (slotPrefix slot) nextMask
    validateChild (slot, child) =
      nodeCountIfValid (shift + 5) (slotPrefix slot) nextMask child
    invalidTerminalSlot slot = shift == 30 && slot > 3
    isLeaf (Leaf _ _ _) = True
    isLeaf _ = False
    singleBranch [Branch _ _ _ _ _] = True
    singleBranch _ = False

hashHasPrefix :: Word32 -> Word32 -> Word32 -> Bool
hashHasPrefix entryHash prefix prefixMask = entryHash .&. prefixMask == prefix

nextPrefixMask :: Int -> Word32
nextPrefixMask shift
  | shift >= 27 = maxBound
  | otherwise = (1 `shiftL` (shift + 5)) - 1

bitmapSlots :: Word32 -> [Int]
bitmapSlots bitmap = [slot | slot <- [0 .. 31], bitmap .&. (1 `shiftL` slot) /= 0]

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

union :: Eq v => HashMap k v -> HashMap k v -> HashMap k v
union = combineMaps CombineUnion

intersection :: Eq v => HashMap k v -> HashMap k v -> HashMap k v
intersection = combineMaps CombineIntersection

difference :: Eq v => HashMap k v -> HashMap k v -> HashMap k v
difference = combineMaps CombineDifference

symmetricDifference :: Eq v => HashMap k v -> HashMap k v -> HashMap k v
symmetricDifference = combineMaps CombineSymmetricDifference

toList :: HashMap k v -> [(k, v)]
toList (HashMap _ _ root) = foldrNode (:) [] root

keys :: HashMap k v -> [k]
keys = fmap fst . toList

elems :: HashMap k v -> [v]
elems = fmap snd . toList

foldrWithKey :: (k -> v -> b -> b) -> b -> HashMap k v -> b
foldrWithKey folder seed (HashMap _ _ root) = foldrNode (\(k, v) acc -> folder k v acc) seed root

-- | Compares contents under the right map's key policy. Maps retaining the
-- same policy object compare canonical nodes in lockstep and prune
-- pointer-identical descendants without key or value callbacks. Independently
-- supplied policies retain semantic lookup comparison because compatible key
-- equivalence functions may use different coherent hash functions.
mapEquals :: Eq v => HashMap k v -> HashMap k v -> Bool
mapEquals (HashMap leftPolicy leftSize leftRoot) right@(HashMap rightPolicy rightSize rightRoot)
  | ptrEq leftRoot rightRoot = True
  | leftSize /= rightSize = False
  | policiesShareFunctions leftPolicy rightPolicy = nodesEqual rightPolicy 0 leftRoot rightRoot
  | otherwise = all matches (nodeEntries leftRoot)
  where
    matches (key, value) = maybe False (valuesEqual value) (lookup key right)

-- | Reports semantic changes. Maps retaining the same policy object use a
-- lockstep canonical-node traversal and prune pointer-identical descendants.
-- Independently supplied policies retain the lookup fallback. The result keeps
-- left removals/changes before right additions.
diff :: Eq v => HashMap k v -> HashMap k v -> [MapDifference k v]
diff left@(HashMap leftPolicy _ leftRoot) right@(HashMap rightPolicy _ rightRoot)
  | ptrEq leftRoot rightRoot = []
  | policiesShareFunctions leftPolicy rightPolicy = removedOrChanged ++ added
  | otherwise = semanticRemovedOrChanged ++ semanticAdded
  where
    (removedOrChanged, added) = diffNodes rightPolicy 0 leftRoot rightRoot
    semanticRemovedOrChanged = concatMap classifyLeft (toList left)
    classifyLeft (key, before) = case lookup key right of
      Nothing -> [EntryRemoved key before]
      Just after | not (valuesEqual before after) -> [EntryChanged key before after]
      _ -> []
    semanticAdded =
      [EntryAdded key value | (key, value) <- toList right, not (member key left)]

-- Compare canonical nodes in lockstep. A positive pointer comparison is enough
-- to prune an immutable descendant without invoking key or value equality; a
-- negative result is never used as a semantic verdict.
nodesEqual :: Eq v => HashPolicy k -> Int -> Node k v -> Node k v -> Bool
nodesEqual _ _ left right | nodePtrEq left right = True
nodesEqual _ _ left right | nodeCardinality left /= nodeCardinality right = False
nodesEqual _ _ EmptyNode EmptyNode = True
nodesEqual hashPolicy _ left right
  | isHashNode left && isHashNode right =
      hashNodeHash left == hashNodeHash right
        && hashEntriesEqual hashPolicy (nodeEntries left) (nodeEntries right)
nodesEqual hashPolicy shift left right
  | shift < hashBits = all slotEqual (bitmapSlots occupied)
  | otherwise = False
  where
    occupied = nodeOccupiedBitmap left shift .|. nodeOccupiedBitmap right shift
    slotEqual slot =
      nodesEqual
        hashPolicy
        (shift + bitsPerLevel)
        (logicalSlot left slot shift)
        (logicalSlot right slot shift)

hashEntriesEqual :: Eq v => HashPolicy k -> [(k, v)] -> [(k, v)] -> Bool
hashEntriesEqual hashPolicy leftEntries rightEntries =
  length leftEntries == length rightEntries && all matches leftEntries
  where
    matches (leftKey, leftValue) = case findEquivalentEntry hashPolicy leftKey rightEntries of
      Just (_, rightValue) -> valuesEqual leftValue rightValue
      Nothing -> False

-- Return left-side removals/changes separately from right-side additions so
-- the public result retains its established left-traversal-then-right-traversal
-- ordering even while the implementation walks paired logical slots.
diffNodes
  :: Eq v
  => HashPolicy k
  -> Int
  -> Node k v
  -> Node k v
  -> ([MapDifference k v], [MapDifference k v])
diffNodes _ _ left right | nodePtrEq left right = ([], [])
diffNodes _ _ EmptyNode right =
  ([], [EntryAdded key value | (key, value) <- nodeEntries right])
diffNodes _ _ left EmptyNode =
  ([EntryRemoved key value | (key, value) <- nodeEntries left], [])
diffNodes hashPolicy _ left right
  | isHashNode left && isHashNode right = diffHashNodes hashPolicy left right
diffNodes hashPolicy shift left right
  | shift < hashBits =
      ( concatMap (fst . resultFor) (nodeTraversalSlots left shift)
      , concatMap (snd . resultFor) (nodeTraversalSlots right shift)
      )
  | otherwise = diffHashEntries hashPolicy (nodeEntries left) (nodeEntries right)
  where
    occupied = nodeOccupiedBitmap left shift .|. nodeOccupiedBitmap right shift
    slotResults =
      [ ( slot
        , diffNodes
            hashPolicy
            (shift + bitsPerLevel)
            (logicalSlot left slot shift)
            (logicalSlot right slot shift)
        )
      | slot <- bitmapSlots occupied
      ]
    resultFor slot = case List.find ((== slot) . fst) slotResults of
      Just (_, result) -> result
      Nothing -> error "occupied CHAMP slot has no paired diff result"

diffHashNodes
  :: Eq v
  => HashPolicy k
  -> Node k v
  -> Node k v
  -> ([MapDifference k v], [MapDifference k v])
diffHashNodes hashPolicy left right
  | hashNodeHash left == hashNodeHash right =
      diffHashEntries hashPolicy (nodeEntries left) (nodeEntries right)
  | otherwise =
      ( [EntryRemoved key value | (key, value) <- nodeEntries left]
      , [EntryAdded key value | (key, value) <- nodeEntries right]
      )

diffHashEntries
  :: Eq v
  => HashPolicy k
  -> [(k, v)]
  -> [(k, v)]
  -> ([MapDifference k v], [MapDifference k v])
diffHashEntries hashPolicy leftEntries rightEntries =
  (concatMap classifyLeft leftEntries, concatMap classifyRight rightEntries)
  where
    classifyLeft (leftKey, leftValue) = case findEquivalentEntry hashPolicy leftKey rightEntries of
      Nothing -> [EntryRemoved leftKey leftValue]
      Just (_, rightValue)
        | valuesEqual leftValue rightValue -> []
        | otherwise -> [EntryChanged leftKey leftValue rightValue]
    classifyRight (rightKey, rightValue) = case findEquivalentEntry hashPolicy rightKey leftEntries of
      Nothing -> [EntryAdded rightKey rightValue]
      Just _ -> []

findEquivalentEntry :: HashPolicy k -> k -> [(k, v)] -> Maybe (k, v)
findEquivalentEntry hashPolicy key = List.find (equalKeys hashPolicy key . fst)

nodeOccupiedBitmap :: Node k v -> Int -> Word32
nodeOccupiedBitmap EmptyNode _ = 0
nodeOccupiedBitmap (Leaf hashValue _ _) shift = bitFor hashValue shift
nodeOccupiedBitmap (Collision _ hashValue _) shift = bitFor hashValue shift
nodeOccupiedBitmap (Branch _ dataMap nodeMap _ _) _ = dataMap .|. nodeMap

-- Enumeration visits a branch's inline payload run before its child run. This
-- slot order lets 'diffNodes' preserve that public traversal-derived ordering.
nodeTraversalSlots :: Node k v -> Int -> [Int]
nodeTraversalSlots EmptyNode _ = []
nodeTraversalSlots (Leaf hashValue _ _) shift = [hashIndex hashValue shift]
nodeTraversalSlots (Collision _ hashValue _) shift = [hashIndex hashValue shift]
nodeTraversalSlots (Branch _ dataMap nodeMap _ _) _ =
  bitmapSlots dataMap ++ bitmapSlots nodeMap

data CombineOperation
  = CombineUnion
  | CombineIntersection
  | CombineDifference
  | CombineSymmetricDifference
  deriving (Eq)

combineMaps :: Eq v => CombineOperation -> HashMap k v -> HashMap k v -> HashMap k v
combineMaps operation (HashMap leftPolicy _ leftRoot) (HashMap rightPolicy _ rightRoot)
  | ptrEq leftRoot rightRoot = fromRoot $ case operation of
      CombineUnion -> leftRoot
      CombineIntersection -> leftRoot
      CombineDifference -> EmptyNode
      CombineSymmetricDifference -> EmptyNode
  | policiesShareFunctions leftPolicy rightPolicy =
      fromRoot (combineNodes leftPolicy operation 0 leftRoot rightRoot)
  | otherwise =
      let HashMap _ _ normalizedRight = fromListWith leftPolicy (nodeEntries rightRoot)
       in fromRoot (combineNodes leftPolicy operation 0 leftRoot normalizedRight)
  where
    fromRoot root = HashMap leftPolicy (nodeCardinality root) root

combineNodes :: Eq v => HashPolicy k -> CombineOperation -> Int -> Node k v -> Node k v -> Node k v
combineNodes _ operation _ left right | nodePtrEq left right = case operation of
  CombineUnion -> left
  CombineIntersection -> left
  CombineDifference -> EmptyNode
  CombineSymmetricDifference -> EmptyNode
combineNodes _ operation _ EmptyNode right = case operation of
  CombineUnion -> right
  CombineIntersection -> EmptyNode
  CombineDifference -> EmptyNode
  CombineSymmetricDifference -> right
combineNodes _ operation _ left EmptyNode = case operation of
  CombineIntersection -> EmptyNode
  _ -> left
combineNodes hashPolicy operation shift left right
  | isHashNode left && isHashNode right =
      combineHashNodes hashPolicy operation shift left right
  | otherwise =
      buildLogicalNode hashPolicy shift left
        [ combineNodes hashPolicy operation (shift + bitsPerLevel)
            (logicalSlot left index shift)
            (logicalSlot right index shift)
        | index <- [0 .. 31]
        ]

combineHashNodes :: Eq v => HashPolicy k -> CombineOperation -> Int -> Node k v -> Node k v -> Node k v
combineHashNodes hashPolicy operation shift left right
  | leftHash /= rightHash = case operation of
      CombineIntersection -> EmptyNode
      CombineDifference -> left
      _
        | shift >= hashBits -> error "distinct 32-bit hashes exhausted every CHAMP level"
        | leftIndex /= rightIndex ->
            buildLogicalNode hashPolicy shift left
              (replaceAt rightIndex right (replaceAt leftIndex left emptySlots))
        | otherwise ->
            buildLogicalNode hashPolicy shift left
              (replaceAt leftIndex
                (combineHashNodes hashPolicy operation (shift + bitsPerLevel) left right)
                emptySlots)
  | entriesMatch hashPolicy leftEntries resultEntries = left
  | otherwise = entriesToNode leftHash resultEntries
  where
    leftHash = hashNodeHash left
    rightHash = hashNodeHash right
    leftIndex = hashIndex leftHash shift
    rightIndex = hashIndex rightHash shift
    emptySlots = replicate 32 EmptyNode
    leftEntries = nodeEntries left
    rightEntries = nodeEntries right
    resultEntries = combineHashEntries hashPolicy operation leftEntries rightEntries

combineHashEntries :: Eq v => HashPolicy k -> CombineOperation -> [(k, v)] -> [(k, v)] -> [(k, v)]
combineHashEntries hashPolicy operation leftEntries rightEntries = leftResults ++ rightOnly
  where
    leftResults = concatMap combineLeft leftEntries
    combineLeft entry@(leftKey, leftValue) = case findEquivalent leftKey rightEntries of
      Just (_, rightValue) -> case operation of
        CombineUnion -> [(leftKey, if valuesEqual leftValue rightValue then leftValue else rightValue)]
        CombineIntersection -> [entry]
        CombineDifference -> []
        CombineSymmetricDifference -> []
      Nothing -> case operation of
        CombineIntersection -> []
        _ -> [entry]
    rightOnly
      | operation == CombineUnion || operation == CombineSymmetricDifference =
          [entry | entry@(key, _) <- rightEntries, not (any (equalKeys hashPolicy key . fst) leftEntries)]
      | otherwise = []
    findEquivalent key = List.find (equalKeys hashPolicy key . fst)

entriesMatch :: Eq v => HashPolicy k -> [(k, v)] -> [(k, v)] -> Bool
entriesMatch hashPolicy left right =
  length left == length right && and (zipWith matches left right)
  where
    matches (leftKey, leftValue) (rightKey, rightValue) =
      equalKeys hashPolicy leftKey rightKey && valuesEqual leftValue rightValue

logicalSlot :: Node k v -> Int -> Int -> Node k v
logicalSlot EmptyNode _ _ = EmptyNode
logicalSlot node@(Leaf hashValue _ _) index shift
  | hashIndex hashValue shift == index = node
  | otherwise = EmptyNode
logicalSlot node@(Collision _ hashValue _) index shift
  | hashIndex hashValue shift == index = node
  | otherwise = EmptyNode
logicalSlot (Branch _ dataMap nodeMap payloads children) index _
  | dataMap .&. selectedBit /= 0 =
      let (hashValue, key, value) = payloads !! childIndex dataMap selectedBit
       in Leaf hashValue key value
  | nodeMap .&. selectedBit /= 0 = children !! childIndex nodeMap selectedBit
  | otherwise = EmptyNode
  where
    selectedBit = 1 `shiftL` index

buildLogicalNode :: Eq v => HashPolicy k -> Int -> Node k v -> [Node k v] -> Node k v
buildLogicalNode hashPolicy shift original slots
  | logicalSlotsMatch hashPolicy shift original slots = original
  | otherwise = normalizeBranch dataMap nodeMap (reverse payloads) (reverse children)
  where
    (_, dataMap, nodeMap, payloads, children) =
      List.foldl' addSlot (0 :: Int, 0, 0, [], []) slots
    addSlot (!index, !currentDataMap, !currentNodeMap, currentPayloads, currentChildren) node =
      let selectedBit = 1 `shiftL` index
       in case node of
            EmptyNode -> (index + 1, currentDataMap, currentNodeMap, currentPayloads, currentChildren)
            Leaf hashValue key value ->
              ( index + 1
              , currentDataMap .|. selectedBit
              , currentNodeMap
              , (hashValue, key, value) : currentPayloads
              , currentChildren
              )
            _ ->
              ( index + 1
              , currentDataMap
              , currentNodeMap .|. selectedBit
              , currentPayloads
              , node : currentChildren
              )

logicalSlotsMatch :: Eq v => HashPolicy k -> Int -> Node k v -> [Node k v] -> Bool
logicalSlotsMatch hashPolicy shift original slots = and
  [ nodesMatch (logicalSlot original index shift) actual
  | (index, actual) <- zip [0 .. 31] slots
  ]
  where
    nodesMatch expected actual
      | nodePtrEq expected actual = True
    nodesMatch (Leaf leftHash leftKey leftValue) (Leaf rightHash rightKey rightValue) =
      leftHash == rightHash && equalKeys hashPolicy leftKey rightKey && valuesEqual leftValue rightValue
    nodesMatch EmptyNode EmptyNode = True
    nodesMatch _ _ = False

nodeCardinality :: Node k v -> Int
nodeCardinality EmptyNode = 0
nodeCardinality (Leaf _ _ _) = 1
nodeCardinality (Collision count _ _) = count
nodeCardinality (Branch count _ _ _ _) = count

isHashNode :: Node k v -> Bool
isHashNode (Leaf _ _ _) = True
isHashNode (Collision _ _ _) = True
isHashNode _ = False

hashNodeHash :: Node k v -> Word32
hashNodeHash (Leaf hashValue _ _) = hashValue
hashNodeHash (Collision _ hashValue _) = hashValue
hashNodeHash _ = error "branch nodes do not have one full hash"

hashIndex :: Word32 -> Int -> Int
hashIndex hashValue shift = fromIntegral ((hashValue `shiftR` shift) .&. 0x1f)

ptrEq :: a -> a -> Bool
ptrEq left right = isTrue# (reallyUnsafePtrEquality# left right)
{-# INLINE ptrEq #-}

-- Child lists are lazy, so force both internal nodes to constructor form before
-- asking GHC for positive identity. This follows indirections introduced by
-- evaluation while preserving the non-strict value shortcut in 'valuesEqual'.
nodePtrEq :: Node k v -> Node k v -> Bool
nodePtrEq left right = left `seq` right `seq` ptrEq left right
{-# INLINE nodePtrEq #-}

-- A shared record is sufficient but not required: strict projection can copy
-- the record while retaining both exact function closures. Positive identity
-- of both closures is a safe witness that stored hashes and key equivalence
-- came from one policy implementation. A negative result is never semantic.
policiesShareFunctions :: HashPolicy k -> HashPolicy k -> Bool
policiesShareFunctions left right =
  ptrEq left right
    || (ptrEq (hashKey left) (hashKey right) && ptrEq (equalKeys left) (equalKeys right))
{-# INLINE policiesShareFunctions #-}

valuesEqual :: Eq a => a -> a -> Bool
valuesEqual left right = ptrEq left right || left == right
{-# INLINE valuesEqual #-}

lookupNode :: HashPolicy k -> Word32 -> k -> Int -> Node k v -> Maybe v
lookupNode _ _ _ _ EmptyNode = Nothing
lookupNode hashPolicy hashValue key _ (Leaf leafHash leafKey value)
  | hashValue == leafHash && equalKeys hashPolicy key leafKey = Just value
  | otherwise = Nothing
lookupNode hashPolicy hashValue key _ (Collision _ collisionHash entries)
  | hashValue == collisionHash = lookupCollision hashPolicy key entries
  | otherwise = Nothing
lookupNode hashPolicy hashValue key shift (Branch _ dataMap nodeMap payloads children)
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
actualKeyNode hashPolicy hashValue key _ (Collision _ collisionHash entries)
  | hashValue == collisionHash = actualKeyCollision hashPolicy key entries
  | otherwise = Nothing
actualKeyNode hashPolicy hashValue key shift (Branch _ dataMap nodeMap payloads children)
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
insertNode hashPolicy addOnly hashValue key value shift (Collision _ collisionHash entries)
  | hashValue == collisionHash =
      case insertCollision hashPolicy addOnly key value entries of
        InsertedEntries entries' -> Inserted (makeCollision collisionHash entries')
        ReplacedEntries entries' -> Replaced (makeCollision collisionHash entries')
        DuplicateEntry -> Duplicate
  | otherwise = Inserted (mergeTwo shift collisionHash (makeCollision collisionHash entries) hashValue (Leaf hashValue key value))
insertNode hashPolicy addOnly hashValue key value shift (Branch _ dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let dataIndex = childIndex dataMap bit
          (leafHash, leafKey, leafValue) = payloads !! dataIndex
       in if hashValue == leafHash && equalKeys hashPolicy key leafKey
            then if addOnly
              then Duplicate
              else Replaced (makeBranchNode dataMap nodeMap (replaceAt dataIndex (leafHash, leafKey, value) payloads) children)
            else
              let child = mergeTwo (shift + bitsPerLevel) leafHash (Leaf leafHash leafKey leafValue) hashValue (Leaf hashValue key value)
               in Inserted
                    (makeBranchNode
                      (dataMap .&. complement bit)
                      (nodeMap .|. bit)
                      (removeAt dataIndex payloads)
                      (insertAt (childIndex nodeMap bit) child children))
  | nodeMap .&. bit /= 0 =
      let nodeIndex = childIndex nodeMap bit
       in case insertNode hashPolicy addOnly hashValue key value (shift + bitsPerLevel) (children !! nodeIndex) of
            Inserted child -> Inserted (makeBranchNode dataMap nodeMap payloads (replaceAt nodeIndex child children))
            Replaced child -> Replaced (makeBranchNode dataMap nodeMap payloads (replaceAt nodeIndex child children))
            Duplicate -> Duplicate
  | otherwise =
      Inserted (makeBranchNode (dataMap .|. bit) nodeMap (insertAt (childIndex dataMap bit) (hashValue, key, value) payloads) children)
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
removeNode hashPolicy hashValue key _ (Collision _ collisionHash entries)
  | hashValue /= collisionHash = Missing
  | otherwise =
      case removeCollision hashPolicy key entries of
        Nothing -> Missing
        Just (value, remaining) -> Removed (entriesToNode collisionHash remaining) value
removeNode hashPolicy hashValue key shift branch@(Branch _ dataMap nodeMap payloads children)
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
adjustNode hashPolicy updater hashValue key _ (Collision _ collisionHash entries)
  | hashValue /= collisionHash = NotFound
  | otherwise =
      case adjustCollision hashPolicy updater key entries of
        Nothing -> NotFound
        Just entries' -> Adjusted (makeCollision collisionHash entries')
adjustNode hashPolicy updater hashValue key shift (Branch _ dataMap nodeMap payloads children)
  | dataMap .&. bit /= 0 =
      let index = childIndex dataMap bit
          (leafHash, leafKey, value) = payloads !! index
       in if leafHash == hashValue && equalKeys hashPolicy key leafKey
            then let !updated = updater value
                  in Adjusted (makeBranchNode dataMap nodeMap (replaceAt index (leafHash, leafKey, updated) payloads) children)
            else NotFound
  | nodeMap .&. bit /= 0 =
      let index = childIndex nodeMap bit
       in case adjustNode hashPolicy updater hashValue key (shift + bitsPerLevel) (children !! index) of
            NotFound -> NotFound
            Adjusted child -> Adjusted (makeBranchNode dataMap nodeMap payloads (replaceAt index child children))
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

makeCollision :: Word32 -> [(k, v)] -> Node k v
makeCollision hashValue entries = Collision (length entries) hashValue entries

makeBranchNode :: Word32 -> Word32 -> [(Word32, k, v)] -> [Node k v] -> Node k v
makeBranchNode dataMap nodeMap payloads children =
  Branch count dataMap nodeMap payloads children
  where
    count = length payloads + sum (map nodeCardinality children)

entriesToNode :: Word32 -> [(k, v)] -> Node k v
entriesToNode _ [] = EmptyNode
entriesToNode hashValue [(key, value)] = Leaf hashValue key value
entriesToNode hashValue entries = makeCollision hashValue entries

mergeTwo :: Int -> Word32 -> Node k v -> Word32 -> Node k v -> Node k v
mergeTwo shift leftHash leftNode rightHash rightNode
  | shift >= hashBits = makeCollision leftHash (nodeEntries leftNode ++ nodeEntries rightNode)
  | leftBit == rightBit = makeBranchNode 0 leftBit [] [mergeTwo (shift + bitsPerLevel) leftHash leftNode rightHash rightNode]
  | otherwise = makeBranch leftBit leftNode rightBit rightNode
  where
    leftBit = bitFor leftHash shift
    rightBit = bitFor rightHash shift

nodeEntries :: Node k v -> [(k, v)]
nodeEntries EmptyNode = []
nodeEntries (Leaf _ key value) = [(key, value)]
nodeEntries (Collision _ _ entries) = entries
nodeEntries (Branch _ _ _ payloads children) =
  [(key, value) | (_, key, value) <- payloads] ++ concatMap nodeEntries children

makeBranch :: Word32 -> Node k v -> Word32 -> Node k v -> Node k v
makeBranch leftBit leftNode rightBit rightNode =
  makeBranchNode dataMap nodeMap payloads children
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
normalizeBranch 0 _ [] [child@(Collision _ _ _)] = child
normalizeBranch dataMap nodeMap payloads children = makeBranchNode dataMap nodeMap payloads children

singletonPayload :: Node k v -> Maybe (Word32, k, v)
singletonPayload (Leaf hashValue key value) = Just (hashValue, key, value)
singletonPayload (Collision _ hashValue [(key, value)]) = Just (hashValue, key, value)
singletonPayload (Branch _ _ 0 [(hashValue, key, value)] []) = Just (hashValue, key, value)
singletonPayload _ = Nothing

mapNode :: (v -> w) -> Node k v -> Node k w
mapNode _ EmptyNode = EmptyNode
mapNode mapper (Leaf hashValue key value) =
  let !mapped = mapper value
   in Leaf hashValue key mapped
mapNode mapper (Collision count hashValue entries) =
  let !mappedEntries = mapEntriesStrict entries
   in Collision count hashValue mappedEntries
  where
    mapEntriesStrict [] = []
    mapEntriesStrict ((key, value) : rest) =
      let !mapped = mapper value
          !mappedRest = mapEntriesStrict rest
       in (key, mapped) : mappedRest
mapNode mapper (Branch count dataMap nodeMap payloads children) =
  let !mappedPayloads = mapPayloadsStrict payloads
      !mappedChildren = mapNodesStrict children
   in Branch count dataMap nodeMap mappedPayloads mappedChildren
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
foldrNode folder seed (Collision _ _ entries) = foldr folder seed entries
foldrNode folder seed (Branch _ _ _ payloads children) =
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
