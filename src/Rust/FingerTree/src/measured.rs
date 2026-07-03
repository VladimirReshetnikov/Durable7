use std::fmt;
use std::marker::PhantomData;
use std::ops::Add;
use std::sync::Arc;

pub trait MeasurePolicy<T> {
    type Measure: Clone;

    fn empty() -> Self::Measure;
    fn measure(element: &T) -> Self::Measure;
    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure;
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SizeMeasure;

impl<T> MeasurePolicy<T> for SizeMeasure {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(_element: &T) -> Self::Measure {
        1
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left + right
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RankedKey<T> {
    pub count: usize,
    pub key: Option<T>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct OrderStatisticMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<T> for OrderStatisticMeasure<T>
where
    T: Clone,
{
    type Measure = RankedKey<T>;

    fn empty() -> Self::Measure {
        RankedKey {
            count: 0,
            key: None,
        }
    }

    fn measure(element: &T) -> Self::Measure {
        RankedKey {
            count: 1,
            key: Some(element.clone()),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        RankedKey {
            count: left.count + right.count,
            key: right.key.clone().or_else(|| left.key.clone()),
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SumMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<T> for SumMeasure<T>
where
    T: Add<Output = T> + Clone + Default,
{
    type Measure = T;

    fn empty() -> Self::Measure {
        T::default()
    }

    fn measure(element: &T) -> Self::Measure {
        element.clone()
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left.clone() + right.clone()
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MaxMeasure;

impl<T> MeasurePolicy<T> for MaxMeasure
where
    T: Clone + Ord,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.max(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MinMeasure;

impl<T> MeasurePolicy<T> for MinMeasure
where
    T: Clone + Ord,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.min(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

pub struct FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    root: Arc<MeasuredNode<T, P::Measure>>,
    measure: P::Measure,
    _policy: PhantomData<P>,
}

#[derive(Clone)]
pub struct MeasuredSplit<T, P>
where
    P: MeasurePolicy<T>,
{
    pub left: FingerTree<T, P>,
    pub right: FingerTree<T, P>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LocateResult<T, M> {
    pub index: usize,
    pub measure_before: M,
    pub item: Option<T>,
}

enum MeasuredNode<T, M> {
    Empty,
    Leaf {
        item: T,
        measure: M,
    },
    Node {
        left: Arc<MeasuredNode<T, M>>,
        right: Arc<MeasuredNode<T, M>>,
        len: usize,
        height: u8,
        measure: M,
    },
}

impl<T, M> MeasuredNode<T, M>
where
    M: Clone,
{
    fn empty() -> Arc<Self> {
        Arc::new(Self::Empty)
    }

    fn leaf(item: T, measure: M) -> Arc<Self> {
        Arc::new(Self::Leaf { item, measure })
    }

    fn from_vec<F, C>(mut items: Vec<T>, measure_element: &F, combine: &C) -> Arc<Self>
    where
        F: Fn(&T) -> M,
        C: Fn(&M, &M) -> M,
    {
        match items.len() {
            0 => Self::empty(),
            1 => {
                let item = items.pop().expect("single-item vector has an item");
                let measure = measure_element(&item);
                Self::leaf(item, measure)
            }
            len => {
                let right = items.split_off(len / 2);
                Self::make_node(
                    Self::from_vec(items, measure_element, combine),
                    Self::from_vec(right, measure_element, combine),
                    combine,
                )
            }
        }
    }

    fn len(&self) -> usize {
        match self {
            Self::Empty => 0,
            Self::Leaf { .. } => 1,
            Self::Node { len, .. } => *len,
        }
    }

    fn height(&self) -> u8 {
        match self {
            Self::Empty => 0,
            Self::Leaf { .. } => 1,
            Self::Node { height, .. } => *height,
        }
    }

    fn measure(&self) -> Option<&M> {
        match self {
            Self::Empty => None,
            Self::Leaf { measure, .. } | Self::Node { measure, .. } => Some(measure),
        }
    }

    fn measure_or(&self, empty: &M) -> M {
        self.measure().cloned().unwrap_or_else(|| empty.clone())
    }

    fn make_node<C>(left: Arc<Self>, right: Arc<Self>, combine: &C) -> Arc<Self>
    where
        C: Fn(&M, &M) -> M,
    {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let len = left.len() + right.len();
        let height = left.height().max(right.height()) + 1;
        let left_measure = left
            .measure()
            .expect("non-empty left measured node has a measure");
        let right_measure = right
            .measure()
            .expect("non-empty right measured node has a measure");
        let measure = combine(left_measure, right_measure);
        Arc::new(Self::Node {
            left,
            right,
            len,
            height,
            measure,
        })
    }

    fn concat<C>(left: Arc<Self>, right: Arc<Self>, combine: &C) -> Arc<Self>
    where
        C: Fn(&M, &M) -> M,
    {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let left_height = left.height();
        let right_height = right.height();
        if left_height > right_height + 1
            && let Self::Node {
                left: left_left,
                right: left_right,
                ..
            } = left.as_ref()
        {
            let joined = Self::concat(Arc::clone(left_right), right, combine);
            return Self::balance(Arc::clone(left_left), joined, combine);
        }

        if right_height > left_height + 1
            && let Self::Node {
                left: right_left,
                right: right_right,
                ..
            } = right.as_ref()
        {
            let joined = Self::concat(left, Arc::clone(right_left), combine);
            return Self::balance(joined, Arc::clone(right_right), combine);
        }

        Self::balance(left, right, combine)
    }

    fn balance<C>(left: Arc<Self>, right: Arc<Self>, combine: &C) -> Arc<Self>
    where
        C: Fn(&M, &M) -> M,
    {
        if left.len() == 0 || right.len() == 0 {
            return Self::make_node(left, right, combine);
        }

        let left_height = left.height();
        let right_height = right.height();
        if left_height > right_height + 1
            && let Self::Node {
                left: left_left,
                right: left_right,
                ..
            } = left.as_ref()
        {
            if left_left.height() >= left_right.height() {
                return Self::make_node(
                    Arc::clone(left_left),
                    Self::make_node(Arc::clone(left_right), right, combine),
                    combine,
                );
            }

            if let Self::Node {
                left: middle_left,
                right: middle_right,
                ..
            } = left_right.as_ref()
            {
                return Self::make_node(
                    Self::make_node(Arc::clone(left_left), Arc::clone(middle_left), combine),
                    Self::make_node(Arc::clone(middle_right), right, combine),
                    combine,
                );
            }
        }

        if right_height > left_height + 1
            && let Self::Node {
                left: right_left,
                right: right_right,
                ..
            } = right.as_ref()
        {
            if right_right.height() >= right_left.height() {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(right_left), combine),
                    Arc::clone(right_right),
                    combine,
                );
            }

            if let Self::Node {
                left: middle_left,
                right: middle_right,
                ..
            } = right_left.as_ref()
            {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(middle_left), combine),
                    Self::make_node(Arc::clone(middle_right), Arc::clone(right_right), combine),
                    combine,
                );
            }
        }

        Self::make_node(left, right, combine)
    }

    fn split_at<C>(tree: &Arc<Self>, index: usize, combine: &C) -> (Arc<Self>, Arc<Self>)
    where
        C: Fn(&M, &M) -> M,
    {
        if index == 0 {
            return (Self::empty(), Arc::clone(tree));
        }

        if index == tree.len() {
            return (Arc::clone(tree), Self::empty());
        }

        match tree.as_ref() {
            Self::Empty => (Self::empty(), Self::empty()),
            Self::Leaf { .. } => {
                debug_assert_eq!(index, 0);
                (Self::empty(), Arc::clone(tree))
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                match index.cmp(&left_len) {
                    std::cmp::Ordering::Less => {
                        let (before, after) = Self::split_at(left, index, combine);
                        (before, Self::concat(after, Arc::clone(right), combine))
                    }
                    std::cmp::Ordering::Equal => (Arc::clone(left), Arc::clone(right)),
                    std::cmp::Ordering::Greater => {
                        let (before, after) = Self::split_at(right, index - left_len, combine);
                        (Self::concat(Arc::clone(left), before, combine), after)
                    }
                }
            }
        }
    }

    fn boundary_index<F, C>(
        tree: &Arc<Self>,
        prefix: &M,
        predicate: &mut F,
        combine: &C,
    ) -> Option<usize>
    where
        F: FnMut(&M) -> bool,
        C: Fn(&M, &M) -> M,
    {
        match tree.as_ref() {
            Self::Empty => None,
            Self::Leaf { measure, .. } => {
                let next = combine(prefix, measure);
                predicate(&next).then_some(0)
            }
            Self::Node { left, right, .. } => {
                let left_measure = left
                    .measure()
                    .expect("non-empty left measured node has a measure");
                let left_total = combine(prefix, left_measure);
                if predicate(&left_total) {
                    Self::boundary_index(left, prefix, predicate, combine)
                } else {
                    Self::boundary_index(right, &left_total, predicate, combine)
                        .map(|index| left.len() + index)
                }
            }
        }
    }

    fn prefix_measure<C>(tree: &Arc<Self>, count: usize, empty: &M, combine: &C) -> M
    where
        C: Fn(&M, &M) -> M,
    {
        if count == 0 {
            return empty.clone();
        }

        if count >= tree.len() {
            return tree.measure_or(empty);
        }

        match tree.as_ref() {
            Self::Empty => empty.clone(),
            Self::Leaf { measure, .. } => {
                debug_assert_eq!(count, 1);
                measure.clone()
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                if count <= left_len {
                    Self::prefix_measure(left, count, empty, combine)
                } else {
                    let left_measure = left.measure_or(empty);
                    let right_measure =
                        Self::prefix_measure(right, count - left_len, empty, combine);
                    combine(&left_measure, &right_measure)
                }
            }
        }
    }

    fn first(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf { item, .. } => Some(item),
            Self::Node { left, .. } => left.first(),
        }
    }

    fn last(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf { item, .. } => Some(item),
            Self::Node { right, .. } => right.last(),
        }
    }

    fn get(&self, index: usize) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf { item, .. } => (index == 0).then_some(item),
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                if index < left_len {
                    left.get(index)
                } else {
                    right.get(index - left_len)
                }
            }
        }
    }

    fn copy_to_vec(&self, destination: &mut Vec<T>)
    where
        T: Clone,
    {
        match self {
            Self::Empty => {}
            Self::Leaf { item, .. } => destination.push(item.clone()),
            Self::Node { left, right, .. } => {
                left.copy_to_vec(destination);
                right.copy_to_vec(destination);
            }
        }
    }

    #[cfg(test)]
    fn validate<F, C>(
        &self,
        measure_element: &F,
        empty: &M,
        combine: &C,
    ) -> Result<(usize, M), String>
    where
        F: Fn(&T) -> M,
        C: Fn(&M, &M) -> M,
        M: PartialEq + std::fmt::Debug,
    {
        match self {
            Self::Empty => Ok((0, empty.clone())),
            Self::Leaf { item, measure } => {
                let expected = measure_element(item);
                if expected != *measure {
                    return Err(format!(
                        "leaf cached measure {measure:?} disagrees with expected {expected:?}"
                    ));
                }

                Ok((1, expected))
            }
            Self::Node {
                left,
                right,
                len,
                height,
                measure,
            } => {
                let (left_len, left_measure) = left.validate(measure_element, empty, combine)?;
                let (right_len, right_measure) = right.validate(measure_element, empty, combine)?;
                let expected_len = left_len + right_len;
                if expected_len != *len {
                    return Err(format!(
                        "node cached length {len} disagrees with child total {expected_len}"
                    ));
                }

                let expected_height = left.height().max(right.height()) + 1;
                if expected_height != *height {
                    return Err(format!(
                        "node cached height {height} disagrees with child height {expected_height}"
                    ));
                }

                if left.height().abs_diff(right.height()) > 2 {
                    return Err(format!(
                        "node is too imbalanced: left height {}, right height {}",
                        left.height(),
                        right.height()
                    ));
                }

                let expected_measure = combine(&left_measure, &right_measure);
                if expected_measure != *measure {
                    return Err(format!(
                        "node cached measure {measure:?} disagrees with child total {expected_measure:?}"
                    ));
                }

                Ok((expected_len, expected_measure))
            }
        }
    }
}

impl<T, P> Clone for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            root: Arc::clone(&self.root),
            measure: self.measure.clone(),
            _policy: PhantomData,
        }
    }
}

