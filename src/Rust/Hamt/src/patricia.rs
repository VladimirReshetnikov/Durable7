use std::sync::Arc;

#[derive(Clone)]
enum Node<K, V> {
    Leaf {
        path: u64,
        key: K,
        value: V,
    },
    Branch {
        prefix: u64,
        mask: u64,
        left: Arc<Node<K, V>>,
        right: Arc<Node<K, V>>,
    },
}

#[derive(Clone)]
struct Core<K, V> {
    root: Option<Arc<Node<K, V>>>,
    len: usize,
    encode: fn(K) -> u64,
}

impl<K: Copy + Eq, V> Core<K, V> {
    fn new(encode: fn(K) -> u64) -> Self {
        Self {
            root: None,
            len: 0,
            encode,
        }
    }

    fn get(&self, key: K) -> Option<&V> {
        let path = (self.encode)(key);
        let mut node = self.root.as_deref()?;
        loop {
            match node {
                Node::Leaf {
                    path: leaf_path,
                    value,
                    ..
                } => return (*leaf_path == path).then_some(value),
                Node::Branch {
                    prefix,
                    mask,
                    left,
                    right,
                } => {
                    if prefix_of(path, *mask) != *prefix {
                        return None;
                    }
                    node = if path & mask == 0 { left } else { right };
                }
            }
        }
    }

    fn contains(&self, key: K) -> bool {
        self.get(key).is_some()
    }

    fn iter(&self) -> Iter<'_, K, V> {
        let mut stack = Vec::new();
        if let Some(root) = self.root.as_deref() {
            stack.push(root);
        }
        Iter { stack }
    }
}

impl<K: Copy + Eq, V: Clone + PartialEq> Core<K, V> {
    fn insert(&self, key: K, value: V) -> Self {
        let path = (self.encode)(key);
        let (root, added, changed) = insert_node(self.root.as_ref(), path, key, value);
        if !changed {
            return self.clone();
        }
        Self {
            root: Some(root),
            len: self.len + usize::from(added),
            encode: self.encode,
        }
    }

    fn remove(&self, key: K) -> Self {
        let (root, changed) = remove_node(self.root.as_ref(), (self.encode)(key));
        if !changed {
            return self.clone();
        }
        Self {
            root,
            len: self.len - 1,
            encode: self.encode,
        }
    }

    fn union(&self, other: &Self) -> Self {
        let root = union_nodes(self.root.as_ref(), other.root.as_ref());
        if same_root(&root, &self.root) {
            return self.clone();
        }
        Self {
            len: count(root.as_deref()),
            root,
            encode: self.encode,
        }
    }

    fn intersect(&self, other: &Self) -> Self {
        let root = intersect_nodes(self.root.as_ref(), other.root.as_ref());
        if same_root(&root, &self.root) {
            return self.clone();
        }
        Self {
            len: count(root.as_deref()),
            root,
            encode: self.encode,
        }
    }

    fn except(&self, other: &Self) -> Self {
        let root = except_nodes(self.root.as_ref(), other.root.as_ref());
        if same_root(&root, &self.root) {
            return self.clone();
        }
        Self {
            len: count(root.as_deref()),
            root,
            encode: self.encode,
        }
    }
}

pub struct Iter<'a, K, V> {
    stack: Vec<&'a Node<K, V>>,
}
impl<'a, K, V> Iterator for Iter<'a, K, V> {
    type Item = (&'a K, &'a V);
    fn next(&mut self) -> Option<Self::Item> {
        while let Some(node) = self.stack.pop() {
            match node {
                Node::Leaf { key, value, .. } => return Some((key, value)),
                Node::Branch { left, right, .. } => {
                    self.stack.push(right);
                    self.stack.push(left);
                }
            }
        }
        None
    }
}

