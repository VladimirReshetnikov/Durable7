use std::cmp::Ordering;
use std::fmt;
use std::sync::Arc;

pub struct PersistentDeque<T> {
    root: Arc<DequeTree<T>>,
}

impl<T> Clone for PersistentDeque<T> {
    fn clone(&self) -> Self {
        Self {
            root: Arc::clone(&self.root),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeSplit<T> {
    pub left: PersistentDeque<T>,
    pub right: PersistentDeque<T>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeItemSplit<T> {
    pub left: PersistentDeque<T>,
    pub item: T,
    pub right: PersistentDeque<T>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeRangeSplit<T> {
    pub before: PersistentDeque<T>,
    pub range: PersistentDeque<T>,
    pub after: PersistentDeque<T>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequePop<T> {
    pub value: T,
    pub rest: PersistentDeque<T>,
}

enum DequeTree<T> {
    Empty,
    Leaf(T),
    Node {
        left: Arc<DequeTree<T>>,
        right: Arc<DequeTree<T>>,
        len: usize,
        height: u8,
    },
}

impl<T> DequeTree<T> {
    fn empty() -> Arc<Self> {
        Arc::new(Self::Empty)
    }

    fn leaf(item: T) -> Arc<Self> {
        Arc::new(Self::Leaf(item))
    }

    fn from_vec(mut items: Vec<T>) -> Arc<Self> {
        match items.len() {
            0 => Self::empty(),
            1 => Self::leaf(items.pop().expect("single-item vector has an item")),
            len => {
                let right = items.split_off(len / 2);
                Self::make_node(Self::from_vec(items), Self::from_vec(right))
            }
        }
    }

    fn len(&self) -> usize {
        match self {
            Self::Empty => 0,
            Self::Leaf(_) => 1,
            Self::Node { len, .. } => *len,
        }
    }

    fn height(&self) -> u8 {
        match self {
            Self::Empty => 0,
            Self::Leaf(_) => 1,
            Self::Node { height, .. } => *height,
        }
    }

    fn make_node(left: Arc<Self>, right: Arc<Self>) -> Arc<Self> {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let len = left.len() + right.len();
        let height = left.height().max(right.height()) + 1;
        Arc::new(Self::Node {
            left,
            right,
            len,
            height,
        })
    }

    fn concat(left: Arc<Self>, right: Arc<Self>) -> Arc<Self> {
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
            let joined = Self::concat(Arc::clone(left_right), right);
            return Self::balance(Arc::clone(left_left), joined);
        }

        if right_height > left_height + 1
            && let Self::Node {
                left: right_left,
                right: right_right,
                ..
            } = right.as_ref()
        {
            let joined = Self::concat(left, Arc::clone(right_left));
            return Self::balance(joined, Arc::clone(right_right));
        }

        Self::balance(left, right)
    }

    fn balance(left: Arc<Self>, right: Arc<Self>) -> Arc<Self> {
        if left.len() == 0 || right.len() == 0 {
            return Self::make_node(left, right);
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
                    Self::make_node(Arc::clone(left_right), right),
                );
            }

            if let Self::Node {
                left: middle_left,
                right: middle_right,
                ..
            } = left_right.as_ref()
            {
                return Self::make_node(
                    Self::make_node(Arc::clone(left_left), Arc::clone(middle_left)),
                    Self::make_node(Arc::clone(middle_right), right),
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
                    Self::make_node(left, Arc::clone(right_left)),
                    Arc::clone(right_right),
                );
            }

            if let Self::Node {
                left: middle_left,
                right: middle_right,
                ..
            } = right_left.as_ref()
            {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(middle_left)),
                    Self::make_node(Arc::clone(middle_right), Arc::clone(right_right)),
                );
            }
        }

        Self::make_node(left, right)
    }

    fn split_at(tree: &Arc<Self>, index: usize) -> (Arc<Self>, Arc<Self>) {
        if index == 0 {
            return (Self::empty(), Arc::clone(tree));
        }

        if index == tree.len() {
            return (Arc::clone(tree), Self::empty());
        }

        match tree.as_ref() {
            Self::Empty => (Self::empty(), Self::empty()),
            Self::Leaf(_) => {
                debug_assert_eq!(index, 0);
                (Self::empty(), Arc::clone(tree))
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                match index.cmp(&left_len) {
                    Ordering::Less => {
                        let (before, after) = Self::split_at(left, index);
                        (before, Self::concat(after, Arc::clone(right)))
                    }
                    Ordering::Equal => (Arc::clone(left), Arc::clone(right)),
                    Ordering::Greater => {
                        let (before, after) = Self::split_at(right, index - left_len);
                        (Self::concat(Arc::clone(left), before), after)
                    }
                }
            }
        }
    }

    fn first(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf(item) => Some(item),
            Self::Node { left, .. } => left.first(),
        }
    }