impl<T, P> fmt::Debug for FingerTree<T, P>
where
    T: fmt::Debug,
    P: MeasurePolicy<T>,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<T, P> PartialEq for FingerTree<T, P>
where
    T: PartialEq,
    P: MeasurePolicy<T>,
{
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len() && self.iter().eq(other.iter())
    }
}

impl<T, P> Eq for FingerTree<T, P>
where
    T: Eq,
    P: MeasurePolicy<T>,
{
}

impl<T, P> FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            root: MeasuredNode::empty(),
            measure: P::empty(),
            _policy: PhantomData,
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.root.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.len() == 0
    }

    #[must_use]
    pub fn measure(&self) -> &P::Measure {
        &self.measure
    }

    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.root.first()
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.root.last()
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        (index < self.len()).then(|| self.root.get(index)).flatten()
    }

    pub fn iter(&self) -> Iter<'_, T, P::Measure> {
        Iter::new(&self.root)
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        (self.is_empty() && other.is_empty()) || Arc::ptr_eq(&self.root, &other.root)
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_root(MeasuredNode::from_vec(items, &P::measure, &P::combine))
    }

    fn from_root(root: Arc<MeasuredNode<T, P::Measure>>) -> Self {
        let measure = root.measure_or(&P::empty());
        Self {
            root,
            measure,
            _policy: PhantomData,
        }
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        Self::from_root(MeasuredNode::concat(
            Arc::clone(&self.root),
            Arc::clone(&other.root),
            &P::combine,
        ))
    }

    #[must_use]
    pub fn split<F>(&self, predicate: F) -> MeasuredSplit<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate).unwrap_or(self.len());
        self.split_at_index_unchecked(index)
    }

    #[must_use]
    pub fn split_at_index(&self, index: usize) -> Option<MeasuredSplit<T, P>> {
        (index <= self.len()).then(|| self.split_at_index_unchecked(index))
    }

    #[must_use]
    pub fn prefix_measure(&self, count: usize) -> Option<P::Measure> {
        (count <= self.len())
            .then(|| MeasuredNode::prefix_measure(&self.root, count, &P::empty(), &P::combine))
    }

    fn split_at_index_unchecked(&self, index: usize) -> MeasuredSplit<T, P> {
        let (left, right) = MeasuredNode::split_at(&self.root, index, &P::combine);
        MeasuredSplit {
            left: Self::from_root(left),
            right: Self::from_root(right),
        }
    }

    fn boundary_index<F>(&self, mut predicate: F) -> Option<usize>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        MeasuredNode::boundary_index(&self.root, &P::empty(), &mut predicate, &P::combine)
    }

    #[cfg(test)]
    pub(crate) fn tree_depth(&self) -> usize {
        self.root.height() as usize
    }

    #[cfg(test)]
    pub(crate) fn validate_invariants(&self)
    where
        P::Measure: PartialEq + std::fmt::Debug,
    {
        let (len, measure) = self
            .root
            .validate(&P::measure, &P::empty(), &P::combine)
            .expect("measured tree invariant failure");
        assert_eq!(len, self.len());
        assert_eq!(measure, self.measure);
    }

    #[cfg(test)]
    pub(crate) fn shared_node_count_with(&self, other: &Self) -> usize {
        use std::collections::HashSet;

        fn collect<T, M>(
            tree: &Arc<MeasuredNode<T, M>>,
            seen: &mut HashSet<*const MeasuredNode<T, M>>,
        ) {
            if !seen.insert(Arc::as_ptr(tree)) {
                return;
            }

            if let MeasuredNode::Node { left, right, .. } = tree.as_ref() {
                collect(left, seen);
                collect(right, seen);
            }
        }

        fn count<T, M>(
            tree: &Arc<MeasuredNode<T, M>>,
            seen: &HashSet<*const MeasuredNode<T, M>>,
        ) -> usize {
            let mut total = usize::from(seen.contains(&Arc::as_ptr(tree)));
            if let MeasuredNode::Node { left, right, .. } = tree.as_ref() {
                total += count(left, seen);
                total += count(right, seen);
            }

            total
        }

        let mut seen = HashSet::new();
        collect(&self.root, &mut seen);
        count(&other.root, &seen)
    }
}