fn insert_node<K: Copy + Eq, V: Clone + PartialEq>(
    node: Option<&Arc<Node<K, V>>>,
    path: u64,
    key: K,
    value: V,
) -> (Arc<Node<K, V>>, bool, bool) {
    let Some(node) = node else {
        return (Arc::new(Node::Leaf { path, key, value }), true, true);
    };
    match node.as_ref() {
        Node::Leaf {
            path: old_path,
            key: old_key,
            value: old_value,
        } => {
            if *old_path == path {
                if old_value == &value {
                    (Arc::clone(node), false, false)
                } else {
                    (
                        Arc::new(Node::Leaf {
                            path,
                            key: *old_key,
                            value,
                        }),
                        false,
                        true,
                    )
                }
            } else {
                (
                    join(
                        *old_path,
                        Arc::clone(node),
                        path,
                        Arc::new(Node::Leaf { path, key, value }),
                    ),
                    true,
                    true,
                )
            }
        }
        Node::Branch {
            prefix,
            mask,
            left,
            right,
        } => {
            if prefix_of(path, *mask) != *prefix {
                return (
                    join(
                        *prefix,
                        Arc::clone(node),
                        path,
                        Arc::new(Node::Leaf { path, key, value }),
                    ),
                    true,
                    true,
                );
            }
            if path & mask == 0 {
                let (child, added, changed) = insert_node(Some(left), path, key, value);
                if !changed {
                    (Arc::clone(node), false, false)
                } else {
                    (
                        Arc::new(Node::Branch {
                            prefix: *prefix,
                            mask: *mask,
                            left: child,
                            right: Arc::clone(right),
                        }),
                        added,
                        true,
                    )
                }
            } else {
                let (child, added, changed) = insert_node(Some(right), path, key, value);
                if !changed {
                    (Arc::clone(node), false, false)
                } else {
                    (
                        Arc::new(Node::Branch {
                            prefix: *prefix,
                            mask: *mask,
                            left: Arc::clone(left),
                            right: child,
                        }),
                        added,
                        true,
                    )
                }
            }
        }
    }
}

fn remove_node<K, V>(node: Option<&Arc<Node<K, V>>>, path: u64) -> (Option<Arc<Node<K, V>>>, bool) {
    let Some(node) = node else {
        return (None, false);
    };
    match node.as_ref() {
        Node::Leaf {
            path: leaf_path, ..
        } => {
            if *leaf_path == path {
                (None, true)
            } else {
                (Some(Arc::clone(node)), false)
            }
        }
        Node::Branch {
            prefix,
            mask,
            left,
            right,
        } => {
            if prefix_of(path, *mask) != *prefix {
                return (Some(Arc::clone(node)), false);
            }
            if path & mask == 0 {
                let (child, changed) = remove_node(Some(left), path);
                if !changed {
                    (Some(Arc::clone(node)), false)
                } else {
                    (
                        Some(child.map_or_else(
                            || Arc::clone(right),
                            |left| branch(*prefix, *mask, left, Arc::clone(right)),
                        )),
                        true,
                    )
                }
            } else {
                let (child, changed) = remove_node(Some(right), path);
                if !changed {
                    (Some(Arc::clone(node)), false)
                } else {
                    (
                        Some(child.map_or_else(
                            || Arc::clone(left),
                            |right| branch(*prefix, *mask, Arc::clone(left), right),
                        )),
                        true,
                    )
                }
            }
        }
    }
}

