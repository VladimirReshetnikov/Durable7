#![forbid(unsafe_code)]
#![doc = "Persistent hash-array mapped trie map and set."]

use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};
use std::iter::FusedIterator;
use std::ops::Index;
use std::sync::Arc;

const BITS_PER_LEVEL: u32 = 5;
const BRANCH_MASK: u32 = 0x1f;
/// Deepest shift at which a `Branch` node can appear (32-bit hash, 5 bits per level).
const MAX_BRANCH_SHIFT: u32 = 30;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DuplicateKey;

impl fmt::Display for DuplicateKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("an entry with the same key already exists")
    }
}

impl std::error::Error for DuplicateKey {}

pub struct PersistentHashMap<K, V, S = RandomState> {
    root: Option<Arc<Node<K, V>>>,
    len: usize,
    hasher: S,
}

impl<K, V, S: Clone> Clone for PersistentHashMap<K, V, S> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            len: self.len,
            hasher: self.hasher.clone(),
        }
    }
}

#[derive(Clone)]
enum Node<K, V> {
    Leaf {
        hash: u32,
        key: K,
        value: V,
    },
    Collision {
        hash: u32,
        entries: Arc<[(K, V)]>,
    },
    Branch {
        bitmap: u32,
        children: Arc<[Arc<Node<K, V>>]>,
    },
}

struct InsertResult<K, V> {
    node: Arc<Node<K, V>>,
    added: bool,
    changed: bool,
    duplicate: bool,
}

struct RemoveResult<K, V> {
    node: Option<Arc<Node<K, V>>>,
    removed: Option<(K, V)>,
    changed: bool,
}

impl<K, V> PersistentHashMap<K, V, RandomState> {
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S> PersistentHashMap<K, V, S> {
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            root: None,
            len: 0,
            hasher,
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.len
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        &self.hasher
    }

    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (None, None) => true,
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            _ => false,
        }
    }

    #[must_use]
    pub fn iter(&self) -> Iter<'_, K, V> {
        let mut stack = Vec::new();
        if let Some(root) = self.root.as_deref() {
            stack.push(IterFrame::Node(root));
        }

        Iter {
            stack,
            remaining: self.len,
        }
    }

    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.iter().map(|(key, _)| key)
    }

    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.iter().map(|(_, value)| value)
    }
}

impl<K, V, S: Clone> PersistentHashMap<K, V, S> {
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }

        Self {
            root: None,
            len: 0,
            hasher: self.hasher.clone(),
        }
    }
}

impl<K, V, S> PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.get(key).is_some()
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.get_key_value(key).map(|(_, value)| value)
    }

    #[must_use]
    pub fn get_key_value(&self, key: &K) -> Option<(&K, &V)> {
        let hash = self.hash_key(key);
        self.root
            .as_deref()
            .and_then(|node| get_in_node(node, hash, key, 0))
    }

    fn hash_key(&self, key: &K) -> u32 {
        self.hasher.hash_one(key) as u32
    }
}

impl<K, V, S> PersistentHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        self.try_remove(key)
            .map_or_else(|| self.clone(), |(map, _)| map)
    }

    #[must_use]
    pub fn try_remove(&self, key: &K) -> Option<(Self, V)> {
        self.try_remove_entry(key)
            .map(|(map, _, value)| (map, value))
    }

    #[must_use]
    pub fn try_remove_entry(&self, key: &K) -> Option<(Self, K, V)> {
        let root = self.root.as_ref()?;
        let hash = self.hash_key(key);
        let result = remove_node(root, hash, key, 0);
        if !result.changed {
            return None;
        }

        let (removed_key, removed_value) =
            result.removed.expect("changed removal must carry an entry");
        Some((
            Self {
                root: result.node,
                len: self.len - 1,
                hasher: self.hasher.clone(),
            },
            removed_key,
            removed_value,
        ))
    }
}

