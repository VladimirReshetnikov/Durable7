module Data.Structures.FingerTree.ReversibleDeque
  ( ReversibleDeque
  , Cursor
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , cons
  , snoc
  , append
  , reverse
  , viewL
  , viewR
  , first
  , last
  , index
  , cursor
  , cursorAt
  , cursorPosition
  , cursorCount
  , cursorIsAtStart
  , cursorIsAtEnd
  , cursorPeekPrevious
  , cursorPeekNext
  , cursorMovePrevious
  , cursorMoveNext
  , cursorSeek
  , cursorInsert
  , cursorInsertList
  , cursorDeletePrevious
  , cursorDeleteNext
  , cursorReplaceNext
  , cursorReverse
  , cursorSnapshot
  ) where

import Prelude hiding (last, null, reverse)

import qualified Data.List as List

newtype ReversibleDeque a = ReversibleDeque (Tree a)
  deriving (Show)

-- | Immutable logical-order gap cursor over a reversible deque snapshot.
data Cursor a = Cursor !(ReversibleDeque a) !Int
  deriving (Show)

-- Extensional equality: orientation flags and tree shape are implementation
-- details, so equality compares the logical element sequences.
instance Eq a => Eq (ReversibleDeque a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (ReversibleDeque a) where
  compare left right = compare (toList left) (toList right)

data Tree a
  = Empty
  | Single !(Elem a)
  | Deep !Bool !Int [Elem a] !(Tree a) [Elem a]
  deriving (Eq, Ord, Read, Show)

data Elem a
  = Leaf a
  | Node !Bool !Int [Elem a]
  deriving (Eq, Ord, Read, Show)

empty :: ReversibleDeque a
empty = ReversibleDeque Empty

singleton :: a -> ReversibleDeque a
singleton value = ReversibleDeque (Single (Leaf value))

fromList :: [a] -> ReversibleDeque a
fromList = List.foldl' snoc empty

toList :: ReversibleDeque a -> [a]
toList (ReversibleDeque tree) = buildTree tree []

count :: ReversibleDeque a -> Int
count (ReversibleDeque tree) = treeSize tree

null :: ReversibleDeque a -> Bool
null deque = count deque == 0

cons :: a -> ReversibleDeque a -> ReversibleDeque a
cons value (ReversibleDeque tree) = ReversibleDeque (treeCons (Leaf value) tree)

snoc :: ReversibleDeque a -> a -> ReversibleDeque a
snoc (ReversibleDeque tree) value = ReversibleDeque (treeSnoc tree (Leaf value))

append :: ReversibleDeque a -> ReversibleDeque a -> ReversibleDeque a
append (ReversibleDeque left) (ReversibleDeque right) = ReversibleDeque (treeConcat left right)

reverse :: ReversibleDeque a -> ReversibleDeque a
reverse (ReversibleDeque tree) = ReversibleDeque (treeMirror tree)

viewL :: ReversibleDeque a -> Maybe (a, ReversibleDeque a)
viewL (ReversibleDeque tree) =
  case treeViewL tree of
    Just (Leaf value, rest) -> Just (value, ReversibleDeque rest)
    Just (element, rest) ->
      case elemViewL element of
        Just (value, remaining) -> Just (value, ReversibleDeque (treeConcat (elemToTree remaining) rest))
        Nothing -> Nothing
    Nothing -> Nothing

viewR :: ReversibleDeque a -> Maybe (ReversibleDeque a, a)
viewR (ReversibleDeque tree) =
  case treeViewR tree of
    Just (rest, Leaf value) -> Just (ReversibleDeque rest, value)
    Just (rest, element) ->
      case elemViewR element of
        Just (remaining, value) -> Just (ReversibleDeque (treeConcat rest (elemToTree remaining)), value)
        Nothing -> Nothing
    Nothing -> Nothing

first :: ReversibleDeque a -> Maybe a
first deque = fst <$> viewL deque

last :: ReversibleDeque a -> Maybe a
last deque = snd <$> viewR deque

index :: Int -> ReversibleDeque a -> Maybe a
index position (ReversibleDeque tree)
  | position < 0 || position >= treeSize tree = Nothing
  | otherwise = Just (treeGetLeaf tree position)

cursor :: ReversibleDeque a -> Cursor a
cursor deque = Cursor deque 0

cursorAt :: Int -> ReversibleDeque a -> Maybe (Cursor a)
cursorAt position deque
  | position < 0 || position > count deque = Nothing
  | otherwise = Just (Cursor deque position)

cursorPosition :: Cursor a -> Int
cursorPosition (Cursor _ position) = position

cursorCount :: Cursor a -> Int
cursorCount (Cursor deque _) = count deque

cursorIsAtStart :: Cursor a -> Bool
cursorIsAtStart value = cursorPosition value == 0

cursorIsAtEnd :: Cursor a -> Bool
cursorIsAtEnd value = cursorPosition value == cursorCount value

cursorPeekPrevious :: Cursor a -> Maybe a
cursorPeekPrevious (Cursor deque position) = index (position - 1) deque

cursorPeekNext :: Cursor a -> Maybe a
cursorPeekNext (Cursor deque position) = index position deque

cursorMovePrevious :: Cursor a -> Maybe (Cursor a)
cursorMovePrevious (Cursor deque position) = cursorAt (position - 1) deque

cursorMoveNext :: Cursor a -> Maybe (Cursor a)
cursorMoveNext (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = cursorAt (position + 1) deque

cursorSeek :: Int -> Cursor a -> Maybe (Cursor a)
cursorSeek position (Cursor deque _) = cursorAt position deque

cursorInsert :: a -> Cursor a -> Cursor a
cursorInsert value = cursorInsertList [value]

cursorInsertList :: [a] -> Cursor a -> Cursor a
cursorInsertList [] value = value
cursorInsertList values (Cursor deque position) =
  let (left, right) = List.splitAt position (toList deque)
  in Cursor (fromList (left ++ values ++ right)) (position + length values)

cursorDeletePrevious :: Cursor a -> Maybe (Cursor a)
cursorDeletePrevious (Cursor deque position)
  | position == 0 = Nothing
  | otherwise =
      let values = toList deque
      in Just (Cursor (fromList (take (position - 1) values ++ drop position values)) (position - 1))

cursorDeleteNext :: Cursor a -> Maybe (Cursor a)
cursorDeleteNext (Cursor deque position)
  | position == count deque = Nothing
  | otherwise =
      let values = toList deque
      in Just (Cursor (fromList (take position values ++ drop (position + 1) values)) position)

cursorReplaceNext :: a -> Cursor a -> Maybe (Cursor a)
cursorReplaceNext value (Cursor deque position)
  | position == count deque = Nothing
  | otherwise =
      let values = toList deque
      in Just (Cursor (fromList (take position values ++ value : drop (position + 1) values)) position)

cursorReverse :: Cursor a -> Cursor a
cursorReverse (Cursor deque position) = Cursor (reverse deque) (count deque - position)

cursorSnapshot :: Cursor a -> ReversibleDeque a
cursorSnapshot (Cursor deque _) = deque

treeSize :: Tree a -> Int
treeSize Empty = 0
treeSize (Single element) = elemSize element
treeSize (Deep _ size _ _ _) = size

elemSize :: Elem a -> Int
elemSize (Leaf _) = 1
elemSize (Node _ size _) = size

elemMirror :: Elem a -> Elem a
elemMirror leaf@(Leaf _) = leaf
elemMirror (Node reversed size children) = Node (not reversed) size children

treeMirror :: Tree a -> Tree a
treeMirror Empty = Empty
treeMirror (Single element) = Single (elemMirror element)
treeMirror (Deep reversed size prefix middle suffix) = Deep (not reversed) size prefix middle suffix

logicalChildren :: Elem a -> [Elem a]
logicalChildren (Leaf value) = [Leaf value]
logicalChildren (Node reversed _ children)
  | reversed = mirrorReversed children
  | otherwise = children

logicalPrefix :: Tree a -> [Elem a]
logicalPrefix (Deep reversed _ prefix _ suffix)
  | reversed = mirrorReversed suffix
  | otherwise = prefix
logicalPrefix _ = []

logicalSuffix :: Tree a -> [Elem a]
logicalSuffix (Deep reversed _ prefix _ suffix)
  | reversed = mirrorReversed prefix
  | otherwise = suffix
logicalSuffix _ = []

logicalMiddle :: Tree a -> Tree a
logicalMiddle (Deep reversed _ _ middle _)
  | reversed = treeMirror middle
  | otherwise = middle
logicalMiddle _ = Empty

mirrorReversed :: [Elem a] -> [Elem a]
mirrorReversed = map elemMirror . List.reverse

makeNode :: [Elem a] -> Elem a
makeNode children = Node False (checkedSum (map elemSize children)) children

makeDeep :: [Elem a] -> Tree a -> [Elem a] -> Tree a
makeDeep prefix middle suffix =
  Deep
    False
    (checkedAdd (checkedSum (map elemSize prefix)) (checkedAdd (treeSize middle) (checkedSum (map elemSize suffix))))
    prefix
    middle
    suffix

fromDigit :: [Elem a] -> Tree a
fromDigit [] = Empty
fromDigit [element] = Single element
fromDigit (element : rest) = makeDeep [element] Empty rest

elemToTree :: Elem a -> Tree a
elemToTree (Leaf value) = Single (Leaf value)
elemToTree element = fromDigit (logicalChildren element)

deepL :: [Elem a] -> Tree a -> [Elem a] -> Tree a
deepL prefix middle suffix
  | not (List.null prefix) = makeDeep prefix middle suffix
  | otherwise =
      case treeViewL middle of
        Just (node, rest) -> makeDeep (logicalChildren node) rest suffix
        Nothing -> fromDigit suffix

deepR :: [Elem a] -> Tree a -> [Elem a] -> Tree a
deepR prefix middle suffix
  | not (List.null suffix) = makeDeep prefix middle suffix
  | otherwise =
      case treeViewR middle of
        Just (rest, node) -> makeDeep prefix rest (logicalChildren node)
        Nothing -> fromDigit prefix

treeCons :: Elem a -> Tree a -> Tree a
treeCons value Empty = Single value
treeCons value (Single old) = makeDeep [value] Empty [old]
treeCons value tree@(Deep _ _ _ _ _) =
  case logicalPrefix tree of
    [a, b, c] -> makeDeep [value, a] (treeCons (makeNode [b, c]) (logicalMiddle tree)) (logicalSuffix tree)
    prefix -> makeDeep (value : prefix) (logicalMiddle tree) (logicalSuffix tree)

treeSnoc :: Tree a -> Elem a -> Tree a
treeSnoc Empty value = Single value
treeSnoc (Single old) value = makeDeep [old] Empty [value]
treeSnoc tree@(Deep _ _ _ _ _) value =
  case logicalSuffix tree of
    [a, b, c] -> makeDeep (logicalPrefix tree) (treeSnoc (logicalMiddle tree) (makeNode [a, b])) [c, value]
    suffix -> makeDeep (logicalPrefix tree) (logicalMiddle tree) (suffix ++ [value])

treeViewL :: Tree a -> Maybe (Elem a, Tree a)
treeViewL Empty = Nothing
treeViewL (Single element) = Just (element, Empty)
treeViewL tree@(Deep _ _ _ _ _) =
  case logicalPrefix tree of
    [] -> Nothing
    headElement : restPrefix ->
      let rest =
            if List.null restPrefix
              then deepL [] (logicalMiddle tree) (logicalSuffix tree)
              else makeDeep restPrefix (logicalMiddle tree) (logicalSuffix tree)
       in Just (headElement, rest)

treeViewR :: Tree a -> Maybe (Tree a, Elem a)
treeViewR Empty = Nothing
treeViewR (Single element) = Just (Empty, element)
treeViewR tree@(Deep _ _ _ _ _) =
  case logicalSuffix tree of
    [] -> Nothing
    suffix ->
      let (initSuffix, lastElement) = splitLast suffix
          rest =
            if List.null initSuffix
              then deepR (logicalPrefix tree) (logicalMiddle tree) []
              else makeDeep (logicalPrefix tree) (logicalMiddle tree) initSuffix
       in Just (rest, lastElement)

elemViewL :: Elem a -> Maybe (a, Elem a)
elemViewL (Leaf _) = Nothing
elemViewL element =
  case logicalChildren element of
    [] -> Nothing
    child : rest ->
      case child of
        Leaf value -> Just (value, makeNodeOrChild rest)
        _ ->
          case elemViewL child of
            Just (value, remaining) -> Just (value, makeNodeOrChild (remaining : rest))
            Nothing -> Nothing

elemViewR :: Elem a -> Maybe (Elem a, a)
elemViewR (Leaf _) = Nothing
elemViewR element =
  case logicalChildren element of
    [] -> Nothing
    children ->
      let (prefix, child) = splitLast children
       in case child of
            Leaf value -> Just (makeNodeOrChild prefix, value)
            _ ->
              case elemViewR child of
                Just (remaining, value) -> Just (makeNodeOrChild (prefix ++ [remaining]), value)
                Nothing -> Nothing

makeNodeOrChild :: [Elem a] -> Elem a
makeNodeOrChild [] = error "Data.Structures.FingerTree.ReversibleDeque.makeNodeOrChild: empty child list"
makeNodeOrChild [element] = element
makeNodeOrChild elements = makeNode elements

treeConcat :: Tree a -> Tree a -> Tree a
treeConcat left right = glue left [] right

glue :: Tree a -> [Elem a] -> Tree a -> Tree a
glue Empty middle right = List.foldr treeCons right middle
glue left middle Empty = List.foldl' treeSnoc left middle
glue (Single element) middle right = treeCons element (List.foldr treeCons right middle)
glue left middle (Single element) = treeSnoc (List.foldl' treeSnoc left middle) element
glue left@(Deep _ _ _ _ _) middle right@(Deep _ _ _ _ _) =
  let combined = logicalSuffix left ++ middle ++ logicalPrefix right
      bridge = nodes combined
   in makeDeep
        (logicalPrefix left)
        (glue (logicalMiddle left) bridge (logicalMiddle right))
        (logicalSuffix right)

nodes :: [Elem a] -> [Elem a]
nodes [a, b] = [makeNode [a, b]]
nodes [a, b, c] = [makeNode [a, b, c]]
nodes [a, b, c, d] = [makeNode [a, b], makeNode [c, d]]
nodes [a, b, c, d, e] = [makeNode [a, b], makeNode [c, d, e]]
nodes [a, b, c, d, e, f] = [makeNode [a, b, c], makeNode [d, e, f]]
nodes (a : b : c : rest) = makeNode [a, b, c] : nodes rest
nodes _ = error "Data.Structures.FingerTree.ReversibleDeque.nodes: fewer than two elements"

treeGetLeaf :: Tree a -> Int -> a
treeGetLeaf Empty _ = error "Data.Structures.FingerTree.ReversibleDeque.treeGetLeaf: empty tree"
treeGetLeaf (Single element) position = elemGetLeaf element position
treeGetLeaf tree@(Deep _ _ _ _ _) position
  | position < prefixSize = getInDigit prefix position
  | afterPrefix < middleSize = treeGetLeaf middle afterPrefix
  | otherwise = getInDigit (logicalSuffix tree) (afterPrefix - middleSize)
  where
    prefix = logicalPrefix tree
    prefixSize = sum (map elemSize prefix)
    afterPrefix = position - prefixSize
    middle = logicalMiddle tree
    middleSize = treeSize middle

elemGetLeaf :: Elem a -> Int -> a
elemGetLeaf (Leaf value) _ = value
elemGetLeaf element position = getInDigit (logicalChildren element) position

getInDigit :: [Elem a] -> Int -> a
getInDigit [] _ = error "Data.Structures.FingerTree.ReversibleDeque.getInDigit: index outside digit"
getInDigit (element : rest) position
  | position < elemSize element = elemGetLeaf element position
  | otherwise = getInDigit rest (position - elemSize element)

buildTree :: Tree a -> [a] -> [a]
buildTree Empty rest = rest
buildTree (Single element) rest = buildElem element rest
buildTree tree@(Deep _ _ _ _ _) rest =
  foldBuild
    buildElem
    (logicalPrefix tree)
    (buildTree (logicalMiddle tree) (foldBuild buildElem (logicalSuffix tree) rest))

buildElem :: Elem a -> [a] -> [a]
buildElem (Leaf value) rest = value : rest
buildElem element rest = foldBuild buildElem (logicalChildren element) rest

foldBuild :: (b -> [a] -> [a]) -> [b] -> [a] -> [a]
foldBuild builder values rest = foldr builder rest values

-- | Adds two element counts, refusing to publish a wrapped count.  A wrapped
-- count is worse than a failure here: it produces a negative 'count', so a
-- cursor built from it is never at its end and every bound check is nonsense.
checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 =
      error "Data.Structures.FingerTree.ReversibleDeque: internal negative count"
  | right > maxBound - left =
      error "Data.Structures.FingerTree.ReversibleDeque: length overflow"
  | otherwise = left + right

checkedSum :: [Int] -> Int
checkedSum = List.foldl' checkedAdd 0

splitLast :: [a] -> ([a], a)
splitLast [] = error "Data.Structures.FingerTree.ReversibleDeque.splitLast: empty list"
splitLast [value] = ([], value)
splitLast (value : rest) =
  let (prefix, lastValue) = splitLast rest
   in (value : prefix, lastValue)
