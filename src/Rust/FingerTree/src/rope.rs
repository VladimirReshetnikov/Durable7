use crate::deque::{Iter as DequeIter, PersistentDeque};
use crate::measured::{FingerTree, MeasurePolicy};
use std::marker::PhantomData;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Rope<T> {
    items: PersistentDeque<T>,
}

impl<T> Rope<T> {
    #[must_use]
    pub fn new() -> Self {
        Self::from_deque(PersistentDeque::new())
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_deque(PersistentDeque::from_vec(items))
    }

    fn from_deque(items: PersistentDeque<T>) -> Self {
        Self { items }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.items.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.items.front()
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.items.back()
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.items.get(index)
    }

    pub fn iter(&self) -> DequeIter<'_, T> {
        self.items.iter()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
    }
}

impl<T> Default for Rope<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for Rope<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> Rope<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.to_vec()
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        Self::from_deque(self.items.push_front(item))
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        Self::from_deque(self.items.push_back(item))
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        self.items.set_item(index, item).map(Self::from_deque)
    }

    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        self.items.insert_at(index, item).map(Self::from_deque)
    }

    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        self.items.insert_range(index, items).map(Self::from_deque)
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        self.items.remove_at(index).map(Self::from_deque)
    }

    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        self.items.remove_range(index, count).map(Self::from_deque)
    }

    #[must_use]
    pub fn slice(&self, index: usize, count: usize) -> Option<Self> {
        self.items.get_range(index, count).map(Self::from_deque)
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<(Self, Self)> {
        let split = self.items.split_at(index)?;
        Some((Self::from_deque(split.left), Self::from_deque(split.right)))
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        Self::from_deque(self.items.concat(&other.items))
    }

    #[must_use]
    pub fn compact(&self) -> Self {
        Self::from_vec(self.to_vec())
    }
}

struct CountedMeasure<T, P>(PhantomData<(T, P)>);

impl<T, P> MeasurePolicy<T> for CountedMeasure<T, P>
where
    P: MeasurePolicy<T>,
{
    type Measure = (usize, P::Measure);

    fn empty() -> Self::Measure {
        (0, P::empty())
    }

    fn measure(element: &T) -> Self::Measure {
        (1, P::measure(element))
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        (left.0 + right.0, P::combine(&left.1, &right.1))
    }
}

type MeasuredRopeTree<T, P> = FingerTree<T, CountedMeasure<T, P>>;

pub struct MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    tree: MeasuredRopeTree<T, P>,
}

#[derive(Clone)]
pub struct MeasuredRopeSplit<T, P>
where
    P: MeasurePolicy<T>,
{
    pub left: MeasuredRope<T, P>,
    pub right: MeasuredRope<T, P>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MeasuredRopeLocate<T, M> {
    pub index: usize,
    pub measure_before: M,
    pub value: Option<T>,
}

impl<T, P> Clone for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            tree: self.tree.clone(),
        }
    }
}

impl<T, P> MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_tree(MeasuredRopeTree::new())
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_tree(items.into_iter().collect())
    }

    fn from_tree(tree: MeasuredRopeTree<T, P>) -> Self {
        Self { tree }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.tree.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.tree.is_empty()
    }

    #[must_use]
    pub fn measure(&self) -> &P::Measure {
        &self.tree.measure().1
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.tree.get(index)
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.tree.shares_storage_with(&other.tree)
    }

    #[must_use]
    pub fn prefix_measure(&self, count: usize) -> Option<P::Measure> {
        self.tree.prefix_measure(count).map(|measure| measure.1)
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<MeasuredRopeSplit<T, P>> {
        self.split_at_count(index)
    }

    #[must_use]
    pub fn split_by_measure<F>(&self, mut predicate: F) -> MeasuredRopeSplit<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let split = self.tree.split(|measure| predicate(&measure.1));
        MeasuredRopeSplit {
            left: Self::from_tree(split.left),
            right: Self::from_tree(split.right),
        }
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        Self::from_tree(self.tree.concat(&other.tree))
    }

    fn split_at_count(&self, index: usize) -> Option<MeasuredRopeSplit<T, P>> {
        if index > self.len() {
            return None;
        }

        let split = self.tree.split(|measure| measure.0 > index);
        Some(MeasuredRopeSplit {
            left: Self::from_tree(split.left),
            right: Self::from_tree(split.right),
        })
    }
}