fn union_nodes<K: Copy + Eq, V: Clone + PartialEq>(
    left: Option<&Arc<Node<K, V>>>,
    right: Option<&Arc<Node<K, V>>>,
) -> Option<Arc<Node<K, V>>> {
    match (left, right) {
        (None, _) => right.cloned(),
        (_, None) => left.cloned(),
        (Some(left), Some(right)) if Arc::ptr_eq(left, right) => Some(Arc::clone(left)),
        (Some(left), Some(right)) => Some(match (left.as_ref(), right.as_ref()) {
            (Node::Leaf { path, key, value }, _) => {
                put_existing(right, *path, *key, value.clone(), true)
            }
            (_, Node::Leaf { path, key, value }) => {
                put_existing(left, *path, *key, value.clone(), false)
            }
            (
                Node::Branch {
                    prefix: lp,
                    mask: lm,
                    left: ll,
                    right: lr,
                },
                Node::Branch {
                    prefix: rp,
                    mask: rm,
                    left: rl,
                    right: rr,
                },
            ) => {
                if lm == rm && lp == rp {
                    branch(
                        *lp,
                        *lm,
                        union_nodes(Some(ll), Some(rl)).unwrap(),
                        union_nodes(Some(lr), Some(rr)).unwrap(),
                    )
                } else if lm > rm && prefix_of(*rp, *lm) == *lp {
                    if rp & lm == 0 {
                        branch(
                            *lp,
                            *lm,
                            union_nodes(Some(ll), Some(right)).unwrap(),
                            Arc::clone(lr),
                        )
                    } else {
                        branch(
                            *lp,
                            *lm,
                            Arc::clone(ll),
                            union_nodes(Some(lr), Some(right)).unwrap(),
                        )
                    }
                } else if rm > lm && prefix_of(*lp, *rm) == *rp {
                    if lp & rm == 0 {
                        branch(
                            *rp,
                            *rm,
                            union_nodes(Some(left), Some(rl)).unwrap(),
                            Arc::clone(rr),
                        )
                    } else {
                        branch(
                            *rp,
                            *rm,
                            Arc::clone(rl),
                            union_nodes(Some(left), Some(rr)).unwrap(),
                        )
                    }
                } else {
                    join(*lp, Arc::clone(left), *rp, Arc::clone(right))
                }
            }
        }),
    }
}

fn put_existing<K: Copy + Eq, V: Clone + PartialEq>(
    node: &Arc<Node<K, V>>,
    path: u64,
    key: K,
    value: V,
    prefer_existing: bool,
) -> Arc<Node<K, V>> {
    match node.as_ref() {
        Node::Leaf {
            path: old,
            key: old_key,
            ..
        } if *old == path => {
            if prefer_existing {
                Arc::clone(node)
            } else {
                Arc::new(Node::Leaf {
                    path,
                    key: *old_key,
                    value,
                })
            }
        }
        Node::Leaf { path: old, .. } => join(
            *old,
            Arc::clone(node),
            path,
            Arc::new(Node::Leaf { path, key, value }),
        ),
        Node::Branch {
            prefix,
            mask,
            left,
            right,
        } => {
            if prefix_of(path, *mask) != *prefix {
                return join(
                    *prefix,
                    Arc::clone(node),
                    path,
                    Arc::new(Node::Leaf { path, key, value }),
                );
            }
            if path & mask == 0 {
                branch(
                    *prefix,
                    *mask,
                    put_existing(left, path, key, value, prefer_existing),
                    Arc::clone(right),
                )
            } else {
                branch(
                    *prefix,
                    *mask,
                    Arc::clone(left),
                    put_existing(right, path, key, value, prefer_existing),
                )
            }
        }
    }
}

fn intersect_nodes<K: Copy + Eq, V: Clone + PartialEq>(
    left: Option<&Arc<Node<K, V>>>,
    right: Option<&Arc<Node<K, V>>>,
) -> Option<Arc<Node<K, V>>> {
    let (Some(left), Some(right)) = (left, right) else {
        return None;
    };
    if Arc::ptr_eq(left, right) {
        return Some(Arc::clone(left));
    }
    match (left.as_ref(), right.as_ref()) {
        (Node::Leaf { path, .. }, _) => contains_path(right, *path).then(|| Arc::clone(left)),
        (_, Node::Leaf { path, .. }) => find_leaf(left, *path),
        (
            Node::Branch {
                prefix: lp,
                mask: lm,
                left: ll,
                right: lr,
            },
            Node::Branch {
                prefix: rp,
                mask: rm,
                left: rl,
                right: rr,
            },
        ) => {
            if lm == rm && lp == rp {
                collapse(
                    *lp,
                    *lm,
                    intersect_nodes(Some(ll), Some(rl)),
                    intersect_nodes(Some(lr), Some(rr)),
                )
            } else if lm > rm && prefix_of(*rp, *lm) == *lp {
                if rp & lm == 0 {
                    intersect_nodes(Some(ll), Some(right))
                } else {
                    intersect_nodes(Some(lr), Some(right))
                }
            } else if rm > lm && prefix_of(*lp, *rm) == *rp {
                if lp & rm == 0 {
                    intersect_nodes(Some(left), Some(rl))
                } else {
                    intersect_nodes(Some(left), Some(rr))
                }
            } else {
                None
            }
        }
    }
}