    fn last(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf(item) => Some(item),
            Self::Node { right, .. } => right.last(),
        }
    }

    fn get(&self, index: usize) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf(item) => (index == 0).then_some(item),
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

    fn set(tree: &Arc<Self>, index: usize, item: T) -> Option<Arc<Self>> {
        match tree.as_ref() {
            Self::Empty => None,
            Self::Leaf(_) => (index == 0).then(|| Self::leaf(item)),
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                if index < left_len {
                    Some(Self::balance(
                        Self::set(left, index, item)?,
                        Arc::clone(right),
                    ))
                } else {
                    Some(Self::balance(
                        Arc::clone(left),
                        Self::set(right, index - left_len, item)?,
                    ))
                }
            }
        }
    }

    fn bound_index<F>(&self, predicate: &mut F) -> usize
    where
        F: FnMut(&T) -> bool,
    {
        match self {
            Self::Empty => 0,
            Self::Leaf(item) => usize::from(!predicate(item)),
            Self::Node { left, right, .. } => {
                let left_last = left.last().expect("non-empty left child has a last leaf");
                if predicate(left_last) {
                    left.bound_index(predicate)
                } else {
                    left.len() + right.bound_index(predicate)
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
            Self::Leaf(item) => destination.push(item.clone()),
            Self::Node { left, right, .. } => {
                left.copy_to_vec(destination);
                right.copy_to_vec(destination);
            }
        }
    }

    #[cfg(test)]
    fn validate(&self) -> Result<usize, String> {
        match self {
            Self::Empty => Ok(0),
            Self::Leaf(_) => Ok(1),
            Self::Node {
                left,
                right,
                len,
                height,
            } => {
                let left_len = left.validate()?;
                let right_len = right.validate()?;
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

                Ok(expected_len)
            }
        }
    }
}

impl<T> PersistentDeque<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            root: DequeTree::empty(),
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self {
            root: DequeTree::from_vec(items),
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

    pub fn iter(&self) -> Iter<'_, T> {
        Iter::new(&self.root)
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        (self.is_empty() && other.is_empty()) || Arc::ptr_eq(&self.root, &other.root)
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        Self {
            root: DequeTree::concat(DequeTree::leaf(item), Arc::clone(&self.root)),
        }
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        Self {
            root: DequeTree::concat(Arc::clone(&self.root), DequeTree::leaf(item)),
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

        Self {
            root: DequeTree::concat(Arc::clone(&self.root), Arc::clone(&other.root)),
        }
    }

    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        (!self.is_empty()).then(|| self.split_at(1).expect("one is a valid split").right)
    }

    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        let split_index = self.len().checked_sub(1)?;
        Some(
            self.split_at(split_index)
                .expect("len - 1 is a valid split")
                .left,
        )
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        (index < self.len()).then(|| Self {
            root: DequeTree::set(&self.root, index, item).expect("validated index must update"),
        })
    }

    #[must_use]
    pub fn update_at<F>(&self, index: usize, updater: F) -> Option<Self>
    where
        F: FnOnce(&T) -> T,
    {
        let current = self.get(index)?;
        self.set_item(index, updater(current))
    }

    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        let split = self.split_at(index).expect("validated index must split");
        Some(split.left.push_back(item).concat(&split.right))
    }

    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        if index > self.len() {
            return None;
        }

        let inserted = Self::from_vec(items.into_iter().collect());
        if inserted.is_empty() {
            return Some(self.clone());
        }

        let split = self.split_at(index).expect("validated index must split");
        Some(split.left.concat(&inserted).concat(&split.right))
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let split = self.split_range(index, 1)?;
        Some(split.before.concat(&split.after))
    }

    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        let split = self.split_range(index, count)?;
        Some(split.before.concat(&split.after))
    }

    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        Some(self.split_range(index, count)?.range)
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<DequeSplit<T>> {
        if index > self.len() {
            return None;
        }

        let (left, right) = DequeTree::split_at(&self.root, index);
        Some(DequeSplit {
            left: Self { root: left },
            right: Self { root: right },
        })
    }

    #[must_use]
    pub fn split_range(&self, index: usize, count: usize) -> Option<DequeRangeSplit<T>> {
        let end = checked_range_end(self.len(), index, count)?;
        let (before, rest) = DequeTree::split_at(&self.root, index);
        let (range, after) = DequeTree::split_at(&rest, end - index);
        Some(DequeRangeSplit {
            before: Self { root: before },
            range: Self { root: range },
            after: Self { root: after },
        })
    }

    #[must_use]
    pub fn add_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        self.concat(&Self::from_vec(items.into_iter().collect()))
    }

    #[cfg(test)]
    fn tree_depth(&self) -> usize {
        self.root.height() as usize
    }

    #[cfg(test)]
    fn validate_invariants(&self) {
        let len = self
            .root
            .validate()
            .expect("persistent deque tree invariant failure");
        assert_eq!(len, self.len());
    }

    #[cfg(test)]
    fn shared_node_count_with(&self, other: &Self) -> usize {
        use std::collections::HashSet;

        fn collect<T>(tree: &Arc<DequeTree<T>>, seen: &mut HashSet<*const DequeTree<T>>) {
            if !seen.insert(Arc::as_ptr(tree)) {
                return;
            }

            if let DequeTree::Node { left, right, .. } = tree.as_ref() {
                collect(left, seen);
                collect(right, seen);
            }
        }

        fn count<T>(tree: &Arc<DequeTree<T>>, seen: &HashSet<*const DequeTree<T>>) -> usize {
            let mut total = usize::from(seen.contains(&Arc::as_ptr(tree)));
            if let DequeTree::Node { left, right, .. } = tree.as_ref() {
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

impl<T> PersistentDeque<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut result = Vec::with_capacity(self.len());
        self.root.copy_to_vec(&mut result);
        result
    }

    #[must_use]
    pub fn pop_first(&self) -> Option<DequePop<T>> {
        let value = self.front()?.clone();
        let rest = self.remove_first().expect("non-empty deque has a tail");
        Some(DequePop { value, rest })
    }

    #[must_use]
    pub fn pop_last(&self) -> Option<DequePop<T>> {
        let value = self.back()?.clone();
        let rest = self.remove_last().expect("non-empty deque has an init");
        Some(DequePop { value, rest })
    }

    #[must_use]
    pub fn split_item_at(&self, index: usize) -> Option<DequeItemSplit<T>> {
        if index >= self.len() {
            return None;
        }

        let split = self.split_range(index, 1)?;
        Some(DequeItemSplit {
            left: split.before,
            item: self
                .get(index)
                .expect("validated index has an item")
                .clone(),
            right: split.after,
        })
    }
}

impl<T> Default for PersistentDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> fmt::Debug for PersistentDeque<T>
where
    T: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<T> PartialEq for PersistentDeque<T>
where
    T: PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len() && self.iter().eq(other.iter())
    }
}