impl<K, V, S> PersistentHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn insert(&self, key: K, value: V) -> Self {
        let hash = self.hash_key(&key);
        match &self.root {
            None => Self {
                root: Some(Arc::new(Node::Leaf { hash, key, value })),
                len: 1,
                hasher: self.hasher.clone(),
            },
            Some(root) => {
                let result = insert_node(root, hash, key, value, 0, true);
                Self {
                    root: Some(result.node),
                    len: self.len + usize::from(result.added),
                    hasher: self.hasher.clone(),
                }
            }
        }
    }

    pub fn add(&self, key: K, value: V) -> Result<Self, DuplicateKey> {
        let (map, added) = self.try_add(key, value);
        if added { Ok(map) } else { Err(DuplicateKey) }
    }

    #[must_use]
    pub fn try_add(&self, key: K, value: V) -> (Self, bool) {
        let hash = self.hash_key(&key);
        match &self.root {
            None => (
                Self {
                    root: Some(Arc::new(Node::Leaf { hash, key, value })),
                    len: 1,
                    hasher: self.hasher.clone(),
                },
                true,
            ),
            Some(root) => {
                let result = insert_node(root, hash, key, value, 0, false);
                if result.duplicate {
                    return (self.clone(), false);
                }

                (
                    Self {
                        root: Some(result.node),
                        len: self.len + usize::from(result.added),
                        hasher: self.hasher.clone(),
                    },
                    result.added,
                )
            }
        }
    }

    #[must_use]
    pub fn set_items<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        let mut map = self.clone();
        for (key, value) in items {
            map = map.insert(key, value);
        }

        map
    }
}

impl<K, V, S: Default> Default for PersistentHashMap<K, V, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V, S> FromIterator<(K, V)> for PersistentHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone + Default,
{
    fn from_iter<T: IntoIterator<Item = (K, V)>>(iter: T) -> Self {
        Self::with_hasher(S::default()).set_items(iter)
    }
}

impl<'a, K, V, S> IntoIterator for &'a PersistentHashMap<K, V, S> {
    type Item = (&'a K, &'a V);
    type IntoIter = Iter<'a, K, V>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<K, V, S> fmt::Debug for PersistentHashMap<K, V, S>
where
    K: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_map().entries(self.iter()).finish()
    }
}

impl<K, V, S> PartialEq for PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    V: PartialEq,
    S: BuildHasher,
{
    fn eq(&self, other: &Self) -> bool {
        self.len == other.len
            && self
                .iter()
                .all(|(key, value)| other.get(key) == Some(value))
    }
}

impl<K, V, S> Eq for PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    V: Eq,
    S: BuildHasher,
{
}

impl<K, V, S> Index<&K> for PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    type Output = V;

    fn index(&self, key: &K) -> &V {
        self.get(key).expect("no entry found for key")
    }
}

pub struct Iter<'a, K, V> {
    stack: Vec<IterFrame<'a, K, V>>,
    remaining: usize,
}

impl<K, V> Clone for Iter<'_, K, V> {
    fn clone(&self) -> Self {
        Self {
            stack: self.stack.clone(),
            remaining: self.remaining,
        }
    }
}

enum IterFrame<'a, K, V> {
    Node(&'a Node<K, V>),
    Branch(std::slice::Iter<'a, Arc<Node<K, V>>>),
    Collision(std::slice::Iter<'a, (K, V)>),
}

impl<K, V> Clone for IterFrame<'_, K, V> {
    fn clone(&self) -> Self {
        match self {
            Self::Node(node) => Self::Node(node),
            Self::Branch(children) => Self::Branch(children.clone()),
            Self::Collision(entries) => Self::Collision(entries.clone()),
        }
    }
}

impl<'a, K, V> Iterator for Iter<'a, K, V> {
    type Item = (&'a K, &'a V);

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(frame) = self.stack.pop() {
            match frame {
                IterFrame::Node(node) => match node {
                    Node::Leaf { key, value, .. } => {
                        self.remaining -= 1;
                        return Some((key, value));
                    }
                    Node::Collision { entries, .. } => {
                        self.stack.push(IterFrame::Collision(entries.iter()));
                    }
                    Node::Branch { children, .. } => {
                        self.stack.push(IterFrame::Branch(children.iter()));
                    }
                },
                IterFrame::Branch(mut children) => {
                    if let Some(child) = children.next() {
                        self.stack.push(IterFrame::Branch(children));
                        self.stack.push(IterFrame::Node(child.as_ref()));
                    }
                }
                IterFrame::Collision(mut entries) => {
                    if let Some((key, value)) = entries.next() {
                        self.stack.push(IterFrame::Collision(entries));
                        self.remaining -= 1;
                        return Some((key, value));
                    }
                }
            }
        }

        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<K, V> ExactSizeIterator for Iter<'_, K, V> {}

impl<K, V> FusedIterator for Iter<'_, K, V> {}

pub struct PersistentHashSet<T, S = RandomState> {
    map: PersistentHashMap<T, (), S>,
}

impl<T, S: Clone> Clone for PersistentHashSet<T, S> {
    fn clone(&self) -> Self {
        Self {
            map: self.map.clone(),
        }
    }
}

impl<T> PersistentHashSet<T, RandomState> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            map: PersistentHashMap::new(),
        }
    }
}