fn except_nodes<K: Copy + Eq, V: Clone + PartialEq>(
    left: Option<&Arc<Node<K, V>>>,
    right: Option<&Arc<Node<K, V>>>,
) -> Option<Arc<Node<K, V>>> {
    let Some(left) = left else {
        return None;
    };
    let Some(right) = right else {
        return Some(Arc::clone(left));
    };
    if Arc::ptr_eq(left, right) {
        return None;
    }
    match (left.as_ref(), right.as_ref()) {
        (Node::Leaf { path, .. }, _) => (!contains_path(right, *path)).then(|| Arc::clone(left)),
        (_, Node::Leaf { path, .. }) => remove_path(left, *path),
        (
            Node::Branch {
                prefix: lp,
                mask: lm,
                left: ll,
                right: lr,
            },
            Node::Branch {
                prefix: rp,
                mask: rm,
                left: rl,
                right: rr,
            },
        ) => {
            if lm == rm && lp == rp {
                collapse(
                    *lp,
                    *lm,
                    except_nodes(Some(ll), Some(rl)),
                    except_nodes(Some(lr), Some(rr)),
                )
            } else if lm > rm && prefix_of(*rp, *lm) == *lp {
                if rp & lm == 0 {
                    collapse(
                        *lp,
                        *lm,
                        except_nodes(Some(ll), Some(right)),
                        Some(Arc::clone(lr)),
                    )
                } else {
                    collapse(
                        *lp,
                        *lm,
                        Some(Arc::clone(ll)),
                        except_nodes(Some(lr), Some(right)),
                    )
                }
            } else if rm > lm && prefix_of(*lp, *rm) == *rp {
                if lp & rm == 0 {
                    except_nodes(Some(left), Some(rl))
                } else {
                    except_nodes(Some(left), Some(rr))
                }
            } else {
                Some(Arc::clone(left))
            }
        }
    }
}

fn find_leaf<K, V>(node: &Arc<Node<K, V>>, path: u64) -> Option<Arc<Node<K, V>>> {
    match node.as_ref() {
        Node::Leaf { path: found, .. } => (*found == path).then(|| Arc::clone(node)),
        Node::Branch {
            prefix,
            mask,
            left,
            right,
        } if prefix_of(path, *mask) == *prefix => {
            find_leaf(if path & mask == 0 { left } else { right }, path)
        }
        _ => None,
    }
}
fn contains_path<K, V>(node: &Arc<Node<K, V>>, path: u64) -> bool {
    find_leaf(node, path).is_some()
}
fn remove_path<K, V>(node: &Arc<Node<K, V>>, path: u64) -> Option<Arc<Node<K, V>>> {
    match node.as_ref() {
        Node::Leaf { path: found, .. } => (*found != path).then(|| Arc::clone(node)),
        Node::Branch {
            prefix,
            mask,
            left,
            right,
        } if prefix_of(path, *mask) == *prefix => {
            if path & mask == 0 {
                collapse(
                    *prefix,
                    *mask,
                    remove_path(left, path),
                    Some(Arc::clone(right)),
                )
            } else {
                collapse(
                    *prefix,
                    *mask,
                    Some(Arc::clone(left)),
                    remove_path(right, path),
                )
            }
        }
        _ => Some(Arc::clone(node)),
    }
}
fn collapse<K, V>(
    prefix: u64,
    mask: u64,
    left: Option<Arc<Node<K, V>>>,
    right: Option<Arc<Node<K, V>>>,
) -> Option<Arc<Node<K, V>>> {
    match (left, right) {
        (None, right) => right,
        (left, None) => left,
        (Some(left), Some(right)) => Some(branch(prefix, mask, left, right)),
    }
}
fn branch<K, V>(
    prefix: u64,
    mask: u64,
    left: Arc<Node<K, V>>,
    right: Arc<Node<K, V>>,
) -> Arc<Node<K, V>> {
    Arc::new(Node::Branch {
        prefix,
        mask,
        left,
        right,
    })
}
fn join<K, V>(
    left_path: u64,
    left: Arc<Node<K, V>>,
    right_path: u64,
    right: Arc<Node<K, V>>,
) -> Arc<Node<K, V>> {
    let mask = 1_u64 << (63 - (left_path ^ right_path).leading_zeros());
    let prefix = prefix_of(left_path, mask);
    if left_path & mask == 0 {
        branch(prefix, mask, left, right)
    } else {
        branch(prefix, mask, right, left)
    }
}
fn prefix_of(path: u64, mask: u64) -> u64 {
    path & !mask.wrapping_shl(1).wrapping_sub(1)
}
fn count<K, V>(node: Option<&Node<K, V>>) -> usize {
    match node {
        None => 0,
        Some(Node::Leaf { .. }) => 1,
        Some(Node::Branch { left, right, .. }) => count(Some(left)) + count(Some(right)),
    }
}
fn same_root<K, V>(left: &Option<Arc<Node<K, V>>>, right: &Option<Arc<Node<K, V>>>) -> bool {
    match (left, right) {
        (None, None) => true,
        (Some(l), Some(r)) => Arc::ptr_eq(l, r),
        _ => false,
    }
}