impl<T> Eq for PersistentDeque<T> where T: Eq {}

impl<T> FromIterator<T> for PersistentDeque<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> IntoIterator for PersistentDeque<T>
where
    T: Clone,
{
    type Item = T;
    type IntoIter = std::vec::IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.to_vec().into_iter()
    }
}

impl<'a, T> IntoIterator for &'a PersistentDeque<T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T> PersistentDeque<T>
where
    T: Ord,
{
    #[must_use]
    pub fn sorted_lower_bound(&self, item: &T) -> usize {
        self.sorted_lower_bound_by(item, T::cmp)
    }

    #[must_use]
    pub fn sorted_upper_bound(&self, item: &T) -> usize {
        self.sorted_upper_bound_by(item, T::cmp)
    }

    pub fn sorted_binary_search(&self, item: &T) -> Result<usize, usize> {
        let index = self.sorted_lower_bound(item);
        if index < self.len()
            && self
                .get(index)
                .is_some_and(|value| value.cmp(item) == Ordering::Equal)
        {
            Ok(index)
        } else {
            Err(index)
        }
    }

    #[must_use]
    pub fn sorted_contains(&self, item: &T) -> bool {
        self.sorted_binary_search(item).is_ok()
    }
}

impl<T> PersistentDeque<T> {
    #[must_use]
    pub fn sorted_lower_bound_by<F>(&self, item: &T, mut compare: F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let mut predicate = |value: &T| compare(value, item) != Ordering::Less;
        self.root.bound_index(&mut predicate)
    }