impl<T, P> Default for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> FromIterator<T> for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T, P> FingerTree<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut result = Vec::with_capacity(self.len());
        self.root.copy_to_vec(&mut result);
        result
    }

    #[must_use]
    pub fn prepend(&self, item: T) -> Self {
        let measure = P::measure(&item);
        Self::from_root(MeasuredNode::concat(
            MeasuredNode::leaf(item, measure),
            Arc::clone(&self.root),
            &P::combine,
        ))
    }

    #[must_use]
    pub fn append(&self, item: T) -> Self {
        let measure = P::measure(&item);
        Self::from_root(MeasuredNode::concat(
            Arc::clone(&self.root),
            MeasuredNode::leaf(item, measure),
            &P::combine,
        ))
    }

    #[must_use]
    pub fn try_view_left(&self) -> Option<(T, Self)> {
        let first = self.front()?.clone();
        Some((first, self.split_at_index_unchecked(1).right))
    }

    #[must_use]
    pub fn try_view_right(&self) -> Option<(T, Self)> {
        let last = self.back()?.clone();
        Some((last, self.split_at_index_unchecked(self.len() - 1).left))
    }

    #[must_use]
    pub fn try_split_find<F>(&self, predicate: F) -> Option<(Self, T, Self)>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate)?;
        let item = self.root.get(index)?.clone();
        let left = self.split_at_index_unchecked(index).left;
        let right = self.split_at_index_unchecked(index + 1).right;
        Some((left, item, right))
    }

    #[must_use]
    pub fn try_locate<F>(&self, predicate: F) -> LocateResult<T, P::Measure>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let Some(index) = self.boundary_index(predicate) else {
            return LocateResult {
                index: self.len(),
                measure_before: self.measure.clone(),
                item: None,
            };
        };

        LocateResult {
            index,
            measure_before: MeasuredNode::prefix_measure(
                &self.root,
                index,
                &P::empty(),
                &P::combine,
            ),
            item: self.root.get(index).cloned(),
        }
    }
}

