{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MagicHash #-}

-- | A fully branching persistent insertion-only connectivity forest that reports the first
-- ancestor version in which two vertices became connected.
--
-- Vertices are the fixed integer universe @0 .. vertexCount - 1@. Each successful 'link' performs
-- union by size /without/ path compression and labels the one new root-parent edge with the child
-- 'AncestralConnectionVersion'. A redundant (already connected) link still creates a child history
-- version, but shares the complete connectivity index, which 'sharesConnectivityRootWith'
-- confirms.
--
-- The parent cells live sparsely in a persistent CHAMP map ("Durable7.Hamt.HashMap"). An absent
-- cell denotes a singleton root, so 'create' is @O(1)@ rather than @O(vertexCount)@. Union by size
-- limits every parent path to @O(log n)@ cells. 'firstConnected' compares the two current parent
-- paths and takes the latest edge-version strictly below their forest lowest common ancestor, so
-- it never searches the version history and its cost is independent of history depth.
--
-- Instances and version tokens are immutable snapshots safe for concurrent readers. Deletion,
-- confluent merging, retroactive updates, and path compression are deliberately outside this
-- type's contract. The @O(log n)@ parent-path factor is worst-case logarithmic in component size
-- rather than the inverse-Ackermann amortized bound of an ephemeral linear-history union-find.
--
-- == Cost of the CHAMP path, and a divergence from the managed reference
--
-- Throughout this module @w@ denotes the cost of one "Durable7.Hamt.HashMap" cell lookup or
-- insertion. That cost is bounded /in expectation/, not unconditionally. The map hashes a key with
-- 'Durable7.Hamt.Hashable.hash' and truncates the result to 32 bits, branching five bits per
-- level, so the trie itself is at most seven levels deep unconditionally; but two distinct 'Int'
-- vertices whose 32-bit hashes agree share a collision bucket whose scan is linear in that
-- bucket's occupancy. Because the mixing function is a fixed deterministic one rather than a
-- per-process randomized hasher, a colliding vertex set is a property of the universe rather than
-- of the run. Over an ordinary vertex universe the expected bucket occupancy is @O(1)@, so @w@ is
-- @O(1)@ in expectation and every per-operation bound stated here is an expected bound in @w@ and
-- a worst-case bound in the @O(log n)@ parent-path factor.
--
-- This is a deliberate divergence from the managed reference, which states @O(w log n)@ with @w@
-- unconditionally bounded. Only the hash policy differs: the forest algorithm, the union-by-size
-- height argument, and the @O(log n)@ parent-path factor are identical, and that factor remains
-- worst-case.
--
-- == Version identity
--
-- The managed reference gives 'AncestralConnectionVersion' reference identity, and the Rust port
-- uses pointer identity on a retained handle. A version here is an ordinary immutable value, so
-- identity is /structural/: two versions are equal exactly when they were produced by the same
-- history, that is, by the same universe size followed by the same sequence of 'link' endpoint
-- pairs. This distinguishes every version the reference distinguishes except the ones referential
-- transparency makes indistinguishable: @link 1 0@ applied twice to one parent yields one value,
-- not two, and two @create 5@ roots are one value. Sibling branches created by /different/ link
-- endpoints stay distinct, which is what the first-connection witness depends on.
--
-- Comparing versions is @O(1)@ when the two are the same retained value or their depths differ,
-- and otherwise walks the shared history to the first difference. Every walk over a parent chain
-- in this module -- equality, 'versionRoot', and the 'validateStructure' lineage audit -- is
-- accumulator-tail-recursive rather than a deep non-tail recursion, because a history can be
-- hundreds of thousands of links deep. The native ports iterate for the same reason when releasing
-- a chain; a Haskell chain is released by the garbage collector without any per-node recursion.
--
-- Depth is an 'Integer' rather than a fixed-width counter, so the reference's history-depth
-- overflow failure is vacuous here and 'link' has no overflow mode.
module Durable7.Hamt.PersistentAncestralConnectionForest
  ( PersistentAncestralConnectionForest
  , AncestralConnectionVersion
  , AncestralConnectionForestStatistics(..)
  , create
  , vertexCount
  , componentCount
  , version
  , versionParent
  , versionDepth
  , versionRoot
  , find
  , connected
  , componentSize
  , link
  , firstConnected
  , validateStructure
  , sharesConnectivityRootWith
  ) where

import Control.Exception (evaluate)
import qualified Data.List as List
import qualified Data.Map.Strict as Map
import Data.Maybe (isJust)
import GHC.Exts (isTrue#, reallyUnsafePtrEquality#)
import System.Mem.StableName (eqStableName, makeStableName)

import Durable7.Hamt.HashMap (HashMap)
import qualified Durable7.Hamt.HashMap as HashMap

-- | Identifies one immutable version in a 'PersistentAncestralConnectionForest' history tree.
--
-- The parent chain contains history identities, not mutable connectivity state. Equality is
-- structural over that chain, as described in the module header; 'show' reports only the depth so
-- that printing a deep history is @O(1)@.
data AncestralConnectionVersion
  = HistoryRoot !Int
    -- ^ A history root, labelled with its forest's fixed vertex-universe size.
  | HistoryLink
      !AncestralConnectionVersion
      !Int
      !Int
      !Integer
      !AncestralConnectionVersion
    -- ^ Parent version, the two 'link' endpoints as supplied, this version's depth, and the
    -- history root of the whole tree.

instance Eq AncestralConnectionVersion where
  (==) = sameVersion

instance Show AncestralConnectionVersion where
  show token = "AncestralConnectionVersion {depth = " ++ show (versionDepth token) ++ "}"

-- | One sparse parent record: the stored parent vertex, the union-by-size count, and the version
-- that created this vertex's parent edge.
--
-- @size@ is meaningful only for a root (a parent equal to the vertex itself) and the version tag
-- is present only on a non-root edge. An absent cell canonically means \"singleton root of size
-- 1\".
data Cell = Cell !Int !Int !(Maybe AncestralConnectionVersion)

-- | One vertex on a parent path, paired with the version that created its parent edge.
data PathStep = PathStep !Int !(Maybe AncestralConnectionVersion)

-- | A fully branching persistent insertion-only connectivity forest.
--
-- The fields are the fixed vertex-universe size, the cached component count, the sparse parent
-- cells, and this version's history token.
data PersistentAncestralConnectionForest = PersistentAncestralConnectionForest
  !Int
  !Int
  !(HashMap Int Cell)
  !AncestralConnectionVersion

-- | Independently recomputed facts about one forest version, returned by 'validateStructure'.
data AncestralConnectionForestStatistics = AncestralConnectionForestStatistics
  { statisticsVertexCount :: !Int
    -- ^ The fixed vertex-universe size.
  , statisticsComponentCount :: !Int
    -- ^ The number of connected components, recounted from the parent forest.
  , statisticsStoredCellCount :: !Int
    -- ^ The number of explicitly stored parent cells; absent cells are singleton roots.
  , statisticsMaximumParentPathLength :: !Int
    -- ^ The longest parent path found, in edges. Bounded by @floor(log2 vertexCount)@.
  , statisticsHistoryDepth :: !Integer
    -- ^ The number of link events from the history root to the validated version.
  }
  deriving (Eq, Show)

-- | An edgeless root version over vertices @0 .. count - 1@, or 'Nothing' for a negative universe.
-- @O(1)@.
--
-- Every vertex starts as its own component. Because a singleton is represented by the /absence/ of
-- a cell, construction costs the same for a universe of two vertices and for one of 'maxBound'.
create :: Int -> Maybe PersistentAncestralConnectionForest
create count
  | count < 0 = Nothing
  | otherwise =
      Just (PersistentAncestralConnectionForest count count HashMap.empty (HistoryRoot count))

-- | The fixed number of vertices in this forest. @O(1)@.
vertexCount :: PersistentAncestralConnectionForest -> Int
vertexCount (PersistentAncestralConnectionForest count _ _ _) = count

-- | The number of connected components in this version. @O(1)@.
componentCount :: PersistentAncestralConnectionForest -> Int
componentCount (PersistentAncestralConnectionForest _ components _ _) = components

-- | The identity token for this history version. @O(1)@.
version :: PersistentAncestralConnectionForest -> AncestralConnectionVersion
version (PersistentAncestralConnectionForest _ _ _ token) = token

-- | A version's parent, or 'Nothing' at the history root. @O(1)@.
versionParent :: AncestralConnectionVersion -> Maybe AncestralConnectionVersion
versionParent (HistoryRoot _) = Nothing
versionParent (HistoryLink parent _ _ _ _) = Just parent

-- | The number of link events from the history root to this version. @O(1)@.
versionDepth :: AncestralConnectionVersion -> Integer
versionDepth (HistoryRoot _) = 0
versionDepth (HistoryLink _ _ _ depth _) = depth

-- | The root identity of this version tree. @O(1)@; the root is cached on every version rather
-- than reached by walking the chain.
versionRoot :: AncestralConnectionVersion -> AncestralConnectionVersion
versionRoot token@(HistoryRoot _) = token
versionRoot (HistoryLink _ _ _ _ root) = root

-- | The current union-find representative of a vertex, or 'Nothing' outside the universe.
-- @O(w log n)@: worst-case in the @O(log n)@ path factor and expected in the CHAMP factor @w@.
--
-- The representative is selected by deterministic union-by-size history and is an implementation
-- artifact: sibling versions need not agree on it.
find :: Int -> PersistentAncestralConnectionForest -> Maybe Int
find vertex forest@(PersistentAncestralConnectionForest _ _ cells _)
  | inUniverse vertex forest = Just (findRoot vertex cells)
  | otherwise = Nothing

-- | Whether two vertices have the same current root, or 'Nothing' when either lies outside the
-- universe. @O(w log n)@: worst-case in the path factor, expected in @w@.
connected :: Int -> Int -> PersistentAncestralConnectionForest -> Maybe Bool
connected leftVertex rightVertex forest@(PersistentAncestralConnectionForest _ _ cells _)
  | inUniverse leftVertex forest && inUniverse rightVertex forest =
      Just (findRoot leftVertex cells == findRoot rightVertex cells)
  | otherwise = Nothing

-- | The number of vertices in a vertex's connected component, or 'Nothing' outside the universe.
-- @O(w log n)@: worst-case in the path factor, expected in @w@.
--
-- An isolated vertex is still a singleton root and reports @1@. The answer is the union-by-size
-- count cached at the component's current root, so the walk reads the structure without
-- compressing it; this forest deliberately has no path compression.
componentSize :: Int -> PersistentAncestralConnectionForest -> Maybe Int
componentSize vertex forest@(PersistentAncestralConnectionForest _ _ cells _)
  | inUniverse vertex forest = Just (sizeOf (findRoot vertex cells) cells)
  | otherwise = Nothing

-- | A child history version containing an undirected connection between two vertices, or
-- 'Nothing' when either endpoint lies outside the universe.
--
-- The returned version is always distinct from the receiver's. When the endpoints were already
-- connected its connectivity cells are shared unchanged, which 'sharesConnectivityRootWith'
-- confirms; otherwise exactly one union-by-size root edge is added and labelled with the child
-- version. On a size tie the first endpoint's root stays the representative, which makes the
-- result deterministic without changing the logarithmic-height proof. A self-link is a redundant
-- link.
--
-- @O(w log n)@ time and @O(w)@ new CHAMP nodes for a successful union; @O(w log n)@ time and
-- @O(1)@ space when redundant. The receiver is untouched, including when the endpoints are
-- rejected.
link :: Int
     -> Int
     -> PersistentAncestralConnectionForest
     -> Maybe PersistentAncestralConnectionForest
link leftVertex rightVertex forest@(PersistentAncestralConnectionForest count components cells token)
  | not (inUniverse leftVertex forest && inUniverse rightVertex forest) = Nothing
  | leftRoot == rightRoot =
      Just (PersistentAncestralConnectionForest count components cells childVersion)
  | otherwise =
      Just (PersistentAncestralConnectionForest count (components - 1) unitedCells childVersion)
  where
    leftRoot = findRoot leftVertex cells
    rightRoot = findRoot rightVertex cells
    childVersion =
      HistoryLink token leftVertex rightVertex (versionDepth token + 1) (versionRoot token)
    leftSize = sizeOf leftRoot cells
    rightSize = sizeOf rightRoot cells
    -- The combined size cannot overflow: component sizes partition the fixed vertex universe.
    (keptRoot, absorbedRoot, combinedSize)
      | leftSize < rightSize = (rightRoot, leftRoot, rightSize + leftSize)
      | otherwise = (leftRoot, rightRoot, leftSize + rightSize)
    unitedCells =
      HashMap.insert absorbedRoot (Cell keptRoot 0 (Just childVersion))
        (HashMap.insert keptRoot (Cell keptRoot combinedSize Nothing) cells)

-- | The earliest version on this version's root-to-current history in which two vertices were
-- connected.
--
-- 'Nothing' reports that an endpoint lies outside the fixed universe; @'Just' 'Nothing'@ reports
-- that the pair is disconnected in this version; @'Just' ('Just' token)@ is the witness. A vertex
-- is connected to itself at the history root.
--
-- The answer is computed from the two current parent paths as the latest union-edge version
-- strictly below their forest lowest common ancestor; the version history is never searched. On
-- equal candidate depths the earlier-encountered candidate is kept, which is immaterial because
-- union-edge versions on one lineage are unique per depth. @O(w log n)@ time -- worst-case in the
-- path factor, expected in @w@ -- and @O(log n)@ worst-case temporary path space, independent of
-- history depth.
--
-- The result is never a version on another branch, and a later merge of the pair's component into
-- a bigger one never replaces the pair's earlier witness.
firstConnected :: Int
               -> Int
               -> PersistentAncestralConnectionForest
               -> Maybe (Maybe AncestralConnectionVersion)
firstConnected leftVertex rightVertex forest@(PersistentAncestralConnectionForest _ _ cells token)
  | not (inUniverse leftVertex forest && inUniverse rightVertex forest) = Nothing
  | leftVertex == rightVertex = Just (Just (versionRoot token))
  | pathRootVertex leftPath /= pathRootVertex rightPath = Just Nothing
  | otherwise = Just (Just witness)
  where
    leftPath = buildPath leftVertex cells
    rightPath = buildPath rightVertex cells
    -- Delete the common rootward suffix. The remaining steps are precisely the two paths strictly
    -- below the forest LCA, and every one of those steps owns a successful-union tag.
    shared = commonSuffixLength leftPath rightPath
    belowAncestor =
      take (length leftPath - shared) leftPath ++ take (length rightPath - shared) rightPath
    witness =
      case List.foldl' later Nothing [tag | PathStep _ tag <- belowAncestor] of
        Just found -> found
        -- Distinct connected vertices have at least one edge strictly below their LCA.
        Nothing ->
          error
            "Durable7.Hamt.PersistentAncestralConnectionForest: \
            \a connected pair has no union-edge witness"

-- | Whether two versions retain the exact same sparse connectivity index, so neither can observe a
-- union recorded in the other. A representation test, not an equality test. @O(1)@.
--
-- Heap identity of an immutable value is not observable in pure code, so this is the one operation
-- in 'IO': it compares the two retained indexes by 'System.Mem.StableName', falling back to the
-- CHAMP root-node comparison that "Durable7.Hamt.HashMap" already offers. The observation is
-- stable, because a retained immutable index is never rebuilt in place.
sharesConnectivityRootWith :: PersistentAncestralConnectionForest
                           -> PersistentAncestralConnectionForest
                           -> IO Bool
sharesConnectivityRootWith (PersistentAncestralConnectionForest _ _ leftCells _)
                           (PersistentAncestralConnectionForest _ _ rightCells _) = do
  leftValue <- evaluate leftCells
  rightValue <- evaluate rightCells
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  pure (leftName `eqStableName` rightName || HashMap.sharesRootWith leftValue rightValue)

-- | Audits sparse-cell canonicality, current root sizes, the resulting union-by-size height
-- ceiling, union-tag ancestry and order, and the cached component count.
--
-- Returns the first violation's description, or independently recomputed statistics. The checks
-- and their messages follow the managed reference's order: counts, history-token chain, history
-- root, cell addressing, canonical root cells, child cells, union-tag order, height ceiling,
-- component count, and cached root sizes.
--
-- @O(H log H + m log n (w + log H))@ expected, where @H@ is the history depth and @m@ is the
-- explicit-cell count. The reference states @O(H + m w log n)@ because it tests lineage membership
-- with a reference-identity hash set; structural version identity replaces that with a depth-keyed
-- ordered-map lookup plus one version comparison, and that comparison is @O(1)@ for the retained
-- tokens ordinary operations store. The membership test runs once per parent-path /step/ rather
-- than once per stored cell, which is where the extra @log n@ factor over the reference comes from.
-- A defensive audit; ordinary operations maintain these invariants.
validateStructure :: PersistentAncestralConnectionForest
                  -> Either String AncestralConnectionForestStatistics
validateStructure forest@(PersistentAncestralConnectionForest count components cells token)
  | count < 0 || components < 0 || components > count = Left "Connection-forest counts are invalid."
  | otherwise = do
      lineage <- collectLineage token
      mapM_ (checkCell lineage) storedCells
      (sizes, longestPath) <- walkAllPaths lineage
      let absentSingletons = toInteger count - toInteger storedCount
      if absentSingletons < 0
          || absentSingletons + toInteger (Map.size sizes) /= toInteger components
        then Left "The cached component count disagrees with the parent forest."
        else do
          mapM_ checkRootSize (Map.toList sizes)
          Right AncestralConnectionForestStatistics
            { statisticsVertexCount = count
            , statisticsComponentCount = components
            , statisticsStoredCellCount = storedCount
            , statisticsMaximumParentPathLength = longestPath
            , statisticsHistoryDepth = versionDepth token
            }
  where
    storedCells = HashMap.toList cells
    storedCount = HashMap.size cells
    maximumHeight = if count <= 1 then 0 else integerLog2 count

    checkCell lineage (vertex, Cell parent cellSize joinedAt)
      | not (inUniverse vertex forest) || not (inUniverse parent forest) =
          Left "A sparse parent cell addresses a vertex outside the universe."
      | parent == vertex =
          if cellSize <= 1 || isJust joinedAt
            then Left "A stored root cell is not a canonical non-singleton root."
            else Right ()
      | cellSize /= 0
          || not (maybe False (onLineage lineage) joinedAt)
          || not (HashMap.member parent cells) =
          Left "A non-root cell has an invalid size or union-version tag."
      | otherwise = Right ()

    walkAllPaths lineage = go storedCells Map.empty 0
      where
        go [] !sizes !longest = Right (sizes, longest)
        go ((vertex, _) : rest) !sizes !longest = do
          (root, height) <- walkPath lineage vertex
          let !extended = Map.insertWith (+) root (1 :: Int) sizes
          go rest extended (max longest height)

    walkPath lineage start = go start 0 (-1)
      where
        go current !height !previousDepth =
          case cellOf current cells of
            Cell parent _ joinedAt
              | parent == current -> Right (current, height)
              | otherwise -> case joinedAt of
                  Nothing -> Left tagOrderViolation
                  Just tag
                    | versionDepth tag <= previousDepth || not (onLineage lineage tag) ->
                        Left tagOrderViolation
                    | height + 1 > maximumHeight ->
                        Left "A parent path exceeds the union-by-size height bound."
                    | otherwise -> go parent (height + 1) (versionDepth tag)

    tagOrderViolation = "Union-edge versions do not strictly increase toward the root."

    checkRootSize (root, expected)
      | sizeOf root cells == expected = Right ()
      | otherwise = Left "A root's cached union size is incorrect."

-- | Walks the history chain rootward, checking that depths decrease by exactly one and that every
-- token agrees on the history root, and returns the lineage keyed by depth. Tail-recursive: a
-- history can be hundreds of thousands of links deep.
collectLineage :: AncestralConnectionVersion
               -> Either String (Map.Map Integer AncestralConnectionVersion)
collectLineage tip = do
  lineage <- walk tip (versionDepth tip) Map.empty
  let historyRoot = versionRoot tip
  if Map.lookup 0 lineage /= Just historyRoot
      || isJust (versionParent historyRoot)
      || versionDepth historyRoot /= 0
    then Left "The history root is inconsistent."
    else Right lineage
  where
    expectedRoot = versionRoot tip
    walk current !expectedDepth !lineage
      | versionDepth current /= expectedDepth || versionRoot current /= expectedRoot =
          Left "The history-token parent chain is inconsistent."
      | otherwise =
          let !extended = Map.insert expectedDepth current lineage
           in case versionParent current of
                Just parent -> walk parent (expectedDepth - 1) extended
                Nothing
                  | expectedDepth /= 0 -> Left "The history root is inconsistent."
                  | otherwise -> Right extended

-- | Whether a union tag is one of the audited version's own ancestors. Versions on one lineage are
-- unique per depth, so this is a depth-keyed lookup plus one comparison.
onLineage :: Map.Map Integer AncestralConnectionVersion -> AncestralConnectionVersion -> Bool
onLineage lineage tag = Map.lookup (versionDepth tag) lineage == Just tag

-- | Structural version equality with a positive pointer-identity shortcut.
--
-- A positive pointer comparison is enough to prune an immutable chain without walking it; a
-- negative result is never used as a verdict. The walk itself is tail-recursive.
sameVersion :: AncestralConnectionVersion -> AncestralConnectionVersion -> Bool
sameVersion left right
  | samePointer left right = True
sameVersion (HistoryRoot leftUniverse) (HistoryRoot rightUniverse) = leftUniverse == rightUniverse
sameVersion (HistoryLink leftParent leftFirst leftSecond leftDepth _)
            (HistoryLink rightParent rightFirst rightSecond rightDepth _)
  | leftDepth /= rightDepth || leftFirst /= rightFirst || leftSecond /= rightSecond = False
  | otherwise = sameVersion leftParent rightParent
sameVersion _ _ = False

-- | Forces both operands to constructor form before asking GHC for positive heap identity, so an
-- indirection introduced by evaluation does not hide a genuine sharing.
samePointer :: a -> a -> Bool
samePointer left right = left `seq` right `seq` isTrue# (reallyUnsafePtrEquality# left right)
{-# INLINE samePointer #-}

-- | The current representative of a vertex, following parent cells to the root.
findRoot :: Int -> HashMap Int Cell -> Int
findRoot vertex cells =
  case cellOf vertex cells of
    Cell parent _ _
      | parent == vertex -> vertex
      | otherwise -> findRoot parent cells

-- | The parent path from a vertex to its root, each step carrying that step's union tag. The path
-- is never empty and its last step is the root.
buildPath :: Int -> HashMap Int Cell -> [PathStep]
buildPath vertex cells =
  case cellOf vertex cells of
    Cell parent _ joinedAt
      | parent == vertex -> [PathStep vertex joinedAt]
      | otherwise -> PathStep vertex joinedAt : buildPath parent cells

-- | The root vertex a parent path ends at.
pathRootVertex :: [PathStep] -> Int
pathRootVertex [PathStep vertex _] = vertex
pathRootVertex (_ : rest) = pathRootVertex rest
pathRootVertex [] =
  error "Durable7.Hamt.PersistentAncestralConnectionForest: an empty parent path"

-- | How many rootward steps two parent paths share.
commonSuffixLength :: [PathStep] -> [PathStep] -> Int
commonSuffixLength leftPath rightPath = go (reverse leftPath) (reverse rightPath) 0
  where
    go (PathStep leftVertex _ : leftRest) (PathStep rightVertex _ : rightRest) !matched
      | leftVertex == rightVertex = go leftRest rightRest (matched + 1)
    go _ _ !matched = matched

-- | The deeper of two candidate union tags, keeping the left one on equal depths.
later :: Maybe AncestralConnectionVersion
      -> Maybe AncestralConnectionVersion
      -> Maybe AncestralConnectionVersion
later current Nothing = current
later Nothing candidate = candidate
later current@(Just currentVersion) candidate@(Just candidateVersion)
  | versionDepth candidateVersion > versionDepth currentVersion = candidate
  | otherwise = current

-- | Reads a vertex's cell; an absent cell is canonically its own singleton root.
cellOf :: Int -> HashMap Int Cell -> Cell
cellOf vertex cells =
  case HashMap.lookup vertex cells of
    Just cell -> cell
    Nothing -> Cell vertex 1 Nothing

-- | Reads the component size cached at a root; an absent cell is a singleton.
sizeOf :: Int -> HashMap Int Cell -> Int
sizeOf vertex cells =
  case HashMap.lookup vertex cells of
    Just (Cell _ storedSize _) -> storedSize
    Nothing -> 1

-- | Whether a vertex lies in the forest's fixed universe.
inUniverse :: Int -> PersistentAncestralConnectionForest -> Bool
inUniverse vertex (PersistentAncestralConnectionForest count _ _ _) =
  vertex >= 0 && vertex < count

-- | @floor(log2 value)@ for a positive value.
integerLog2 :: Int -> Int
integerLog2 value = go 0 value
  where
    go !result current
      | current <= 1 = result
      | otherwise = go (result + 1) (current `div` 2)
