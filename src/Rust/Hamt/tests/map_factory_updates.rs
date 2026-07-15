use std::cell::Cell;
use std::collections::BTreeMap;
use std::hash::{BuildHasher, BuildHasherDefault, Hash, Hasher};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use tools_data_structures_hamt::PersistentHashMap;

#[derive(Clone, Debug)]
struct Representative {
    class: i32,
    label: &'static str,
}

impl Hash for Representative {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.class.hash(state);
    }
}

impl PartialEq for Representative {
    fn eq(&self, other: &Self) -> bool {
        self.class == other.class
    }
}

impl Eq for Representative {}

#[derive(Clone, Default)]
struct CountingState {
    builds: Arc<AtomicUsize>,
}

impl CountingState {
    fn reset(&self) {
        self.builds.store(0, Ordering::Relaxed);
    }

    fn count(&self) -> usize {
        self.builds.load(Ordering::Relaxed)
    }
}

impl BuildHasher for CountingState {
    type Hasher = FnvHasher;

    fn build_hasher(&self) -> Self::Hasher {
        self.builds.fetch_add(1, Ordering::Relaxed);
        FnvHasher::default()
    }
}

struct FnvHasher(u64);

impl Default for FnvHasher {
    fn default() -> Self {
        Self(0xcbf2_9ce4_8422_2325)
    }
}

impl Hasher for FnvHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        for byte in bytes {
            self.0 ^= u64::from(*byte);
            self.0 = self.0.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
}

#[derive(Default)]
struct ConstantHasher;

impl Hasher for ConstantHasher {
    fn finish(&self) -> u64 {
        0
    }

    fn write(&mut self, _bytes: &[u8]) {}
}

type ConstantState = BuildHasherDefault<ConstantHasher>;

#[derive(Default)]
struct IdentityHasher(u64);

impl Hasher for IdentityHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, _bytes: &[u8]) {}

    fn write_u32(&mut self, value: u32) {
        self.0 = u64::from(value);
    }
}

type IdentityState = BuildHasherDefault<IdentityHasher>;

#[test]
fn get_or_add_hashes_and_descends_once_and_returns_the_selected_value() {
    let state = CountingState::default();
    let stored_value = Arc::new(10);
    let map = PersistentHashMap::with_hasher(state.clone()).insert(
        Representative {
            class: 1,
            label: "stored",
        },
        Arc::clone(&stored_value),
    );

    state.reset();
    let factory_calls = Cell::new(0);
    let hit = map.get_or_add(
        Representative {
            class: 1,
            label: "lookup",
        },
        |_| {
            factory_calls.set(factory_calls.get() + 1);
            Arc::new(99)
        },
    );
    assert_eq!(state.count(), 1);
    assert_eq!(factory_calls.get(), 0);
    assert!(map.shares_root_with(&hit.map));
    assert!(Arc::ptr_eq(&hit.value, &stored_value));
    let (stored_key, stored_again) = hit
        .map
        .get_key_value(&Representative {
            class: 1,
            label: "probe",
        })
        .expect("stored class");
    assert_eq!(stored_key.label, "stored");
    assert!(Arc::ptr_eq(stored_again, &stored_value));

    state.reset();
    let produced = Arc::new(20);
    let miss = map.get_or_add(
        Representative {
            class: 2,
            label: "added",
        },
        |caller| {
            assert_eq!(caller.label, "added");
            factory_calls.set(factory_calls.get() + 1);
            Arc::clone(&produced)
        },
    );
    assert_eq!(state.count(), 1);
    assert_eq!(factory_calls.get(), 1);
    assert_eq!(miss.map.len(), 2);
    assert!(Arc::ptr_eq(&miss.value, &produced));
    assert!(Arc::ptr_eq(
        miss.map
            .get(&Representative {
                class: 2,
                label: "probe",
            })
            .expect("added class"),
        &produced,
    ));
    assert_eq!(map.len(), 1);
}

#[test]
fn add_or_update_selects_exactly_one_factory_and_retains_representatives() {
    let state = CountingState::default();
    let stored_value = Arc::new(10);
    let map = PersistentHashMap::with_hasher(state.clone()).insert(
        Representative {
            class: 1,
            label: "stored",
        },
        Arc::clone(&stored_value),
    );

    state.reset();
    let add_calls = Cell::new(0);
    let update_calls = Cell::new(0);
    let replacement = Arc::new(11);
    let changed = map.add_or_update(
        Representative {
            class: 1,
            label: "lookup",
        },
        |_| {
            add_calls.set(add_calls.get() + 1);
            Arc::new(100)
        },
        |caller, current| {
            update_calls.set(update_calls.get() + 1);
            assert_eq!(caller.label, "lookup");
            assert!(Arc::ptr_eq(current, &stored_value));
            Arc::clone(&replacement)
        },
    );
    assert_eq!(state.count(), 1);
    assert_eq!(add_calls.get(), 0);
    assert_eq!(update_calls.get(), 1);
    assert!(Arc::ptr_eq(&changed.value, &replacement));
    let (changed_key, changed_value) = changed
        .map
        .get_key_value(&Representative {
            class: 1,
            label: "probe",
        })
        .expect("updated class");
    assert_eq!(changed_key.label, "stored");
    assert!(Arc::ptr_eq(changed_value, &replacement));
    assert!(Arc::ptr_eq(
        map.get(&Representative {
            class: 1,
            label: "probe",
        })
        .expect("source class"),
        &stored_value,
    ));

    state.reset();
    let equal_but_distinct = Arc::new(10);
    let no_op = map.add_or_update(
        Representative {
            class: 1,
            label: "second lookup",
        },
        |_| panic!("the add factory must not run on a hit"),
        |caller, current| {
            assert_eq!(caller.label, "second lookup");
            assert!(Arc::ptr_eq(current, &stored_value));
            Arc::clone(&equal_but_distinct)
        },
    );
    assert_eq!(state.count(), 1);
    assert!(map.shares_root_with(&no_op.map));
    assert!(Arc::ptr_eq(&no_op.value, &stored_value));

    state.reset();
    let added_value = Arc::new(30);
    let added = map.add_or_update(
        Representative {
            class: 3,
            label: "new caller",
        },
        |caller| {
            assert_eq!(caller.label, "new caller");
            Arc::clone(&added_value)
        },
        |_, _| panic!("the update factory must not run on a miss"),
    );
    assert_eq!(state.count(), 1);
    assert_eq!(added.map.len(), 2);
    assert!(Arc::ptr_eq(&added.value, &added_value));
}

