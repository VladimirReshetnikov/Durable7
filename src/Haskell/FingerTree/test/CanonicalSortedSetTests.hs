{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE NumericUnderscores #-}
{-# OPTIONS_GHC -fno-cse -fno-full-laziness #-}

-- | Tests for the canonical sorted set.
--
-- The central property is history independence: sets reaching the same contents by different
-- sequences of edits must be structurally identical.
module CanonicalSortedSetTests (run) where

import Control.Monad (foldM, forM_, when)
import Data.Bits (testBit, xor)
import qualified Data.ByteString as ByteString
import Data.Char (ord, toLower)
import qualified Data.List as List
import Data.IORef (IORef, newIORef, readIORef, writeIORef)
import qualified Data.Set as Set
import Data.Word (Word32, Word64)
import System.IO.Unsafe (unsafePerformIO)

import qualified Durable7.FingerTree.CanonicalSortedSet as Canonical

-- | Runs this module's test cases, reporting each result.
run :: IO ()
run = do
  testRankVectorsAndPolicyModes
  testCanonicalConstruction
  testRepresentativesAndCoherence
  testRandomizedHistory
  testCollidingRanks
  testAlgebraAndRelations
  testReceiverDefinedEquality
  testStructuralSharing
  testValidatorDetectsPolicyCorruption

testRankVectorsAndPolicyModes :: IO ()
testRankVectorsAndPolicyModes = do
  keyedResult <- Canonical.newKeyedPolicy
    (const 0x0102_0304_0506_0708)
    (ByteString.pack [0 .. 31])
  keyed <- mustRight "32-byte keyed policy" keyedResult
  assertEqual
    "keyed HMAC rank vector"
    (Canonical.ZipTreeRank 1 0x1975_2063_8af1_f7a6 0xf31e_be0a_b983_ff0f)
    (Canonical.rankFor keyed (0 :: Int))

  let retainedKey = ByteString.pack [fromIntegral (index * 7 + 3) | index <- [0 :: Int .. 31]]
      keyedValues = [value | value <- [-1_000 :: Int .. 1_000], value `mod` 3 /= 0]
  firstKeyedResult <- Canonical.newKeyedPolicy intHash retainedKey
  secondKeyedResult <- Canonical.newKeyedPolicy intHash (ByteString.copy retainedKey)
  firstKeyed <- mustRight "first reproducible keyed policy" firstKeyedResult
  secondKeyed <- mustRight "second reproducible keyed policy" secondKeyedResult
  firstKeyedSet <- build firstKeyed keyedValues
  secondKeyedSet <- build secondKeyed (List.reverse keyedValues)
  assertEqual
    "caller key reproduces topology"
    (Canonical.shapeForTesting firstKeyedSet)
    (Canonical.shapeForTesting secondKeyedSet)
  assertEqual
    "caller key reproduces digest"
    (Canonical.contentHash firstKeyedSet)
    (Canonical.contentHash secondKeyedSet)
  assertBool "caller-keyed policies compare semantically" (Canonical.setEquals firstKeyedSet secondKeyedSet)
  assertLeft
    "separately allocated keyed policy identity gates algebra"
    Canonical.IncompatibleRankPolicy
    (Canonical.union firstKeyedSet secondKeyedSet)

  shortResult <- Canonical.newKeyedPolicy intHash (ByteString.replicate 31 0)
  case shortResult of
    Left (Canonical.RankKeyTooShort 31) -> pure ()
    Left failure -> fail ("short keyed policy: unexpected failure " ++ show failure)
    Right _ -> fail "short keyed policy was accepted"

  seeded <- Canonical.newSeededPolicy
    (const 0xfedc_ba98_7654_3210)
    0x0123_4567_89ab_cdef
  -- Independent sibling-port vector.  The policy key is
  -- SHA-256("ZZT2" || 0123456789abcdef), and the source hash is big endian.
  assertEqual
    "public-seed HMAC rank vector"
    (Canonical.ZipTreeRank 0 0x9efd_aef0_3f68_c6bd 0x4da7_5484_837a_7798)
    (Canonical.rankFor seeded (0 :: Int))
  assertEqual
    "public seed retained"
    (Just 0x0123_4567_89ab_cdef)
    (Canonical.policySeed seeded)

  firstRandom <- Canonical.newRandomPolicy intHash
  secondRandom <- Canonical.newRandomPolicy intHash
  assertEqual "fresh policy hides seed" Nothing (Canonical.policySeed firstRandom)
  assertEqual "second fresh policy hides seed" Nothing (Canonical.policySeed secondRandom)
  -- The only probabilistic assertion in this suite: independent hidden HMAC
  -- keys collide on the retained secondary/content pair with probability at
  -- most 2^-128.
  assertBool
    "fresh hidden keys separate ranks"
    (Canonical.rankFor firstRandom 0x5eed /= Canonical.rankFor secondRandom 0x5eed)

  priorityPolicy <- Canonical.newSeededPolicy intHash 0x77_1122
  let ranked = [(value, Canonical.rankFor priorityPolicy value) | value <- [0 :: Int .. 511]]
      pairs =
        [ (high, low)
        | high@(_, highRank) <- ranked
        , low@(_, lowRank) <- ranked
        , Canonical.geometricRank highRank == Canonical.geometricRank lowRank
        , testBit (Canonical.secondaryRank highRank) 63
        , not (testBit (Canonical.secondaryRank lowRank) 63)
        ]
  (high, low) <- case pairs of
    pair : _ -> pure pair
    [] -> fail "seeded vector did not contain an opposite-sign secondary pair"
  unsignedTree <- mustRight
    "unsigned secondary tree"
    (Canonical.fromList priorityPolicy [fst low, fst high])
  case Canonical.shapeForTesting unsignedTree of
    root : _ -> assertEqual "Word64 secondary ordering is unsigned" (fst high) (Canonical.shapeItem root)
    [] -> fail "two-item unsigned-priority set was empty"

testCanonicalConstruction :: IO ()
testCanonicalConstruction = do
  policy <- Canonical.newSeededPolicy intHash 0x5eed_cafe
  let values = [value | value <- [-256 :: Int .. 256], value `mod` 5 /= 0]
      histories =
        [ values
        , List.reverse values
        , List.filter odd values ++ List.filter even values
        , deterministicShuffle 0x9e37_79b9 values
        ]
  baseline <- mustRight "canonical baseline" (Canonical.fromList policy values)
  forM_ histories $ \history -> do
    bulk <- mustRight "canonical bulk history" (Canonical.fromList policy history)
    incremental <- foldM insertOrFail (Canonical.empty policy) history
    assertEqual "bulk canonical topology" (Canonical.shapeForTesting baseline) (Canonical.shapeForTesting bulk)
    assertEqual
      "incremental canonical topology"
      (Canonical.shapeForTesting baseline)
      (Canonical.shapeForTesting incremental)
    assertEqual "bulk canonical digest" (Canonical.contentHash baseline) (Canonical.contentHash bulk)
    assertEqual "incremental canonical digest" (Canonical.contentHash baseline) (Canonical.contentHash incremental)
    assertEqual "canonical sorted contents" values (Canonical.toAscList bulk)
    _ <- assertValid "canonical bulk validates" bulk
    _ <- assertValid "canonical incremental validates" incremental
    pure ()

  reproducedPolicy <- Canonical.newSeededPolicy intHash 0x5eed_cafe
  reproduced <- mustRight
    "separate same-seed policy"
    (Canonical.fromList reproducedPolicy (List.reverse values))
  assertEqual
    "same seed reproduces topology"
    (Canonical.shapeForTesting baseline)
    (Canonical.shapeForTesting reproduced)
  assertEqual "same seed reproduces digest" (Canonical.contentHash baseline) (Canonical.contentHash reproduced)
  assertBool "same seed has semantic equality" (Canonical.setEquals baseline reproduced)
  assertLeft
    "separately allocated policy identity gates algebra"
    Canonical.IncompatibleRankPolicy
    (Canonical.union baseline reproduced)

testRepresentativesAndCoherence :: IO ()
testRepresentativesAndCoherence = do
  coherent <- Canonical.newSeededPolicyBy compareKeyed keyedHash 0x2a11_ce
  let firstAlpha = Keyed 1 "first-alpha"
      laterAlpha = Keyed 1 "later-alpha"
      firstZulu = Keyed 2 "first-zulu"
  bulk <- mustRight
    "representative bulk"
    (Canonical.fromList coherent [firstZulu, firstAlpha, laterAlpha, Keyed 2 "later-zulu"])
  assertEqual "bulk equivalence-class count" 2 (Canonical.count bulk)
  assertEqual "bulk keeps first alpha" (Just firstAlpha) (Canonical.lookupEquivalent laterAlpha bulk)
  assertEqual "bulk keeps first zulu" (Just firstZulu) (Canonical.lookupEquivalent (Keyed 2 "probe") bulk)

  duplicate <- mustRight "duplicate insert" (Canonical.insert laterAlpha bulk)
  Canonical.sharesRootWith bulk duplicate >>= assertBool "duplicate insert reuses root"
  assertEqual
    "duplicate insert keeps representative"
    (Just firstAlpha)
    (Canonical.lookupEquivalent laterAlpha duplicate)
  let absentDelete = Canonical.delete (Keyed 99 "absent") bulk
  Canonical.sharesRootWith bulk absentDelete >>= assertBool "absent delete reuses root"

  incoherent <- Canonical.newSeededPolicyBy compareKeyed incoherentKeyedHash 17
  singletonSet <- mustRight "incoherent singleton" (Canonical.insert firstAlpha (Canonical.empty incoherent))
  assertLeft
    "incremental incoherent rank hash"
    Canonical.InconsistentRankHash
    (Canonical.insert laterAlpha singletonSet)
  assertLeft
    "bulk incoherent rank hash"
    Canonical.InconsistentRankHash
    (Canonical.fromList incoherent [firstAlpha, laterAlpha])

testRandomizedHistory :: IO ()
testRandomizedHistory = do
  policy <- Canonical.newSeededPolicy intHash 0x3141_5926
  walk policy 0 0x1234_5678 (Canonical.empty policy) Set.empty []
  where
    walk !policy !step !random !actual !model !snapshots
      | step == 10_000 = do
          assertEqual "randomized final model" (Set.toAscList model) (Canonical.toAscList actual)
          forM_ snapshots $ \(snapshot, expected) ->
            assertEqual "retained canonical snapshot" expected (Canonical.toAscList snapshot)
      | otherwise = do
          let !next = nextRandom random
              !value = fromIntegral (next `mod` 1_024)
              !operation = next `mod` 5
          updated <- case operation of
            0 -> insertOrFail actual value
            1 -> pure (Canonical.delete value actual)
            2 -> do
              assertEqual "randomized contains model" (Set.member value model) (Canonical.contains value actual)
              pure actual
            3 -> insertOrFail actual (step `mod` 1_024)
            _ -> pure (Canonical.delete (step `mod` 1_024) actual)
          let !expected = case operation of
                0 -> Set.insert value model
                1 -> Set.delete value model
                2 -> model
                3 -> Set.insert (step `mod` 1_024) model
                _ -> Set.delete (step `mod` 1_024) model
              !retained =
                if step `mod` 701 == 0
                  then (updated, Set.toAscList expected) : snapshots
                  else snapshots
          when (step `mod` 257 == 0) $ do
            assertEqual "randomized intermediate model" (Set.toAscList expected) (Canonical.toAscList updated)
            _ <- assertValid "randomized intermediate structure" updated
            pure ()
          walk policy (step + 1) next updated expected retained

testCollidingRanks :: IO ()
testCollidingRanks = do
  let valueCount = 4_096
  policy <- Canonical.newSeededPolicy (const 0) 1
  forward <- mustRight "colliding forward" (Canonical.fromList policy [0 :: Int .. valueCount - 1])
  reverseSet <- mustRight "colliding reverse" (Canonical.fromList policy [valueCount - 1, valueCount - 2 .. 0])
  assertEqual "fully colliding height" valueCount (Canonical.height forward)
  assertEqual
    "colliding construction-order topology"
    (Canonical.shapeForTesting forward)
    (Canonical.shapeForTesting reverseSet)
  assertEqual "colliding construction-order digest" (Canonical.contentHash forward) (Canonical.contentHash reverseSet)
  statistics <- assertValid "fully colliding validation" forward
  assertEqual
    "fully colliding priority count"
    (valueCount - 1)
    (Canonical.statisticsPriorityCollisionCount statistics)

  let withoutLast = Canonical.delete (valueCount - 1) forward
  assertEqual "deep delete is stack safe" (valueCount - 1) (Canonical.height withoutLast)
  restored <- insertOrFail withoutLast (valueCount - 1)
  assertEqual "deep reinsert restores topology" (Canonical.shapeForTesting forward) (Canonical.shapeForTesting restored)
  assertEqual "deep reinsert restores digest" (Canonical.contentHash forward) (Canonical.contentHash restored)
  assertEqual "deep iteration is stack safe" [0 .. valueCount - 1] (Canonical.toAscList restored)
  _ <- assertValid "deep restored validation" restored
  pure ()

testAlgebraAndRelations :: IO ()
testAlgebraAndRelations = do
  policy <- Canonical.newSeededPolicy intHash 99
  left <- build policy [1, 2, 4, 8]
  right <- build policy [2, 3, 4, 5]
  united <- mustRight "canonical union" (Canonical.union left right)
  intersected <- mustRight "canonical intersection" (Canonical.intersection left right)
  excepted <- mustRight "canonical difference" (Canonical.difference left right)
  assertEqual "canonical union contents" [1, 2, 3, 4, 5, 8] (Canonical.toAscList united)
  assertEqual "canonical intersection contents" [2, 4] (Canonical.toAscList intersected)
  assertEqual "canonical difference contents" [1, 8] (Canonical.toAscList excepted)
  assertBool "subset relation" (Canonical.isSubsetOf intersected left)
  assertBool "proper subset relation" (Canonical.isProperSubsetOf intersected left)
  assertBool "superset relation" (Canonical.isSupersetOf united right)
  assertBool "proper superset relation" (Canonical.isProperSupersetOf united right)
  assertBool "overlap relation" (Canonical.overlaps left right)
  disjoint <- build policy [11, 12]
  assertBool "disjoint relation" (not (Canonical.overlaps left disjoint))
  assertBool "duplicate list equality" (Canonical.setEqualsList left [8, 4, 2, 1, 1, 2])
  assertEqual "clear retains policy and empties" [] (Canonical.toAscList (Canonical.clear left))
  forM_ [united, intersected, excepted] (assertValid "algebra result validates")

testReceiverDefinedEquality :: IO ()
testReceiverDefinedEquality = do
  insensitivePolicy <- Canonical.newSeededPolicyBy caseInsensitiveCompare caseInsensitiveHash 23
  sensitivePolicy <- Canonical.newSeededPolicyBy compare stringHash 23
  insensitive <- build insensitivePolicy ["alpha"]
  sensitive <- build sensitivePolicy ["alpha", "ALPHA"]
  assertBool
    "case-insensitive receiver deduplicates sensitive operand"
    (Canonical.setEquals insensitive sensitive)
  assertBool
    "case-sensitive receiver observes both representatives"
    (not (Canonical.setEquals sensitive insensitive))
  assertBool "receiver-policy subset" (Canonical.isSubsetOf insensitive sensitive)
  assertBool "reverse receiver-policy proper superset" (Canonical.isProperSupersetOf sensitive insensitive)

testStructuralSharing :: IO ()
testStructuralSharing = do
  policy <- Canonical.newSeededPolicy intHash 0xbadc_0ffe
  original <- build policy [0 :: Int .. 1_999]
  added <- insertOrFail original 2_000
  addShared <- Canonical.sharedNodeCount original added
  assertBool "insert retains at least 90 percent of old nodes" (addShared >= 1_800)
  let removed = Canonical.delete 1_000 original
  removeShared <- Canonical.sharedNodeCount original removed
  assertBool "delete retains at least 90 percent of unaffected nodes" (removeShared >= 1_800)
  independent <- build policy [1_999, 1_998 .. 0]
  independentShared <- Canonical.sharedNodeCount original independent
  assertEqual "independent canonical rebuild shares no nodes" 0 independentShared
  assertEqual
    "independent canonical rebuild has same topology"
    (Canonical.shapeForTesting original)
    (Canonical.shapeForTesting independent)

testValidatorDetectsPolicyCorruption :: IO ()
testValidatorDetectsPolicyCorruption = do
  salt <- newIORef 0
  policy <- Canonical.newSeededPolicy (mutableIntHash salt) 0x4444
  set <- build policy [0 :: Int .. 127]
  _ <- assertValid "mutable policy starts coherent" set
  writeIORef salt 1
  assertEqual
    "validator detects non-reproducible rank"
    (Left Canonical.NonReproducibleRank)
    (Canonical.validateStructure set)

data Keyed = Keyed !Int String
  deriving (Show)

instance Eq Keyed where
  Keyed leftKey leftLabel == Keyed rightKey rightLabel =
    leftKey == rightKey && leftLabel == rightLabel

compareKeyed :: Keyed -> Keyed -> Ordering
compareKeyed (Keyed left _) (Keyed right _) = compare left right

keyedHash :: Keyed -> Word64
keyedHash (Keyed key _) = intHash key

incoherentKeyedHash :: Keyed -> Word64
incoherentKeyedHash (Keyed key label) = intHash key `xor` stringHash label

intHash :: Int -> Word64
intHash value = fromIntegral (fromIntegral value :: Word32)

stringHash :: String -> Word64
stringHash = List.foldl' step 0xcbf2_9ce4_8422_2325
  where
    step hashValue character = (hashValue `xor` fromIntegral (ord character)) * 0x100_0000_01b3

caseInsensitiveHash :: String -> Word64
caseInsensitiveHash = stringHash . map toLower

caseInsensitiveCompare :: String -> String -> Ordering
caseInsensitiveCompare left right = compare (map toLower left) (map toLower right)

{-# NOINLINE mutableIntHash #-}
mutableIntHash :: IORef Word64 -> Int -> Word64
mutableIntHash salt value = unsafePerformIO $ do
  current <- readIORef salt
  pure (intHash value `xor` current)

build
  :: Canonical.ZipTreeRankPolicy a
  -> [a]
  -> IO (Canonical.CanonicalSortedSet a)
build policy values = mustRight "canonical build" (Canonical.fromList policy values)

insertOrFail
  :: Canonical.CanonicalSortedSet a
  -> a
  -> IO (Canonical.CanonicalSortedSet a)
insertOrFail set value = mustRight "canonical insert" (Canonical.insert value set)

assertValid
  :: String
  -> Canonical.CanonicalSortedSet a
  -> IO Canonical.CanonicalSortedSetStatistics
assertValid label set =
  case Canonical.validateStructure set of
    Right statistics -> pure statistics
    Left failure -> fail (label ++ ": " ++ show failure)

mustRight :: Show error => String -> Either error value -> IO value
mustRight _ (Right value) = pure value
mustRight label (Left failure) = fail (label ++ ": " ++ show failure)

nextRandom :: Word64 -> Word64
nextRandom value = value * 6_364_136_223_846_793_005 + 1_442_695_040_888_963_407

deterministicShuffle :: Word64 -> [a] -> [a]
deterministicShuffle seed values = go seed values []
  where
    go !_ [] result = result
    go !random remaining result =
      let !next = nextRandom random
          !position = fromIntegral (next `mod` fromIntegral (length remaining))
      in case List.splitAt position remaining of
           (prefix, selected : suffix) -> go next (prefix ++ suffix) (selected : result)
           _ -> error "deterministicShuffle: bounded index was out of range"

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")

assertLeft :: (Eq error, Show error) => String -> error -> Either error value -> IO ()
assertLeft label expected result =
  case result of
    Left actual -> assertEqual label expected actual
    Right _ -> fail (label ++ ": expected Left " ++ show expected ++ ", got Right")
