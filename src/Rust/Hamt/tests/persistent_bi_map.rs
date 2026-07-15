use std::collections::HashMap;
use std::collections::hash_map::{DefaultHasher, RandomState};
use std::hash::{BuildHasher, Hash, Hasher};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;

use tools_data_structures_hamt::{BiMapConflict, PersistentBiMap};

#[derive(Clone, Debug)]
struct Representative {
    logical: &'static str,
    spelling: &'static str,
}

impl PartialEq for Representative {
    fn eq(&self, other: &Self) -> bool {
        self.logical == other.logical
    }
}

impl Eq for Representative {}

impl Hash for Representative {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.logical.hash(state);
    }
}

fn representative(logical: &'static str, spelling: &'static str) -> Representative {
    Representative { logical, spelling }
}

#[derive(Clone, Debug)]
struct SeededBuildHasher(u64);

impl BuildHasher for SeededBuildHasher {
    type Hasher = DefaultHasher;

    fn build_hasher(&self) -> Self::Hasher {
        let mut hasher = DefaultHasher::new();
        hasher.write_u64(self.0);
        hasher
    }
}

#[derive(Clone)]
struct PanickingBuildHasher {
    should_panic: Arc<AtomicBool>,
}

struct PanickingHasher {
    should_panic: Arc<AtomicBool>,
}

impl BuildHasher for PanickingBuildHasher {
    type Hasher = PanickingHasher;

    fn build_hasher(&self) -> Self::Hasher {
        PanickingHasher {
            should_panic: Arc::clone(&self.should_panic),
        }
    }
}

impl Hasher for PanickingHasher {
    fn finish(&self) -> u64 {
        0
    }

    fn write(&mut self, _bytes: &[u8]) {
        assert!(
            !self.should_panic.load(Ordering::Relaxed),
            "injected value-domain hash failure"
        );
    }
}

#[test]
fn strict_add_reports_key_first_and_preserves_conflict_roots() {
    let source = PersistentBiMap::new().add(1, "one").unwrap();

    let key_conflict = source.try_add(1, "uno");
    assert!(!key_conflict.added);
    assert_eq!(key_conflict.conflict, Some(BiMapConflict::Key));
    assert!(key_conflict.map.shares_roots_with(&source));

    let value_conflict = source.try_add(2, "one");
    assert!(!value_conflict.added);
    assert_eq!(value_conflict.conflict, Some(BiMapConflict::Value));
    assert!(value_conflict.map.shares_roots_with(&source));

    let same_pair = source.try_add(1, "one");
    assert_eq!(same_pair.conflict, Some(BiMapConflict::Key));
    assert!(matches!(source.add(1, "one"), Err(BiMapConflict::Key)));
    assert!(source.validate_structure());
}

#[test]
fn independent_hashers_and_eq_types_retain_first_representatives() {
    let source = PersistentBiMap::with_hashers(SeededBuildHasher(17), SeededBuildHasher(29))
        .add(
            representative("alpha", "Alpha"),
            representative("one", "One"),
        )
        .unwrap();
    assert_eq!(source.key_hasher().0, 17);
    assert_eq!(source.value_hasher().0, 29);

    let equivalent = source
        .set(
            representative("alpha", "ALPHA"),
            representative("one", "ONE"),
        )
        .unwrap();
    assert!(equivalent.shares_roots_with(&source));
    assert_eq!(
        equivalent
            .get(&representative("alpha", "lookup"))
            .unwrap()
            .spelling,
        "One"
    );
    assert_eq!(
        equivalent
            .get_key(&representative("one", "lookup"))
            .unwrap()
            .spelling,
        "Alpha"
    );

    let replaced = source
        .set(
            representative("alpha", "ignored"),
            representative("two", "Two"),
        )
        .unwrap();
    assert_eq!(
        replaced
            .get_key(&representative("two", "lookup"))
            .unwrap()
            .spelling,
        "Alpha"
    );
    assert!(!replaced.contains_value(&representative("one", "lookup")));
    assert!(replaced.validate_structure());
}

#[test]
fn set_never_displaces_another_key() {
    let source = PersistentBiMap::new()
        .add(1, "one")
        .unwrap()
        .add(2, "two")
        .unwrap();
    assert!(matches!(source.set(1, "two"), Err(BiMapConflict::Value)));
    assert_eq!(source.get(&1), Some(&"one"));
    assert_eq!(source.get_key(&"two"), Some(&2));

    let replaced = source.set(1, "uno").unwrap();
    assert_eq!(replaced.get(&1), Some(&"uno"));
    assert_eq!(replaced.get_key(&"uno"), Some(&1));
    assert!(!replaced.contains_value(&"one"));
    assert_eq!(source.get(&1), Some(&"one"));
}

#[test]
fn inverse_and_symmetric_removal_share_roots_and_return_opposite_representatives() {
    let source = PersistentBiMap::new()
        .add(1, "one")
        .unwrap()
        .add(2, "two")
        .unwrap();
    let inverse = source.inverse();
    assert_eq!(inverse.get(&"one"), Some(&1));
    assert!(inverse.inverse().shares_roots_with(&source));

    let removed_key = source.try_remove_key(&1);
    assert_eq!(removed_key.removed, Some("one"));
    assert!(!removed_key.map.contains_key(&1));
    assert!(!removed_key.map.contains_value(&"one"));

    let removed_value = source.try_remove_value(&"two");
    assert_eq!(removed_value.removed, Some(2));
    assert!(!removed_value.map.contains_key(&2));
    assert!(!removed_value.map.contains_value(&"two"));

    let miss = source.try_remove_key(&99);
    assert_eq!(miss.removed, None);
    assert!(miss.map.shares_roots_with(&source));
}