    #[must_use]
    pub fn sorted_upper_bound_by<F>(&self, item: &T, mut compare: F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let mut predicate = |value: &T| compare(value, item) == Ordering::Greater;
        self.root.bound_index(&mut predicate)
    }
}

pub struct Iter<'a, T> {
    stack: Vec<&'a DequeTree<T>>,
}

impl<'a, T> Iter<'a, T> {
    fn new(root: &'a Arc<DequeTree<T>>) -> Self {
        Self {
            stack: vec![root.as_ref()],
        }
    }
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(tree) = self.stack.pop() {
            match tree {
                DequeTree::Empty => {}
                DequeTree::Leaf(item) => return Some(item),
                DequeTree::Node { left, right, .. } => {
                    self.stack.push(right);
                    self.stack.push(left);
                }
            }
        }

        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, Some(usize::MAX))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReversibleDeque<T> {
    items: PersistentDeque<T>,
    reversed: bool,
}

impl<T> ReversibleDeque<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: PersistentDeque::new(),
            reversed: false,
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_deque(PersistentDeque::from_vec(items))
    }

    fn from_deque(items: PersistentDeque<T>) -> Self {
        Self {
            items,
            reversed: false,
        }
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
    pub fn reverse(&self) -> Self {
        Self {
            items: self.items.clone(),
            reversed: !self.reversed,
        }
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
    }

    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.get(0)
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.len().checked_sub(1).and_then(|index| self.get(index))
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len() {
            return None;
        }

        let physical = self.physical_index(index);
        self.items.get(physical)
    }

    fn physical_index(&self, logical: usize) -> usize {
        if self.reversed {
            self.len() - 1 - logical
        } else {
            logical
        }
    }

    fn physical_insert_index(&self, logical: usize) -> usize {
        if self.reversed {
            self.len() - logical
        } else {
            logical
        }
    }
}

impl<T> Default for ReversibleDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for ReversibleDeque<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> ReversibleDeque<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut result = self.items.to_vec();
        if self.reversed {
            result.reverse();
        }

        result
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        let items = if self.reversed {
            self.items.push_back(item)
        } else {
            self.items.push_front(item)
        };
        Self {
            items,
            reversed: self.reversed,
        }
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        let items = if self.reversed {
            self.items.push_front(item)
        } else {
            self.items.push_back(item)
        };
        Self {
            items,
            reversed: self.reversed,
        }
    }

    #[must_use]
    pub fn pop_front(&self) -> Option<DequePop<T>> {
        if !self.reversed {
            return self.items.pop_first();
        }

        let value = self.front()?.clone();
        let mut rest = self.to_vec();
        rest.remove(0);
        Some(DequePop {
            value,
            rest: PersistentDeque::from_vec(rest),
        })
    }

    #[must_use]
    pub fn pop_back(&self) -> Option<DequePop<T>> {
        if !self.reversed {
            return self.items.pop_last();
        }

        let value = self.back()?.clone();
        let mut rest = self.to_vec();
        rest.pop();
        Some(DequePop {
            value,
            rest: PersistentDeque::from_vec(rest),
        })
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        Some(Self {
            items: self.items.set_item(self.physical_index(index), item)?,
            reversed: self.reversed,
        })
    }

    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        Some(Self {
            items: self
                .items
                .insert_at(self.physical_insert_index(index), item)?,
            reversed: self.reversed,
        })
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        Some(Self {
            items: self.items.remove_at(self.physical_index(index))?,
            reversed: self.reversed,
        })
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<DequeSplit<T>> {
        if self.reversed {
            return PersistentDeque::from_vec(self.to_vec()).split_at(index);
        }

        self.items.split_at(index)
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if !self.reversed && !other.reversed {
            return Self::from_deque(self.items.concat(&other.items));
        }

        let mut next = self.to_vec();
        next.extend(other.to_vec());
        Self::from_vec(next)
    }
}