impl<T, P> Default for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> FromIterator<T> for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T, P> MeasuredRope<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.tree.to_vec()
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        Self::from_tree(self.tree.append(item))
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let before = self
            .split_at_count(index)
            .expect("validated index splits measured rope");
        let after = before
            .right
            .split_at_count(1)
            .expect("validated index leaves an item to replace");
        Some(before.left.push_back(item).concat(&after.right))
    }

    #[must_use]
    pub fn locate_by_measure<F>(&self, mut predicate: F) -> MeasuredRopeLocate<T, P::Measure>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let located = self.tree.try_locate(|measure| predicate(&measure.1));
        MeasuredRopeLocate {
            index: located.index,
            measure_before: located.measure_before.1,
            value: located.item,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NewlineMeasure;

impl MeasurePolicy<char> for NewlineMeasure {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(element: &char) -> Self::Measure {
        usize::from(*element == '\n')
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left + right
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TextRope {
    chars: Rope<char>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LineColumn {
    pub line: usize,
    pub column: usize,
}

impl TextRope {
    #[must_use]
    pub fn new() -> Self {
        Self { chars: Rope::new() }
    }

    #[must_use]
    pub fn from_text(text: &str) -> Self {
        Self {
            chars: text.chars().collect(),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.chars.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.chars.is_empty()
    }

    #[must_use]
    pub fn as_string(&self) -> String {
        self.chars.iter().collect()
    }

    #[must_use]
    pub fn line_count(&self) -> usize {
        self.chars.iter().filter(|ch| **ch == '\n').count() + 1
    }

    #[must_use]
    pub fn line_of_offset(&self, offset: usize) -> Option<usize> {
        if offset > self.len() {
            return None;
        }

        Some(
            self.chars
                .iter()
                .take(offset)
                .filter(|ch| **ch == '\n')
                .count(),
        )
    }

    #[must_use]
    pub fn line_start_offset(&self, line: usize) -> Option<usize> {
        if line == 0 {
            return Some(0);
        }

        let mut seen = 0;
        for (index, ch) in self.chars.iter().enumerate() {
            if *ch == '\n' {
                seen += 1;
                if seen == line {
                    return Some(index + 1);
                }
            }
        }

        None
    }

    #[must_use]
    pub fn line_column_of(&self, offset: usize) -> Option<LineColumn> {
        let line = self.line_of_offset(offset)?;
        let start = self.line_start_offset(line)?;
        Some(LineColumn {
            line,
            column: offset - start,
        })
    }

    #[must_use]
    pub fn offset_of(&self, line: usize, column: usize) -> Option<usize> {
        let start = self.line_start_offset(line)?;
        let end = self.line_end_offset(line)?;
        let offset = start.checked_add(column)?;
        (offset <= end).then_some(offset)
    }

    #[must_use]
    pub fn get_line(&self, line: usize) -> Option<String> {
        let start = self.line_start_offset(line)?;
        let end = self.line_end_offset(line)?;
        Some(self.chars.iter().skip(start).take(end - start).collect())
    }

    #[must_use]
    pub fn lines(&self) -> Vec<String> {
        (0..self.line_count())
            .filter_map(|line| self.get_line(line))
            .collect()
    }

    #[must_use]
    pub fn to_char_rope(&self) -> Rope<char> {
        self.chars.clone()
    }

    #[must_use]
    pub fn to_measured_rope(&self) -> MeasuredRope<char, NewlineMeasure> {
        self.chars.iter().cloned().collect()
    }

    fn line_end_offset(&self, line: usize) -> Option<usize> {
        let start = self.line_start_offset(line)?;
        for (relative, ch) in self.chars.iter().skip(start).enumerate() {
            if *ch == '\n' {
                return Some(start + relative);
            }
        }

        Some(self.len())
    }
}

impl Default for TextRope {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RopeBuilder {
    text: String,
}

impl RopeBuilder {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn append(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self
    }

    pub fn append_line(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self.text.push('\n');
        self
    }

    pub fn clear(&mut self) -> &mut Self {
        self.text.clear();
        self
    }

    #[must_use]
    pub fn to_rope(&self) -> Rope<char> {
        self.text.chars().collect()
    }

    #[must_use]
    pub fn to_text_rope(&self) -> TextRope {
        TextRope::from_text(&self.text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::measured::SumMeasure;

    #[test]
    fn rope_edits_preserve_snapshots() {
        let rope: Rope<_> = [1, 2, 3].into_iter().collect();
        let edited = rope.insert_at(1, 9).unwrap().remove_at(3).unwrap();

        assert_eq!(rope.to_vec(), vec![1, 2, 3]);
        assert_eq!(edited.to_vec(), vec![1, 9, 2]);
        assert_eq!(rope.slice(1, 2).unwrap().to_vec(), vec![2, 3]);
    }

    #[test]
    fn rope_edits_share_underlying_deque_tree() {
        let rope: Rope<_> = (0..256).collect();
        let split = rope.split_at(96).unwrap();
        let joined = split.0.concat(&split.1);
        let changed = rope.set_item(128, -1).unwrap();
        let inserted = rope.insert_at(120, -2).unwrap();
        let removed = rope.remove_range(96, 32).unwrap();

        assert_eq!(joined.to_vec(), rope.to_vec());
        assert_eq!(changed.get(128), Some(&-1));
        assert_eq!(inserted.get(120), Some(&-2));
        assert_eq!(removed.len(), 224);
        assert!(rope.items.shared_node_count_with(&split.0.items) > 64);
        assert!(rope.items.shared_node_count_with(&split.1.items) > 100);
        assert!(rope.items.shared_node_count_with(&changed.items) > 100);
        assert!(rope.items.shared_node_count_with(&inserted.items) > 100);
        assert!(rope.items.shared_node_count_with(&removed.items) > 100);
        assert!(rope.items.tree_depth() < 24);
        split.0.items.validate_invariants();
        split.1.items.validate_invariants();
        joined.items.validate_invariants();
        changed.items.validate_invariants();
        inserted.items.validate_invariants();
        removed.items.validate_invariants();
    }

    #[test]
    fn measured_rope_locates_by_user_measure() {
        let rope: MeasuredRope<_, SumMeasure<i32>> = [2, 4, 8].into_iter().collect();
        let located = rope.locate_by_measure(|sum| *sum > 5);

        assert_eq!(rope.measure(), &14);
        assert_eq!(rope.prefix_measure(2), Some(6));
        assert_eq!(located.index, 1);
        assert_eq!(located.measure_before, 2);
        assert_eq!(located.value, Some(4));
    }

    #[test]
    fn measured_rope_uses_shared_measured_tree_storage() {
        let rope: MeasuredRope<_, SumMeasure<i32>> = (1..=256).collect();
        let split = rope.split_at(96).unwrap();
        let joined = split.left.concat(&split.right);
        let changed = rope.set_item(128, 999).unwrap();
        let by_measure = rope.split_by_measure(|sum| *sum >= 1_000);

        let mut prefix = 0;
        let expected_measure_boundary = (1..=256)
            .position(|value| {
                prefix += value;
                prefix >= 1_000
            })
            .unwrap();

        assert_eq!(rope.get(42), Some(&43));
        assert_eq!(rope.prefix_measure(10), Some(55));
        assert_eq!(rope.prefix_measure(300), None);
        assert_eq!(split.left.len(), 96);
        assert_eq!(split.right.get(0), Some(&97));
        assert_eq!(joined.to_vec(), rope.to_vec());
        assert_eq!(changed.get(128), Some(&999));
        assert_eq!(changed.measure(), &(*rope.measure() - 129 + 999));
        assert_eq!(by_measure.left.len(), expected_measure_boundary);
        assert!(rope.tree.shared_node_count_with(&split.left.tree) > 64);
        assert!(rope.tree.shared_node_count_with(&split.right.tree) > 100);
        assert!(rope.tree.shared_node_count_with(&changed.tree) > 100);
        assert!(rope.tree.tree_depth() < 24);
        split.left.tree.validate_invariants();
        split.right.tree.validate_invariants();
        joined.tree.validate_invariants();
        changed.tree.validate_invariants();
    }

    #[test]
    fn text_rope_uses_character_offsets_and_editor_line_count() {
        let text = TextRope::from_text("alpha\nbeta\n");

        assert_eq!(text.line_count(), 3);
        assert_eq!(
            text.line_column_of(7),
            Some(LineColumn { line: 1, column: 1 })
        );
        assert_eq!(text.offset_of(1, 2), Some(8));
        assert_eq!(text.get_line(0).unwrap(), "alpha");
        assert_eq!(text.get_line(2).unwrap(), "");
        assert_eq!(text.lines(), vec!["alpha", "beta", ""]);
    }

    #[test]
    fn builder_creates_char_and_text_ropes() {
        let mut builder = RopeBuilder::new();
        builder.append("hello").append_line(" world");

        assert_eq!(builder.to_rope().to_vec().len(), 12);
        assert_eq!(builder.to_text_rope().as_string(), "hello world\n");
    }
}