impl<T, S> PersistentHashSet<T, S> {
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            map: PersistentHashMap::with_hasher(hasher),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.map.hasher()
    }

    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        self.map.shares_root_with(&other.map)
    }

    #[must_use]
    pub fn iter(&self) -> SetIter<'_, T> {
        SetIter {
            inner: self.map.iter(),
        }
    }
}

impl<T, S: Clone> PersistentHashSet<T, S> {
    #[must_use]
    pub fn clear(&self) -> Self {
        Self {
            map: self.map.clear(),
        }
    }
}

impl<T, S> PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    #[must_use]
    pub fn contains(&self, value: &T) -> bool {
        self.map.contains_key(value)
    }

    #[must_use]
    pub fn get(&self, value: &T) -> Option<&T> {
        self.map.get_key_value(value).map(|(key, _)| key)
    }
}

impl<T, S> PersistentHashSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn insert(&self, value: T) -> Self {
        Self {
            map: self.map.insert(value, ()),
        }
    }

    pub fn add(&self, value: T) -> Result<Self, DuplicateKey> {
        self.map.add(value, ()).map(|map| Self { map })
    }

    #[must_use]
    pub fn try_add(&self, value: T) -> (Self, bool) {
        let (map, added) = self.map.try_add(value, ());
        (Self { map }, added)
    }

    #[must_use]
    pub fn remove(&self, value: &T) -> Self {
        Self {
            map: self.map.remove(value),
        }
    }

    #[must_use]
    pub fn try_remove(&self, value: &T) -> Option<(Self, T)> {
        let (map, actual, ()) = self.map.try_remove_entry(value)?;
        Some((Self { map }, actual))
    }

    #[must_use]
    pub fn union<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = self.clone();
        for value in other {
            result = result.insert(value);
        }

        result
    }

    #[must_use]
    pub fn intersect<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let probe = PersistentHashSet::with_hasher(self.map.hasher().clone()).union(other);
        let mut result = Self::with_hasher(self.map.hasher().clone());
        for value in self.iter() {
            if probe.contains(value) {
                result = result.insert(value.clone());
            }
        }

        result
    }

    #[must_use]
    pub fn except<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = self.clone();
        for value in other {
            result = result.remove(&value);
        }

        result
    }

    #[must_use]
    pub fn symmetric_except<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let distinct = Self::with_hasher(self.map.hasher().clone()).union(other);
        let mut result = self.clone();
        for value in distinct.iter() {
            result = if result.contains(value) {
                result.remove(value)
            } else {
                result.insert(value.clone())
            };
        }

        result
    }

    #[must_use]
    pub fn is_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = PersistentHashSet::with_hasher(self.map.hasher().clone()).union(other);
        self.iter().all(|value| probe.contains(value))
    }

    #[must_use]
    pub fn is_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        other.into_iter().all(|value| self.contains(&value))
    }

    #[must_use]
    pub fn is_proper_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = PersistentHashSet::with_hasher(self.map.hasher().clone()).union(other);
        self.len() < probe.len() && self.iter().all(|value| probe.contains(value))
    }

    #[must_use]
    pub fn is_proper_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = PersistentHashSet::with_hasher(self.map.hasher().clone()).union(other);
        self.len() > probe.len() && probe.iter().all(|value| self.contains(value))
    }

    #[must_use]
    pub fn overlaps<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        other.into_iter().any(|value| self.contains(&value))
    }

    #[must_use]
    pub fn set_equals<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let other_set = PersistentHashSet::with_hasher(self.map.hasher().clone()).union(other);
        self.len() == other_set.len() && self.iter().all(|value| other_set.contains(value))
    }
}