#[test]
fn factory_updates_cover_collision_nodes_and_preserve_failure_atomicity() {
    let map: PersistentHashMap<i32, Arc<i32>, ConstantState> =
        PersistentHashMap::with_hasher(ConstantState::default())
            .insert(1, Arc::new(10))
            .insert(2, Arc::new(20))
            .insert(3, Arc::new(30));
    let before = map.clone();

    let updated = map.add_or_update(
        2,
        |_| panic!("collision hit must not add"),
        |caller, current| {
            assert_eq!((*caller, **current), (2, 20));
            Arc::new(21)
        },
    );
    assert_eq!(**updated.map.get(&2).expect("updated collision entry"), 21);
    assert_eq!(**map.get(&2).expect("source collision entry"), 20);

    let added = map.get_or_add(4, |caller| Arc::new(*caller * 10));
    assert_eq!(**added.map.get(&4).expect("new collision entry"), 40);
    assert_eq!(map.get(&4), None);

    let update_panic = catch_unwind(AssertUnwindSafe(|| {
        map.add_or_update(
            2,
            |_| Arc::new(0),
            |_, _| -> Arc<i32> { panic!("intentional update failure") },
        )
    }));
    assert!(update_panic.is_err());
    assert!(map.shares_root_with(&before));
    assert_eq!(**map.get(&2).expect("unchanged source"), 20);

    let add_panic = catch_unwind(AssertUnwindSafe(|| {
        map.get_or_add(99, |_| -> Arc<i32> { panic!("intentional add failure") })
    }));
    assert!(add_panic.is_err());
    assert!(map.shares_root_with(&before));
    assert_eq!(map.len(), 3);
}

#[test]
fn factory_updates_recurse_through_branch_children() {
    // The hashes share the low five-bit root fragment and diverge in its child.
    let map: PersistentHashMap<u32, i32, IdentityState> =
        PersistentHashMap::with_hasher(IdentityState::default())
            .insert(0, 10)
            .insert(32, 20);

    let updated = map.add_or_update(
        32,
        |_| panic!("nested branch hit must not add"),
        |key, current| {
            assert_eq!((*key, *current), (32, 20));
            21
        },
    );
    assert_eq!(updated.value, 21);
    assert_eq!(updated.map.get(&0), Some(&10));
    assert_eq!(updated.map.get(&32), Some(&21));
    assert_eq!(map.get(&32), Some(&20));

    let added = updated.map.get_or_add(64, |key| (*key / 32) as i32 * 10);
    assert_eq!(added.value, 20);
    assert_eq!(added.map.get(&0), Some(&10));
    assert_eq!(added.map.get(&32), Some(&21));
    assert_eq!(added.map.get(&64), Some(&20));

    let no_op = added.map.add_or_update(64, |_| 0, |_, current| *current);
    assert!(added.map.shares_root_with(&no_op.map));
}

#[test]
fn collision_heavy_factory_updates_match_a_deterministic_model() {
    let mut map: PersistentHashMap<i32, i32, ConstantState> =
        PersistentHashMap::with_hasher(ConstantState::default());
    let mut model = BTreeMap::new();
    let mut random = 0x9e37_79b9_7f4a_7c15_u64;

    for step in 0..4_096_u64 {
        random ^= random << 7;
        random ^= random >> 9;
        random ^= random << 8;
        let key = ((random >> 17) % 97) as i32 - 48;
        let supplied = ((random >> 33) % 2_003) as i32 - 1_001;

        if (random ^ step) & 1 == 0 {
            let was_present = model.contains_key(&key);
            let calls = Cell::new(0);
            let expected = *model.entry(key).or_insert(supplied);
            let result = map.get_or_add(key, |_| {
                calls.set(calls.get() + 1);
                supplied
            });
            assert_eq!(result.value, expected);
            assert_eq!(calls.get(), usize::from(!was_present));
            map = result.map;
        } else {
            let was_present = model.contains_key(&key);
            let expected = if let Some(current) = model.get_mut(&key) {
                *current = current.wrapping_add(supplied);
                *current
            } else {
                model.insert(key, supplied);
                supplied
            };
            let add_calls = Cell::new(0);
            let update_calls = Cell::new(0);
            let result = map.add_or_update(
                key,
                |_| {
                    add_calls.set(add_calls.get() + 1);
                    supplied
                },
                |_, current| {
                    update_calls.set(update_calls.get() + 1);
                    current.wrapping_add(supplied)
                },
            );
            assert_eq!(result.value, expected);
            assert_eq!(add_calls.get(), usize::from(!was_present));
            assert_eq!(update_calls.get(), usize::from(was_present));
            map = result.map;
        }

        if step % 127 == 0 {
            assert_eq!(map.len(), model.len());
            for probe in -48..=48 {
                assert_eq!(map.get(&probe), model.get(&probe));
            }
        }
    }

    assert_eq!(map.len(), model.len());
    for (key, value) in model {
        assert_eq!(map.get(&key), Some(&value));
    }
}