fn checked_range_end(len: usize, index: usize, count: usize) -> Option<usize> {
    let end = index.checked_add(count)?;
    (index <= len && end <= len).then_some(end)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn deque_updates_preserve_snapshots() {
        let deque: PersistentDeque<_> = [1, 2, 3].into_iter().collect();
        let pushed = deque.push_front(0).push_back(4);
        let removed = pushed.remove_at(2).unwrap();

        assert_eq!(deque.to_vec(), vec![1, 2, 3]);
        assert_eq!(pushed.to_vec(), vec![0, 1, 2, 3, 4]);
        assert_eq!(removed.to_vec(), vec![0, 1, 3, 4]);
        deque.validate_invariants();
        pushed.validate_invariants();
        removed.validate_invariants();
    }

    #[test]
    fn deque_split_and_concat_round_trip() {
        let deque: PersistentDeque<_> = (0..6).collect();
        let split = deque.split_range(2, 3).unwrap();

        assert_eq!(split.before.to_vec(), vec![0, 1]);
        assert_eq!(split.range.to_vec(), vec![2, 3, 4]);
        assert_eq!(split.after.to_vec(), vec![5]);
        assert_eq!(
            split
                .before
                .concat(&split.range)
                .concat(&split.after)
                .to_vec(),
            deque.to_vec()
        );
        split.before.validate_invariants();
        split.range.validate_invariants();
        split.after.validate_invariants();
    }

    #[test]
    fn deque_non_endpoint_edits_share_unchanged_subtrees() {
        let deque: PersistentDeque<_> = (0..256).collect();
        let changed = deque.set_item(128, -1).unwrap();
        let inserted = deque.insert_at(120, -2).unwrap();
        let removed = deque.remove_range(96, 32).unwrap();

        assert_eq!(deque.get(128), Some(&128));
        assert_eq!(changed.get(128), Some(&-1));
        assert!(deque.shared_node_count_with(&changed) > 100);
        assert!(deque.shared_node_count_with(&inserted) > 100);
        assert!(deque.shared_node_count_with(&removed) > 100);
        assert!(!deque.shares_storage_with(&changed));
        changed.validate_invariants();
        inserted.validate_invariants();
        removed.validate_invariants();
    }

    #[test]
    fn deque_depth_grows_logarithmically_for_bulk_build_and_pushes() {
        let deque: PersistentDeque<_> = (0..4096).collect();
        let pushed = (4096..8192).fold(deque.clone(), |current, value| current.push_back(value));

        assert!(deque.tree_depth() < 32, "depth was {}", deque.tree_depth());
        assert!(
            pushed.tree_depth() < 40,
            "depth was {}",
            pushed.tree_depth()
        );
        assert_eq!(pushed.front(), Some(&0));
        assert_eq!(pushed.back(), Some(&8191));
        pushed.validate_invariants();
    }

    #[test]
    fn sorted_search_returns_first_equal() {
        let deque: PersistentDeque<_> = [1, 2, 2, 2, 5].into_iter().collect();

        assert_eq!(deque.sorted_lower_bound(&2), 1);
        assert_eq!(deque.sorted_upper_bound(&2), 4);
        assert_eq!(deque.sorted_binary_search(&2), Ok(1));
        assert_eq!(deque.sorted_binary_search(&4), Err(4));
    }

    #[test]
    fn randomized_deque_model_replay_covers_branching_versions() {
        let mut rng = 0x5eed_1234_u64;
        let mut deque = PersistentDeque::new();
        let mut model = Vec::new();
        let mut retained = vec![(deque.clone(), model.clone())];

        for step in 0..500 {
            rng = rng.wrapping_mul(6364136223846793005).wrapping_add(1);
            match (rng >> 32) % 9 {
                0 if model.len() < 512 => {
                    let value = step;
                    deque = deque.push_front(value);
                    model.insert(0, value);
                }
                1 if model.len() < 512 => {
                    let value = step;
                    deque = deque.push_back(value);
                    model.push(value);
                }
                2 if !model.is_empty() => {
                    assert_eq!(deque.pop_first().unwrap().value, model.remove(0));
                    deque = deque.remove_first().unwrap();
                }
                3 if !model.is_empty() => {
                    assert_eq!(deque.pop_last().unwrap().value, model.pop().unwrap());
                    deque = deque.remove_last().unwrap();
                }
                4 if !model.is_empty() => {
                    let index = ((rng >> 24) as usize) % model.len();
                    let value = -step;
                    deque = deque.set_item(index, value).unwrap();
                    model[index] = value;
                }
                5 if model.len() < 512 => {
                    let index = ((rng >> 24) as usize) % (model.len() + 1);
                    let value = step * 3;
                    deque = deque.insert_at(index, value).unwrap();
                    model.insert(index, value);
                }
                6 if !model.is_empty() => {
                    let index = ((rng >> 24) as usize) % model.len();
                    deque = deque.remove_at(index).unwrap();
                    model.remove(index);
                }
                7 if !retained.is_empty() && model.len() < 256 => {
                    let selected = ((rng >> 24) as usize) % retained.len();
                    let (prefix, prefix_model) = retained[selected].clone();
                    if prefix_model.len() + model.len() <= 512 {
                        deque = prefix.concat(&deque);
                        let mut next = prefix_model;
                        next.extend(model);
                        model = next;
                    }
                }
                _ => {
                    retained.push((deque.clone(), model.clone()));
                    if retained.len() > 24 {
                        retained.remove(0);
                    }
                }
            }

            assert_eq!(deque.to_vec(), model);
            deque.validate_invariants();
        }

        for (version, expected) in retained {
            assert_eq!(version.to_vec(), expected);
            version.validate_invariants();
        }
    }

    #[test]
    fn reversible_reverse_is_storage_sharing_view() {
        let deque: ReversibleDeque<_> = [1, 2, 3].into_iter().collect();
        let reversed = deque.reverse();
        let restored = reversed.reverse();

        assert_eq!(reversed.to_vec(), vec![3, 2, 1]);
        assert_eq!(restored.to_vec(), vec![1, 2, 3]);
        assert!(deque.shares_storage_with(&reversed));
    }

    #[test]
    fn reversible_edits_share_underlying_deque_tree() {
        let deque: ReversibleDeque<_> = (0..256).collect();
        let reversed = deque.reverse();
        let changed = reversed.set_item(100, -1).unwrap();
        let inserted = reversed.insert_at(120, -2).unwrap();
        let removed = reversed.remove_at(80).unwrap();
        let pushed = reversed.push_front(-3).push_back(-4);

        assert_eq!(reversed.get(0), Some(&255));
        assert_eq!(changed.get(100), Some(&-1));
        assert_eq!(inserted.get(120), Some(&-2));
        assert_eq!(removed.len(), 255);
        assert_eq!(pushed.front(), Some(&-3));
        assert_eq!(pushed.back(), Some(&-4));
        assert!(deque.shares_storage_with(&reversed));
        assert!(reversed.items.shared_node_count_with(&changed.items) > 100);
        assert!(reversed.items.shared_node_count_with(&inserted.items) > 100);
        assert!(reversed.items.shared_node_count_with(&removed.items) > 100);
        assert!(reversed.items.shared_node_count_with(&pushed.items) > 100);
        assert!(reversed.items.tree_depth() < 24);
        changed.items.validate_invariants();
        inserted.items.validate_invariants();
        removed.items.validate_invariants();
        pushed.items.validate_invariants();
    }
}