impl<T, S: Default> Default for PersistentHashSet<T, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<T, S> FromIterator<T> for PersistentHashSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone + Default,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::with_hasher(S::default()).union(iter)
    }
}

impl<'a, T, S> IntoIterator for &'a PersistentHashSet<T, S> {
    type Item = &'a T;
    type IntoIter = SetIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T, S> fmt::Debug for PersistentHashSet<T, S>
where
    T: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_set().entries(self.iter()).finish()
    }
}

impl<T, S> PartialEq for PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len() && self.iter().all(|value| other.contains(value))
    }
}

impl<T, S> Eq for PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
}

pub struct SetIter<'a, T> {
    inner: Iter<'a, T, ()>,
}

impl<T> Clone for SetIter<'_, T> {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
        }
    }
}

impl<'a, T> Iterator for SetIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|(key, ())| key)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> ExactSizeIterator for SetIter<'_, T> {}

impl<T> FusedIterator for SetIter<'_, T> {}

fn get_in_node<'a, K, V>(
    node: &'a Node<K, V>,
    hash: u32,
    key: &K,
    shift: u32,
) -> Option<(&'a K, &'a V)>
where
    K: Eq,
{
    match node {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value,
        } => (*leaf_hash == hash && leaf_key == key).then_some((leaf_key, value)),
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash != hash {
                return None;
            }

            entries
                .iter()
                .find(|(entry_key, _)| entry_key == key)
                .map(|(entry_key, value)| (entry_key, value))
        }
        Node::Branch { bitmap, children } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if bitmap & bit == 0 {
                return None;
            }

            let index = sparse_index(*bitmap, bit);
            get_in_node(&children[index], hash, key, shift + BITS_PER_LEVEL)
        }
    }
}