macro_rules! map_type {
    ($name:ident, $key:ty, $encode:expr) => {
        #[derive(Clone)]
        pub struct $name<V> {
            core: Core<$key, V>,
        }
        impl<V> $name<V> {
            #[must_use]
            pub fn new() -> Self {
                Self {
                    core: Core::new($encode),
                }
            }
            #[must_use]
            pub fn len(&self) -> usize {
                self.core.len
            }
            #[must_use]
            pub fn is_empty(&self) -> bool {
                self.core.len == 0
            }
            #[must_use]
            pub fn get(&self, key: $key) -> Option<&V> {
                self.core.get(key)
            }
            #[must_use]
            pub fn contains_key(&self, key: $key) -> bool {
                self.core.contains(key)
            }
            #[must_use]
            pub fn shares_root_with(&self, other: &Self) -> bool {
                same_root(&self.core.root, &other.core.root)
            }
            pub fn iter(&self) -> impl Iterator<Item = (&$key, &V)> {
                self.core.iter()
            }
        }
        impl<V: Clone + PartialEq> $name<V> {
            #[must_use]
            pub fn insert(&self, key: $key, value: V) -> Self {
                Self {
                    core: self.core.insert(key, value),
                }
            }
            #[must_use]
            pub fn remove(&self, key: $key) -> Self {
                Self {
                    core: self.core.remove(key),
                }
            }
            #[must_use]
            pub fn union(&self, other: &Self) -> Self {
                Self {
                    core: self.core.union(&other.core),
                }
            }
            #[must_use]
            pub fn intersect(&self, other: &Self) -> Self {
                Self {
                    core: self.core.intersect(&other.core),
                }
            }
            #[must_use]
            pub fn except(&self, other: &Self) -> Self {
                Self {
                    core: self.core.except(&other.core),
                }
            }
        }
        impl<V> Default for $name<V> {
            fn default() -> Self {
                Self::new()
            }
        }
    };
}
map_type!(PersistentIntMap, i32, |key: i32| (key as u32 ^ 0x8000_0000)
    as u64);
map_type!(PersistentLongMap, i64, |key: i64| key as u64
    ^ 0x8000_0000_0000_0000);

