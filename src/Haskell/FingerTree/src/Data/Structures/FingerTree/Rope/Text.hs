module Data.Structures.FingerTree.Rope.Text
  ( NewlineMeasure(..)
  , TextRope
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
  ) where

import Prelude hiding (getLine, lines)

import qualified Data.Text as Text
import qualified Data.Structures.FingerTree.MeasuredRope as MeasuredRope

newtype NewlineMeasure = NewlineMeasure { getNewlineCount :: Int }
  deriving (Eq, Ord, Read, Show)

instance Semigroup NewlineMeasure where
  NewlineMeasure left <> NewlineMeasure right = NewlineMeasure (left + right)

instance Monoid NewlineMeasure where
  mempty = NewlineMeasure 0

type TextRope = MeasuredRope.MeasuredRope NewlineMeasure Char

fromString :: String -> TextRope
fromString = MeasuredRope.fromListWith measureChar

fromText :: Text.Text -> TextRope
fromText = fromString . Text.unpack

toString :: TextRope -> String
toString = MeasuredRope.toList

toText :: TextRope -> Text.Text
toText = Text.pack . toString

lineCount :: TextRope -> Int
lineCount rope =
  case MeasuredRope.measure rope of
    NewlineMeasure newlineCount -> newlineCount + 1

lineOfOffset :: Int -> TextRope -> Maybe Int
lineOfOffset offset rope
  | offset < 0 || offset > MeasuredRope.count rope = Nothing
  | otherwise = getNewlineCount <$> MeasuredRope.prefixMeasure offset rope

lineStartOffset :: Int -> TextRope -> Maybe Int
lineStartOffset line rope
  | line < 0 || line >= lineCount rope = Nothing
  | line == 0 = Just 0
  | otherwise = do
      (newlineOffset, _, _) <- MeasuredRope.locateByMeasure
        (\(NewlineMeasure seen) -> seen >= line)
        rope
      pure (newlineOffset + 1)

lineColumnOf :: Int -> TextRope -> Maybe (Int, Int)
lineColumnOf offset rope = do
  line <- lineOfOffset offset rope
  start <- lineStartOffset line rope
  pure (line, offset - start)

offsetOf :: Int -> Int -> TextRope -> Maybe Int
offsetOf line column rope
  | column < 0 = Nothing
  | otherwise = do
      start <- lineStartOffset line rope
      end <- lineEndOffset line rope
      if column <= end - start
        then Just (start + column)
        else Nothing

getLine :: Int -> TextRope -> Maybe String
getLine line rope
  | line < 0 || line >= lineCount rope = Nothing
  | otherwise = do
      start <- lineStartOffset line rope
      end <- lineEndOffset line rope
      segment <- MeasuredRope.slice start (end - start) rope
      pure (MeasuredRope.toList segment)

lines :: TextRope -> [String]
lines rope = map lineAt [0 .. lineCount rope - 1]
  where
    lineAt line =
      case getLine line rope of
        Just value -> value
        Nothing -> error "Data.Structures.FingerTree.Rope.Text.lines: inconsistent line measure"

measureChar :: Char -> NewlineMeasure
measureChar '\n' = NewlineMeasure 1
measureChar _ = NewlineMeasure 0

lineEndOffset :: Int -> TextRope -> Maybe Int
lineEndOffset line rope
  | line < 0 || line >= lineCount rope = Nothing
  | line + 1 < lineCount rope = subtract 1 <$> lineStartOffset (line + 1) rope
  | otherwise = Just (MeasuredRope.count rope)