fn insert_node<K, V>(
    node: &Arc<Node<K, V>>,
    hash: u32,
    key: K,
    value: V,
    shift: u32,
    overwrite: bool,
) -> InsertResult<K, V>
where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    match node.as_ref() {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value: leaf_value,
        } => {
            if *leaf_hash == hash && leaf_key == &key {
                if !overwrite {
                    return InsertResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                        duplicate: true,
                    };
                }

                if leaf_value == &value {
                    return InsertResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                        duplicate: false,
                    };
                }

                return InsertResult {
                    node: Arc::new(Node::Leaf {
                        hash,
                        key: leaf_key.clone(),
                        value,
                    }),
                    added: false,
                    changed: true,
                    duplicate: false,
                };
            }

            let new_leaf = Arc::new(Node::Leaf { hash, key, value });
            if *leaf_hash == hash {
                return InsertResult {
                    node: Arc::new(Node::Collision {
                        hash,
                        entries: Arc::from(vec![
                            (leaf_key.clone(), leaf_value.clone()),
                            leaf_entry(new_leaf),
                        ]),
                    }),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            InsertResult {
                node: merge_two(Arc::clone(node), *leaf_hash, new_leaf, hash, shift),
                added: true,
                changed: true,
                duplicate: false,
            }
        }
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash == hash {
                if let Some(index) = entries.iter().position(|(entry_key, _)| entry_key == &key) {
                    if !overwrite {
                        return InsertResult {
                            node: Arc::clone(node),
                            added: false,
                            changed: false,
                            duplicate: true,
                        };
                    }

                    if entries[index].1 == value {
                        return InsertResult {
                            node: Arc::clone(node),
                            added: false,
                            changed: false,
                            duplicate: false,
                        };
                    }

                    let mut next = entries.to_vec();
                    next[index] = (next[index].0.clone(), value);
                    return InsertResult {
                        node: Arc::new(Node::Collision {
                            hash,
                            entries: Arc::from(next),
                        }),
                        added: false,
                        changed: true,
                        duplicate: false,
                    };
                }

                let mut next = entries.to_vec();
                next.push((key, value));
                return InsertResult {
                    node: Arc::new(Node::Collision {
                        hash,
                        entries: Arc::from(next),
                    }),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            let new_leaf = Arc::new(Node::Leaf { hash, key, value });
            InsertResult {
                node: merge_two(Arc::clone(node), *bucket_hash, new_leaf, hash, shift),
                added: true,
                changed: true,
                duplicate: false,
            }
        }
        Node::Branch { bitmap, children } => {
            let bit = bit_position(hash_fragment(hash, shift));
            let index = sparse_index(*bitmap, bit);
            if bitmap & bit == 0 {
                let mut next_children = children.to_vec();
                next_children.insert(index, Arc::new(Node::Leaf { hash, key, value }));
                return InsertResult {
                    node: Arc::new(Node::Branch {
                        bitmap: bitmap | bit,
                        children: Arc::from(next_children),
                    }),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            let child_result = insert_node(
                &children[index],
                hash,
                key,
                value,
                shift + BITS_PER_LEVEL,
                overwrite,
            );
            if child_result.duplicate || !child_result.changed {
                return InsertResult {
                    node: Arc::clone(node),
                    added: false,
                    changed: false,
                    duplicate: child_result.duplicate,
                };
            }

            let mut next_children = children.to_vec();
            next_children[index] = child_result.node;
            InsertResult {
                node: Arc::new(Node::Branch {
                    bitmap: *bitmap,
                    children: Arc::from(next_children),
                }),
                added: child_result.added,
                changed: true,
                duplicate: false,
            }
        }
    }
}

fn remove_node<K, V>(node: &Arc<Node<K, V>>, hash: u32, key: &K, shift: u32) -> RemoveResult<K, V>
where
    K: Eq + Clone,
    V: Clone,
{
    match node.as_ref() {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value,
        } => {
            if *leaf_hash == hash && leaf_key == key {
                RemoveResult {
                    node: None,
                    removed: Some((leaf_key.clone(), value.clone())),
                    changed: true,
                }
            } else {
                RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                }
            }
        }
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash != hash {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            }

            let Some(index) = entries.iter().position(|(entry_key, _)| entry_key == key) else {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            };

            let removed = entries[index].clone();
            let mut next = entries.to_vec();
            next.remove(index);
            let node = match next.as_slice() {
                [] => None,
                [(key, value)] => Some(Arc::new(Node::Leaf {
                    hash,
                    key: key.clone(),
                    value: value.clone(),
                })),
                _ => Some(Arc::new(Node::Collision {
                    hash,
                    entries: Arc::from(next),
                })),
            };

            RemoveResult {
                node,
                removed: Some(removed),
                changed: true,
            }
        }
        Node::Branch { bitmap, children } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if bitmap & bit == 0 {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            }

            let index = sparse_index(*bitmap, bit);
            let child_result = remove_node(&children[index], hash, key, shift + BITS_PER_LEVEL);
            if !child_result.changed {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            }

            let mut next_children = children.to_vec();
            let next_bitmap;
            match child_result.node {
                Some(child) => {
                    next_children[index] = child;
                    next_bitmap = *bitmap;
                }
                None => {
                    next_children.remove(index);
                    next_bitmap = bitmap & !bit;
                }
            }

            let next_node = match next_children.as_slice() {
                [] => None,
                [only] if !matches!(only.as_ref(), Node::Branch { .. }) => Some(Arc::clone(only)),
                _ => Some(Arc::new(Node::Branch {
                    bitmap: next_bitmap,
                    children: Arc::from(next_children),
                })),
            };

            RemoveResult {
                node: next_node,
                removed: child_result.removed,
                changed: true,
            }
        }
    }
}