pub struct Iter<'a, T, M> {
    stack: Vec<&'a MeasuredNode<T, M>>,
}

impl<'a, T, M> Iter<'a, T, M> {
    fn new(root: &'a Arc<MeasuredNode<T, M>>) -> Self {
        Self {
            stack: vec![root.as_ref()],
        }
    }
}

impl<'a, T, M> Iterator for Iter<'a, T, M> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(tree) = self.stack.pop() {
            match tree {
                MeasuredNode::Empty => {}
                MeasuredNode::Leaf { item, .. } => return Some(item),
                MeasuredNode::Node { left, right, .. } => {
                    self.stack.push(right);
                    self.stack.push(left);
                }
            }
        }

        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn size_measure_tracks_count() {
        let tree: FingerTree<_, SizeMeasure> = [10, 20, 30].into_iter().collect();
        let appended = tree.append(40);

        assert_eq!(*tree.measure(), 3);
        assert_eq!(*appended.measure(), 4);
        assert_eq!(tree.get(1), Some(&20));
        assert_eq!(tree.prefix_measure(2), Some(2));
        assert_eq!(tree.prefix_measure(4), None);
        assert_eq!(tree.to_vec(), vec![10, 20, 30]);
        tree.validate_invariants();
        appended.validate_invariants();
    }

    #[test]
    fn split_and_locate_use_prefix_measure() {
        let tree: FingerTree<_, SumMeasure<i32>> = [2, 3, 5, 7].into_iter().collect();
        let split = tree.split(|sum| *sum >= 6);
        let located = tree.try_locate(|sum| *sum >= 6);

        assert_eq!(split.left.to_vec(), vec![2, 3]);
        assert_eq!(split.right.to_vec(), vec![5, 7]);
        assert_eq!(tree.prefix_measure(3), Some(10));
        assert_eq!(located.index, 2);
        assert_eq!(located.measure_before, 5);
        assert_eq!(located.item, Some(5));
        split.left.validate_invariants();
        split.right.validate_invariants();
    }

    #[test]
    fn max_and_min_measures_are_monoids() {
        let max_tree: FingerTree<_, MaxMeasure> = [3, 1, 4, 2].into_iter().collect();
        let min_tree: FingerTree<_, MinMeasure> = [3, 1, 4, 2].into_iter().collect();

        assert_eq!(max_tree.measure(), &Some(4));
        assert_eq!(min_tree.measure(), &Some(1));
        max_tree.validate_invariants();
        min_tree.validate_invariants();
    }

    #[test]
    fn order_statistic_measure_tracks_count_and_last_key() {
        let tree: FingerTree<_, OrderStatisticMeasure<i32>> = [1, 3, 3, 7].into_iter().collect();
        let located = tree.try_locate(|measure| measure.key.as_ref().is_some_and(|key| *key >= 3));

        assert_eq!(
            tree.measure(),
            &RankedKey {
                count: 4,
                key: Some(7)
            }
        );
        assert_eq!(tree.prefix_measure(2).unwrap().count, 2);
        assert_eq!(located.index, 1);
        assert_eq!(located.measure_before.count, 1);
        assert_eq!(located.item, Some(3));
        tree.validate_invariants();
    }

    #[test]
    fn measured_tree_splits_share_subtrees_and_cache_measures() {
        let tree: FingerTree<_, SizeMeasure> = (0..256).collect();
        let split = tree.split_at_index(96).unwrap();
        let joined = split.left.concat(&split.right);

        assert_eq!(split.left.len(), 96);
        assert_eq!(split.right.len(), 160);
        assert_eq!(joined.to_vec(), tree.to_vec());
        assert!(tree.shared_node_count_with(&split.left) > 64);
        assert!(tree.shared_node_count_with(&split.right) > 100);
        assert!(tree.tree_depth() < 24);
        split.left.validate_invariants();
        split.right.validate_invariants();
        joined.validate_invariants();
    }

    #[test]
    fn randomized_sum_measure_splits_match_vector_prefixes() {
        let mut rng = 0xdecaf_bad5eed_u64;
        let mut values = Vec::new();
        for _ in 0..300 {
            rng = rng.wrapping_mul(6364136223846793005).wrapping_add(1);
            values.push(((rng >> 32) % 9 + 1) as i32);
        }

        let tree: FingerTree<_, SumMeasure<i32>> = values.iter().copied().collect();
        for threshold in [1, 7, 37, 111, 777, *tree.measure() + 1] {
            let located = tree.try_locate(|sum| *sum >= threshold);
            let mut prefix = 0;
            let expected = values.iter().position(|value| {
                prefix += *value;
                prefix >= threshold
            });

            assert_eq!(located.index, expected.unwrap_or(values.len()));
            assert_eq!(located.item, expected.map(|index| values[index]));
        }

        tree.validate_invariants();
    }
}