macro_rules! set_type {
    ($name:ident, $map:ident, $key:ty) => {
        #[derive(Clone, Default)]
        pub struct $name {
            map: $map<()>,
        }
        impl $name {
            #[must_use]
            pub fn new() -> Self {
                Self::default()
            }
            #[must_use]
            pub fn len(&self) -> usize {
                self.map.len()
            }
            #[must_use]
            pub fn contains(&self, value: $key) -> bool {
                self.map.contains_key(value)
            }
            #[must_use]
            pub fn insert(&self, value: $key) -> Self {
                Self {
                    map: self.map.insert(value, ()),
                }
            }
            #[must_use]
            pub fn remove(&self, value: $key) -> Self {
                Self {
                    map: self.map.remove(value),
                }
            }
            #[must_use]
            pub fn union(&self, other: &Self) -> Self {
                Self {
                    map: self.map.union(&other.map),
                }
            }
            #[must_use]
            pub fn intersect(&self, other: &Self) -> Self {
                Self {
                    map: self.map.intersect(&other.map),
                }
            }
            #[must_use]
            pub fn except(&self, other: &Self) -> Self {
                Self {
                    map: self.map.except(&other.map),
                }
            }
            pub fn iter(&self) -> impl Iterator<Item = &$key> {
                self.map.iter().map(|(key, _)| key)
            }
        }
    };
}
set_type!(PersistentIntSet, PersistentIntMap, i32);
set_type!(PersistentLongSet, PersistentLongMap, i64);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signed_boundaries_iterate_in_order() {
        let mut ints = PersistentIntMap::new();
        for key in [i32::MAX, 1, 0, -1, i32::MIN] {
            ints = ints.insert(key, key.to_string());
        }
        assert_eq!(
            ints.iter().map(|(key, _)| *key).collect::<Vec<_>>(),
            [i32::MIN, -1, 0, 1, i32::MAX]
        );
        assert_eq!(ints.get(i32::MIN).map(String::as_str), Some("-2147483648"));

        let mut longs = PersistentLongMap::new();
        for key in [i64::MAX, 1, 0, -1, i64::MIN] {
            longs = longs.insert(key, key);
        }
        assert_eq!(
            longs.iter().map(|(key, _)| *key).collect::<Vec<_>>(),
            [i64::MIN, -1, 0, 1, i64::MAX]
        );
    }

    #[test]
    fn randomized_history_matches_btree_map() {
        let mut actual = PersistentIntMap::new();
        let mut expected = std::collections::BTreeMap::new();
        let mut state = 0x1234_abcd_u32;
        for _ in 0..10_000 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let key = ((state >> 8) % 401) as i32 - 200;
            if state & 3 == 0 {
                actual = actual.remove(key);
                expected.remove(&key);
            } else {
                actual = actual.insert(key, state);
                expected.insert(key, state);
            }
        }
        assert_eq!(
            actual.iter().map(|(k, v)| (*k, *v)).collect::<Vec<_>>(),
            expected.into_iter().collect::<Vec<_>>()
        );
        assert!(actual.remove(10_000).shares_root_with(&actual));
    }

    #[test]
    fn structural_map_and_set_algebra() {
        let left = PersistentIntMap::new().insert(1, "left").insert(2, "two");
        let right = PersistentIntMap::new()
            .insert(1, "right")
            .insert(3, "three");
        assert_eq!(
            left.union(&right)
                .iter()
                .map(|(k, v)| (*k, *v))
                .collect::<Vec<_>>(),
            [(1, "right"), (2, "two"), (3, "three")]
        );
        assert_eq!(
            left.intersect(&right)
                .iter()
                .map(|(k, v)| (*k, *v))
                .collect::<Vec<_>>(),
            [(1, "left")]
        );

        let left = [-3, -1, 1, 3]
            .into_iter()
            .fold(PersistentIntSet::new(), |set, value| set.insert(value));
        let right = [-1, 0, 1]
            .into_iter()
            .fold(PersistentIntSet::new(), |set, value| set.insert(value));
        assert_eq!(
            left.union(&right).iter().copied().collect::<Vec<_>>(),
            [-3, -1, 0, 1, 3]
        );
        assert_eq!(
            left.intersect(&right).iter().copied().collect::<Vec<_>>(),
            [-1, 1]
        );
        assert_eq!(
            left.except(&right).iter().copied().collect::<Vec<_>>(),
            [-3, 3]
        );
    }
}