#[test]
fn optional_opposite_values_are_presence_safe_and_clear_preserves_hashers() {
    let source =
        PersistentBiMap::<i32, Option<&str>, SeededBuildHasher, SeededBuildHasher>::with_hashers(
            SeededBuildHasher(3),
            SeededBuildHasher(5),
        )
        .add(1, None)
        .unwrap();
    assert_eq!(source.get(&1), Some(&None));
    assert_eq!(source.get_key(&None), Some(&1));

    let removal = source.try_remove_key(&1);
    assert_eq!(removal.removed, Some(None));
    let cleared = source.clear();
    assert!(cleared.is_empty());
    assert_eq!(cleared.key_hasher().0, 3);
    assert_eq!(cleared.value_hasher().0, 5);
    assert!(cleared.clear().shares_roots_with(&cleared));
}

#[test]
fn generated_history_matches_two_hash_map_model_and_retains_snapshots() {
    let mut map = PersistentBiMap::new();
    let mut forward = HashMap::new();
    let mut inverse = HashMap::new();
    let mut snapshots = Vec::new();
    let mut state = 0xB1A4_D00D_u64;

    for step in 0..2_000 {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1);
        let operation = (state >> 32) as usize % 4;
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1);
        let key = ((state >> 32) % 32) as i32;
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1);
        let value = 100 + ((state >> 32) % 32) as i32;

        match operation {
            0 => {
                let result = map.try_add(key, value);
                let expected = !forward.contains_key(&key) && !inverse.contains_key(&value);
                assert_eq!(result.added, expected);
                if expected {
                    map = result.map;
                    forward.insert(key, value);
                    inverse.insert(value, key);
                } else {
                    assert_eq!(
                        result.conflict,
                        Some(if forward.contains_key(&key) {
                            BiMapConflict::Key
                        } else {
                            BiMapConflict::Value
                        })
                    );
                }
            }
            1 => {
                let old = forward.get(&key).copied();
                if inverse.get(&value).is_some_and(|owner| *owner != key) {
                    assert!(matches!(map.set(key, value), Err(BiMapConflict::Value)));
                } else {
                    map = map.set(key, value).unwrap();
                    if let Some(old_value) = old {
                        inverse.remove(&old_value);
                    }
                    forward.insert(key, value);
                    inverse.insert(value, key);
                }
            }
            2 => {
                let expected = forward.get(&key).copied();
                let result = map.try_remove_key(&key);
                assert_eq!(result.removed, expected);
                if let Some(old_value) = expected {
                    forward.remove(&key);
                    inverse.remove(&old_value);
                    map = result.map;
                }
            }
            _ => {
                let expected = inverse.get(&value).copied();
                let result = map.try_remove_value(&value);
                assert_eq!(result.removed, expected);
                if let Some(old_key) = expected {
                    inverse.remove(&value);
                    forward.remove(&old_key);
                    map = result.map;
                }
            }
        }

        assert_eq!(map.len(), forward.len());
        assert!(map.validate_structure());
        for (expected_key, expected_value) in &forward {
            assert_eq!(map.get(expected_key), Some(expected_value));
            assert_eq!(map.get_key(expected_value), Some(expected_key));
        }
        if step % 127 == 0 {
            snapshots.push(map.clone());
        }
    }

    assert!(snapshots.iter().all(PersistentBiMap::validate_structure));
}

#[test]
fn hash_panic_is_failure_atomic() {
    let should_panic = Arc::new(AtomicBool::new(false));
    let source = PersistentBiMap::<i32, i32, RandomState, PanickingBuildHasher>::with_hashers(
        RandomState::new(),
        PanickingBuildHasher {
            should_panic: Arc::clone(&should_panic),
        },
    )
    .add(1, 10)
    .unwrap();
    let before = source.clone();

    should_panic.store(true, Ordering::Relaxed);
    assert!(catch_unwind(AssertUnwindSafe(|| source.add(2, 20))).is_err());
    should_panic.store(false, Ordering::Relaxed);

    assert!(source.shares_roots_with(&before));
    assert_eq!(source.get(&1), Some(&10));
    assert!(source.validate_structure());
}

#[test]
fn retained_snapshot_supports_concurrent_readers() {
    let map = Arc::new(
        (0..512)
            .try_fold(PersistentBiMap::new(), |map, value| {
                map.add(value, value + 10_000)
            })
            .unwrap(),
    );
    let mut readers = Vec::new();
    for _ in 0..4 {
        let snapshot = Arc::clone(&map);
        readers.push(thread::spawn(move || {
            for _ in 0..50 {
                for value in 0..512 {
                    assert_eq!(snapshot.get(&value), Some(&(value + 10_000)));
                    assert_eq!(snapshot.get_key(&(value + 10_000)), Some(&value));
                }
                assert!(snapshot.validate_structure());
            }
        }));
    }
    for reader in readers {
        reader.join().unwrap();
    }
}