fn merge_two<K, V>(
    left: Arc<Node<K, V>>,
    left_hash: u32,
    right: Arc<Node<K, V>>,
    right_hash: u32,
    shift: u32,
) -> Arc<Node<K, V>>
where
    K: Clone,
    V: Clone,
{
    debug_assert!(
        shift <= MAX_BRANCH_SHIFT,
        "branch nodes only exist at shifts 0..=30 for 32-bit hashes",
    );
    if left_hash == right_hash {
        let mut entries = Vec::new();
        collect_owned_entries(&left, &mut entries);
        collect_owned_entries(&right, &mut entries);
        return Arc::new(Node::Collision {
            hash: left_hash,
            entries: Arc::from(entries),
        });
    }

    let left_fragment = hash_fragment(left_hash, shift);
    let right_fragment = hash_fragment(right_hash, shift);
    let left_bit = bit_position(left_fragment);
    let right_bit = bit_position(right_fragment);

    if left_bit == right_bit {
        let child = merge_two(left, left_hash, right, right_hash, shift + BITS_PER_LEVEL);
        return Arc::new(Node::Branch {
            bitmap: left_bit,
            children: Arc::from(vec![child]),
        });
    }

    let (bitmap, children) = if left_fragment < right_fragment {
        (left_bit | right_bit, vec![left, right])
    } else {
        (left_bit | right_bit, vec![right, left])
    };

    Arc::new(Node::Branch {
        bitmap,
        children: Arc::from(children),
    })
}

fn collect_owned_entries<K, V>(node: &Node<K, V>, entries: &mut Vec<(K, V)>)
where
    K: Clone,
    V: Clone,
{
    match node {
        Node::Leaf { key, value, .. } => entries.push((key.clone(), value.clone())),
        Node::Collision {
            entries: bucket, ..
        } => entries.extend(bucket.iter().cloned()),
        Node::Branch { children, .. } => {
            for child in children.iter() {
                collect_owned_entries(child, entries);
            }
        }
    }
}

fn leaf_entry<K, V>(node: Arc<Node<K, V>>) -> (K, V) {
    match Arc::try_unwrap(node) {
        Ok(Node::Leaf { key, value, .. }) => (key, value),
        _ => unreachable!("newly allocated leaf must unwrap"),
    }
}

fn hash_fragment(hash: u32, shift: u32) -> u32 {
    (hash >> shift) & BRANCH_MASK
}

fn bit_position(fragment: u32) -> u32 {
    1_u32 << fragment
}

