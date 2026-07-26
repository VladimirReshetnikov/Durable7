-- | A text rope over characters, with newline counts cached in its measure.
--
-- That cache is what makes converting between a character offset and a line/column position
-- logarithmic rather than a scan of the text.
module Durable7.FingerTree.Rope.Text
  ( NewlineMeasure(..)
  , TextRope
  , TextRopeCursor
  , TextRopeCursorSearch
  , fromString
  , fromText
  , toString
  , toText
  , lineCount
  , lineOfOffset
  , lineStartOffset
  , lineColumnOf
  , offsetOf
  , getLine
  , lines
  , cursor
  , cursorAt
  , cursorByMeasure
  , searchFound
  , searchCursor
  , cursorCount
  , cursorNull
  , cursorPosition
  , isAtStart
  , isAtEnd
  , measureBefore
  , measureAfter
  , peekPrevious
  , peekNext
  , movePrevious
  , moveNext
  , seek
  , seekByMeasure
  , lineColumnOfCursor
  , insert
  , insertRange
  , deletePrevious
  , deleteNext
  , replaceNext
  , snapshot
  ) where

import Prelude hiding (getLine, lines)

import qualified Data.Text as Text
import qualified Durable7.FingerTree.MeasuredRope as MeasuredRope

-- | Counts line feeds, which is what gives a text rope logarithmic offset-to-line conversion.
newtype NewlineMeasure = NewlineMeasure { getNewlineCount :: Int }
  deriving (Eq, Ord, Read, Show)

instance Semigroup NewlineMeasure where
  NewlineMeasure left <> NewlineMeasure right = NewlineMeasure (left + right)

instance Monoid NewlineMeasure where
  mempty = NewlineMeasure 0

-- | A rope over characters with newline counts cached in its measure, so converting between an
-- offset and a line/column is logarithmic rather than a scan.
type TextRope = MeasuredRope.MeasuredRope NewlineMeasure Char

-- | The measured snapshot-plus-gap cursor specialized to newline measures.
-- 'TextRope' is a true type alias, so snapshots retain the full text surface
-- without materialization or an additional facade wrapper.
type TextRopeCursor = MeasuredRope.MeasuredRopeCursor NewlineMeasure Char

-- | Where a text rope cursor search landed.
type TextRopeCursorSearch = MeasuredRope.MeasuredRopeCursorSearch NewlineMeasure Char

-- | A text rope holding the given `String`.
fromString :: String -> TextRope
fromString = MeasuredRope.fromListWith measureChar

-- | A text rope holding the given `Text`.
fromText :: Text.Text -> TextRope
fromText = fromString . Text.unpack

-- | The text as a `String`.
toString :: TextRope -> String
toString = MeasuredRope.toList

-- | The text as a `Text`.
toText :: TextRope -> Text.Text
toText = Text.pack . toString

-- | How many lines the text holds. Newline counts are cached in the measure, so this is a cached
-- read.
lineCount :: TextRope -> Int
lineCount rope
  | totalNewlines == maxBound =
      error "Durable7.FingerTree.Rope.Text: line count overflow"
  | otherwise = totalNewlines + 1
  where
    totalNewlines = newlineCount rope

-- | Which line the character offset falls on.
lineOfOffset :: Int -> TextRope -> Maybe Int
lineOfOffset offset rope
  | offset < 0 || offset > MeasuredRope.count rope = Nothing
  | otherwise = getNewlineCount <$> MeasuredRope.prefixMeasure offset rope

-- | The character offset where the given line begins.
lineStartOffset :: Int -> TextRope -> Maybe Int
lineStartOffset line rope
  | line < 0 || line > newlineCount rope = Nothing
  | line == 0 = Just 0
  | otherwise = do
      (newlineOffset, _, _) <- MeasuredRope.locateByMeasure
        (\(NewlineMeasure seen) -> seen >= line)
        rope
      pure (newlineOffset + 1)

-- | The line and column of a character offset.
lineColumnOf :: Int -> TextRope -> Maybe (Int, Int)
lineColumnOf offset rope = do
  line <- lineOfOffset offset rope
  start <- lineStartOffset line rope
  pure (line, offset - start)

-- | The character offset of a line and column.
offsetOf :: Int -> Int -> TextRope -> Maybe Int
offsetOf line column rope
  | column < 0 = Nothing
  | otherwise = do
      start <- lineStartOffset line rope
      end <- lineEndOffset line rope
      if column <= end - start
        then Just (start + column)
        else Nothing

-- | The text of the given line.
getLine :: Int -> TextRope -> Maybe String
getLine line rope
  | line < 0 || line > newlineCount rope = Nothing
  | otherwise = do
      start <- lineStartOffset line rope
      end <- lineEndOffset line rope
      segment <- MeasuredRope.slice start (end - start) rope
      pure (MeasuredRope.toList segment)

-- | The text's lines.
lines :: TextRope -> [String]
lines rope = map lineAt [0 .. lineCount rope - 1]
  where
    lineAt line =
      case getLine line rope of
        Just value -> value
        Nothing -> error "Durable7.FingerTree.Rope.Text.lines: inconsistent line measure"