fn sparse_index(bitmap: u32, bit: u32) -> usize {
    (bitmap & (bit - 1)).count_ones() as usize
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::hash::{BuildHasherDefault, Hasher};
    use std::thread;

    #[derive(Default)]
    struct ConstantHasher;

    impl Hasher for ConstantHasher {
        fn finish(&self) -> u64 {
            0
        }

        fn write(&mut self, _bytes: &[u8]) {}
    }

    type ConstantState = BuildHasherDefault<ConstantHasher>;

    #[test]
    fn map_updates_preserve_old_versions() {
        let empty = PersistentHashMap::new();
        let one = empty.insert("a", 1);
        let two = one.insert("b", 2);
        let replaced = two.insert("a", 3);

        assert_eq!(empty.get(&"a"), None);
        assert_eq!(one.get(&"a"), Some(&1));
        assert_eq!(two.get(&"a"), Some(&1));
        assert_eq!(replaced.get(&"a"), Some(&3));
        assert_eq!(replaced.get(&"b"), Some(&2));
    }

    #[test]
    fn no_op_update_and_absent_remove_share_roots() {
        let map = PersistentHashMap::new().insert("a", 1).insert("b", 2);
        let same_value = map.insert("a", 1);
        let absent_removed = map.remove(&"c");

        assert!(map.shares_root_with(&same_value));
        assert!(map.shares_root_with(&absent_removed));
    }

    #[test]
    fn add_rejects_duplicates() {
        let map = PersistentHashMap::new().insert("a", 1);
        let (same, added) = map.try_add("a", 2);

        assert!(!added);
        assert!(map.shares_root_with(&same));
        assert!(matches!(map.add("a", 2), Err(DuplicateKey)));
    }

    #[test]
    fn collisions_are_stored_and_removed() {
        let map: PersistentHashMap<i32, i32, ConstantState> =
            PersistentHashMap::with_hasher(ConstantState::default())
                .insert(1, 10)
                .insert(2, 20)
                .insert(3, 30);

        assert_eq!(map.get(&1), Some(&10));
        assert_eq!(map.get(&2), Some(&20));
        assert_eq!(map.get(&3), Some(&30));

        let (removed, value) = map.try_remove(&2).unwrap();
        assert_eq!(value, 20);
        assert_eq!(removed.get(&1), Some(&10));
        assert_eq!(removed.get(&2), None);
        assert_eq!(removed.get(&3), Some(&30));
    }

    #[test]
    fn iterator_streams_entries_with_exact_remaining_count() {
        let map: PersistentHashMap<i32, i32, ConstantState> =
            PersistentHashMap::with_hasher(ConstantState::default())
                .set_items((0..64).map(|value| (value, value * value)));
        let mut iter = map.iter();
        let mut seen = Vec::new();

        for remaining in (1..=map.len()).rev() {
            assert_eq!(iter.size_hint(), (remaining, Some(remaining)));
            assert_eq!(iter.len(), remaining);
            let (key, value) = iter.next().expect("iterator has remaining entries");
            seen.push((*key, *value));
        }

        assert_eq!(iter.size_hint(), (0, Some(0)));
        assert_eq!(iter.len(), 0);
        assert_eq!(iter.next(), None);
        assert_eq!(
            seen,
            (0..64)
                .map(|value| (value, value * value))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn create_range_is_last_wins_and_retains_original_key_on_replace() {
        #[derive(Clone, Debug)]
        struct Key(&'static str, usize);

        impl Hash for Key {
            fn hash<H: Hasher>(&self, state: &mut H) {
                self.0.hash(state);
            }
        }

        impl PartialEq for Key {
            fn eq(&self, other: &Self) -> bool {
                self.0 == other.0
            }
        }

        impl Eq for Key {}

        let map = PersistentHashMap::new().set_items([
            (Key("x", 1), 10),
            (Key("x", 2), 20),
            (Key("y", 3), 30),
        ]);

        let (stored_key, value) = map.get_key_value(&Key("x", 99)).unwrap();
        assert_eq!(stored_key.1, 1);
        assert_eq!(*value, 20);
        assert_eq!(map.len(), 2);
    }

    #[test]
    fn set_algebra_uses_set_membership() {
        let left: PersistentHashSet<_> = [1, 2, 3].into_iter().collect();
        let right = [3, 4, 5];

        let union = left.union(right);
        assert!(union.set_equals([1, 2, 3, 4, 5]));

        let intersection = left.intersect([2, 3, 9]);
        assert!(intersection.set_equals([2, 3]));

        let except = left.except([1, 3]);
        assert!(except.set_equals([2]));

        let symmetric = left.symmetric_except([3, 4]);
        assert!(symmetric.set_equals([1, 2, 4]));
        assert!(intersection.is_proper_subset_of([1, 2, 3, 3]));
        assert!(left.is_proper_superset_of([1, 3, 3]));
        assert!(!left.is_proper_subset_of([1, 2, 3]));
        assert!(!left.is_proper_superset_of([1, 2, 3]));
    }

    #[test]
    fn except_and_symmetric_except_preserve_untouched_roots() {
        let set: PersistentHashSet<i32> = (0..64).collect();

        let except_nothing = set.except(std::iter::empty());
        assert!(set.shares_root_with(&except_nothing));

        let symmetric_nothing = set.symmetric_except(std::iter::empty());
        assert!(set.shares_root_with(&symmetric_nothing));

        let except = set.except([1, 63, 100]);
        assert_eq!(except.len(), 62);
        assert!(!except.contains(&1));
        assert!(!except.contains(&63));

        let symmetric = set.symmetric_except([0, 1, 64, 65]);
        assert_eq!(symmetric.len(), 64);
        assert!(!symmetric.contains(&0));
        assert!(symmetric.contains(&64));
        assert!(symmetric.contains(&65));
    }

    #[test]
    fn set_supports_into_iterator_and_debug_and_equality() {
        let set: PersistentHashSet<i32> = (0..8).collect();
        let mut collected = Vec::new();
        for value in &set {
            collected.push(*value);
        }
        collected.sort_unstable();
        assert_eq!(collected, (0..8).collect::<Vec<_>>());

        let same: PersistentHashSet<i32> = (0..8).rev().collect();
        assert_eq!(set, same);
        let different: PersistentHashSet<i32> = (0..9).collect();
        assert_ne!(set, different);

        let printed = format!("{:?}", PersistentHashSet::new().insert(5));
        assert_eq!(printed, "{5}");
    }

    #[test]
    fn map_supports_index_debug_and_equality() {
        let map = PersistentHashMap::new().insert("a", 1).insert("b", 2);
        assert_eq!(map[&"a"], 1);

        let same = PersistentHashMap::new().insert("b", 2).insert("a", 1);
        assert_eq!(map, same);
        assert_ne!(map, same.insert("a", 3));
        assert_ne!(map, same.remove(&"a"));

        let printed = format!("{:?}", PersistentHashMap::new().insert("a", 1));
        assert_eq!(printed, "{\"a\": 1}");
    }

    #[test]
    fn read_only_operations_do_not_require_value_bounds() {
        // Box<dyn Fn> is neither Clone nor PartialEq; construction, length,
        // lookup, and iteration must still work.
        let map: PersistentHashMap<&str, Box<dyn Fn() -> i32>> = PersistentHashMap::new();
        assert!(map.is_empty());
        assert!(map.get(&"missing").is_none());
        assert_eq!(map.iter().count(), 0);
    }

    #[test]
    fn from_iterator_supports_custom_default_hashers() {
        let map: PersistentHashMap<i32, i32, ConstantState> =
            (0..16).map(|value| (value, value)).collect();
        assert_eq!(map.len(), 16);

        let set: PersistentHashSet<i32, ConstantState> = (0..16).collect();
        assert_eq!(set.len(), 16);
        assert_eq!(set.hasher().hash_one(42), 0);
    }

    #[test]
    fn duplicate_key_error_propagates() {
        fn try_it() -> Result<(), Box<dyn std::error::Error>> {
            let map = PersistentHashMap::new().insert("a", 1);
            let _ = map.add("a", 2)?;
            Ok(())
        }

        let error = try_it().unwrap_err();
        assert_eq!(
            error.to_string(),
            "an entry with the same key already exists"
        );
    }

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn snapshots_are_send_sync_when_contents_are() {
        assert_send_sync::<PersistentHashMap<i32, i32>>();
        assert_send_sync::<PersistentHashSet<i32>>();
    }

    #[test]
    fn concurrent_readers_share_retained_snapshots() {
        let map = PersistentHashMap::new().set_items((0..256).map(|key| (key, key * 3 - 100)));
        let set: PersistentHashSet<_> = (0..256).collect();
        let expected_map = (0..256).map(|key| (key, key * 3 - 100)).collect::<Vec<_>>();

        let mut handles = Vec::new();
        for _ in 0..8 {
            let map = map.clone();
            let set = set.clone();
            let expected_map = expected_map.clone();
            handles.push(thread::spawn(move || {
                for _ in 0..128 {
                    assert_eq!(map.len(), 256);
                    assert_eq!(map.get(&128), Some(&284));
                    let mut entries = map
                        .iter()
                        .map(|(key, value)| (*key, *value))
                        .collect::<Vec<_>>();
                    entries.sort_unstable();
                    assert_eq!(entries, expected_map);

                    assert_eq!(set.len(), 256);
                    assert!(set.contains(&200));
                    let mut values = set.iter().copied().collect::<Vec<_>>();
                    values.sort_unstable();
                    assert_eq!(values, (0..256).collect::<Vec<_>>());
                }
            }));
        }

        for handle in handles {
            handle.join().expect("reader thread failed");
        }
    }
}