-- | A cursor before the first character.
cursor :: TextRope -> TextRopeCursor
cursor = MeasuredRope.cursor

-- | The character at the given rank.
cursorAt :: Int -> TextRope -> Maybe TextRopeCursor
cursorAt = MeasuredRope.cursorAt

-- | A cursor at the first gap where the measure satisfies the predicate.
cursorByMeasure :: (NewlineMeasure -> Bool) -> TextRope -> TextRopeCursorSearch
cursorByMeasure = MeasuredRope.cursorByMeasure

-- | Whether the search found an exact match.
searchFound :: TextRopeCursorSearch -> Bool
searchFound = MeasuredRope.searchFound

-- | The cursor the search landed on.
searchCursor :: TextRopeCursorSearch -> TextRopeCursor
searchCursor = MeasuredRope.searchCursor

-- | Number of characters in the rope version the cursor is positioned in.
cursorCount :: TextRopeCursor -> Int
cursorCount = MeasuredRope.cursorCount

-- | Whether the rope version the cursor is positioned in holds no characters.
cursorNull :: TextRopeCursor -> Bool
cursorNull = MeasuredRope.cursorNull

-- | The cursor's gap position.
cursorPosition :: TextRopeCursor -> Int
cursorPosition = MeasuredRope.cursorPosition

-- | Whether the gap precedes the first character.
isAtStart :: TextRopeCursor -> Bool
isAtStart = MeasuredRope.isAtStart

-- | Whether the gap follows the last character.
isAtEnd :: TextRopeCursor -> Bool
isAtEnd = MeasuredRope.isAtEnd

-- | The combined measure of everything before the gap.
measureBefore :: TextRopeCursor -> NewlineMeasure
measureBefore = MeasuredRope.measureBefore

-- | The combined measure of everything after the gap.
measureAfter :: TextRopeCursor -> NewlineMeasure
measureAfter = MeasuredRope.measureAfter

-- | The character immediately before the gap, or `Nothing` at the start.
peekPrevious :: TextRopeCursor -> Maybe Char
peekPrevious = MeasuredRope.peekPrevious

-- | The character immediately after the gap, or `Nothing` at the end.
peekNext :: TextRopeCursor -> Maybe Char
peekNext = MeasuredRope.peekNext

-- | A cursor one position earlier. The receiver is unchanged.
movePrevious :: TextRopeCursor -> Maybe TextRopeCursor
movePrevious = MeasuredRope.movePrevious

-- | A cursor one position later. The receiver is unchanged.
moveNext :: TextRopeCursor -> Maybe TextRopeCursor
moveNext = MeasuredRope.moveNext

-- | A cursor at the given position within the same rope version.
seek :: Int -> TextRopeCursor -> Maybe TextRopeCursor
seek = MeasuredRope.seek

-- | A cursor at the first gap where the measure satisfies the predicate.
seekByMeasure :: (NewlineMeasure -> Bool) -> TextRopeCursor -> TextRopeCursorSearch
seekByMeasure = MeasuredRope.seekByMeasure

-- | Returns the zero-based line and 'Char'-element column at the cursor gap.
lineColumnOfCursor :: TextRopeCursor -> (Int, Int)
lineColumnOfCursor cursorValue =
  case lineColumnOf (cursorPosition cursorValue) (snapshot cursorValue) of
    Just value -> value
    Nothing -> error "Durable7.FingerTree.Rope.Text.lineColumnOfCursor: invalid cursor invariant"

-- | A rope containing the given character.
insert :: Char -> TextRopeCursor -> TextRopeCursor
insert = MeasuredRope.insert

-- | A rope with a range's characters inserted at the position.
insertRange :: String -> TextRopeCursor -> TextRopeCursor
insertRange = MeasuredRope.insertRange

-- | Removes the character before the gap, producing a new version the returned cursor is positioned
-- in.
deletePrevious :: TextRopeCursor -> Maybe TextRopeCursor
deletePrevious = MeasuredRope.deletePrevious

-- | Removes the character after the gap, producing a new version the returned cursor is positioned
-- in.
deleteNext :: TextRopeCursor -> Maybe TextRopeCursor
deleteNext = MeasuredRope.deleteNext

-- | Replaces the character after the gap, producing a new version the returned cursor is positioned
-- in.
replaceNext :: Char -> TextRopeCursor -> Maybe TextRopeCursor
replaceNext = MeasuredRope.replaceNext

-- | The rope version this cursor is positioned in.
snapshot :: TextRopeCursor -> TextRope
snapshot = MeasuredRope.snapshot

measureChar :: Char -> NewlineMeasure
measureChar '\n' = NewlineMeasure 1
measureChar _ = NewlineMeasure 0

newlineCount :: TextRope -> Int
newlineCount rope =
  case MeasuredRope.measure rope of
    NewlineMeasure value -> value

lineEndOffset :: Int -> TextRope -> Maybe Int
lineEndOffset line rope
  | line < 0 || line > totalNewlines = Nothing
  | line < totalNewlines = subtract 1 <$> lineStartOffset (line + 1) rope
  | otherwise = Just (MeasuredRope.count rope)
  where
    totalNewlines = newlineCount rope
