use crate::measured::SumMeasure;
use std::cell::RefCell;
use std::collections::HashSet;
use std::fmt;
use std::marker::PhantomData;
use std::ops::Add;
use std::rc::{Rc, Weak};

const BLOCK_CAPACITY: usize = 64;

/// Static monoid operations used by [`DabaLite`].
///
/// Implementations must provide an associative [`combine`](Self::combine) operation with
/// [`empty`](Self::empty) as its two-sided identity. The operation need not be commutative or
/// invertible. A callback may unwind; every mutating `DabaLite` operation plans all callback work
/// before publishing changes and therefore leaves the window unchanged when that happens. Side
/// effects performed by callbacks, including effects through interior mutability, cannot be rolled
/// back.
pub trait DabaMonoid<T> {
    /// Returns the monoid identity.
    fn empty() -> T;

    /// Combines the aggregate of an earlier FIFO segment with a later segment.
    fn combine(left: &T, right: &T) -> T;
}

impl<T> DabaMonoid<T> for SumMeasure<T>
where
    T: Add<Output = T> + Clone + Default,
{
    fn empty() -> T {
        T::default()
    }

    fn combine(left: &T, right: &T) -> T {
        left.clone() + right.clone()
    }
}

/// A mutable FIFO sliding-window aggregate with worst-case constant callback and structural work
/// per insertion, eviction, and query.
///
/// This is Tangwongsan, Hirzel, and Schneider's six-cursor DABA Lite algorithm. It stores the
/// logical queue in a linked sequence of fixed 64-slot chunks, performs one bounded reversal
/// fixup after every insertion or eviction, and never runs a reversal loop. `insert`,
/// [`try_evict`](Self::try_evict), and [`aggregate`](Self::aggregate) invoke
/// [`DabaMonoid::combine`] at most three, two, and one times respectively.
///
/// The type is deliberately ephemeral. It exposes neither the oldest raw value nor iteration,
/// because DABA Lite overwrites queue positions with partial aggregates. Its `Rc`-backed cursor
/// representation makes it neither `Send` nor `Sync`; keep an instance on one thread and use
/// ordinary exclusive `&mut self` access.
///
/// Unlike the tracing-GC C# implementation, [`clear`](Self::clear) is O(n + c) for `n` live values
/// in `c` chunks. Safe generic Rust must run the destructors of promptly released owned values;
/// deferring that work would violate the reclamation contract. All other headline operations retain
/// the paper's worst-case O(1) bound when the monoid callbacks themselves are O(1).
///
/// ```compile_fail
/// use durable7_fingertree::{DabaLite, SumMeasure};
///
/// fn require_send_and_sync<T: Send + Sync>() {}
/// require_send_and_sync::<DabaLite<i32, SumMeasure<i32>>>();
/// ```
pub struct DabaLite<T, M>
where
    M: DabaMonoid<T>,
{
    queue: ChunkedQueue<T>,
    f: Cursor<T>,
    l: Cursor<T>,
    r: Cursor<T>,
    a: Cursor<T>,
    b: Cursor<T>,
    e: Cursor<T>,
    aggregate_ra: Rc<T>,
    aggregate_b: Rc<T>,
    len: usize,
    marker: PhantomData<fn() -> M>,
}

impl<T, M> DabaLite<T, M>
where
    M: DabaMonoid<T>,
{
    /// Creates an empty window.
    pub fn new() -> Self {
        let queue = ChunkedQueue::new();
        let begin = queue.begin();
        Self {
            queue,
            f: begin.clone(),
            l: begin.clone(),
            r: begin.clone(),
            a: begin.clone(),
            b: begin.clone(),
            e: begin,
            aggregate_ra: Rc::new(M::empty()),
            aggregate_b: Rc::new(M::empty()),
            len: 0,
            marker: PhantomData,
        }
    }

    /// Returns the number of values in the current FIFO window.
    pub const fn len(&self) -> usize {
        self.len
    }

    /// Returns `true` when the current window is empty.
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Returns the aggregate in FIFO order.
    ///
    /// An empty query invokes [`DabaMonoid::empty`] and no combine callback. A nonempty query
    /// invokes [`DabaMonoid::combine`] exactly once.
    pub fn aggregate(&self) -> T {
        if self.len == 0 {
            M::empty()
        } else {
            let front = self.queue.read(&self.f);
            M::combine(front.as_ref(), self.aggregate_b.as_ref())
        }
    }

    /// Inserts `value` at the back of the window.
    ///
    /// The operation invokes [`DabaMonoid::combine`] between one and three times. If a monoid
    /// callback unwinds, no cursor, slot, chunk link, aggregate, or length change is published.
    ///
    /// # Panics
    ///
    /// Panics before invoking a callback if the length is already `usize::MAX`.
    pub fn insert(&mut self, value: T) {
        let new_len = self
            .len
            .checked_add(1)
            .expect("a DABA Lite window cannot exceed usize::MAX values");

        let value = Rc::new(value);
        let new_aggregate_b = Rc::new(M::combine(self.aggregate_b.as_ref(), value.as_ref()));
        let old_end = self.e.clone();
        let (new_end, provisional_link) = self.queue.plan_advance_end(&old_end);
        let overlay = Overlay {
            cursor: old_end.clone(),
            value: value.clone(),
        };
        let plan = self.plan_fixup(
            self.f.clone(),
            new_end.clone(),
            new_aggregate_b,
            provisional_link.as_ref(),
            Some(&overlay),
        );

        if let Some(link) = provisional_link.as_ref() {
            self.queue.commit_link(link);
        }
        self.queue.write(&old_end, value);
        self.queue.apply_write(plan.first_write.as_ref());
        self.queue.apply_write(plan.second_write.as_ref());
        self.e = new_end;
        self.len = new_len;
        self.publish_fixup(plan);
    }

    /// Evicts the oldest value, returning an error if the window is empty.
    ///
    /// The operation invokes [`DabaMonoid::combine`] at most twice. If a monoid callback unwinds,
    /// the exact published state is unchanged.
    pub fn evict(&mut self) -> Result<(), EmptyDabaLiteError> {
        if self.try_evict() {
            Ok(())
        } else {
            Err(EmptyDabaLiteError)
        }
    }

    /// Attempts to evict the oldest value.
    ///
    /// Returns `false` without invoking a callback when the window is empty. A successful eviction
    /// promptly clears the retired slot and detaches a predecessor chunk as soon as `F` crosses its
    /// boundary.
    pub fn try_evict(&mut self) -> bool {
        if self.len == 0 {
            return false;
        }

        let old_front = self.f.clone();
        let new_front = self.queue.advance(&old_front, None);
        let plan = self.plan_fixup(
            new_front.clone(),
            self.e.clone(),
            self.aggregate_b.clone(),
            None,
            None,
        );

        self.queue.apply_write(plan.first_write.as_ref());
        self.queue.apply_write(plan.second_write.as_ref());
        self.f = new_front;
        self.len -= 1;
        self.publish_fixup(plan);
        self.queue.clear_slot(&old_front);
        self.queue.trim_before(&self.f);
        true
    }

    /// Removes every value and restores one empty chunk.
    ///
    /// An empty clear performs no callback. A nonempty clear obtains the monoid identity exactly
    /// once before publishing any change and invokes `combine` zero times, so an unwinding identity
    /// callback leaves the window untouched. After publication, Rust deterministically drops all
    /// retired slots and chunks; consequently this operation is O(n + c), not O(1), for `n` values
    /// in `c` chunks.
    pub fn clear(&mut self) {
        if self.len == 0 {
            return;
        }

        let empty = Rc::new(M::empty());
        let new_queue = ChunkedQueue::new();
        let begin = new_queue.begin();
        let old_queue = std::mem::replace(&mut self.queue, new_queue);

        self.f = begin.clone();
        self.l = begin.clone();
        self.r = begin.clone();
        self.a = begin.clone();
        self.b = begin.clone();
        self.e = begin;
        self.aggregate_ra = empty.clone();
        self.aggregate_b = empty;
        self.len = 0;

        // `ChunkedQueue::drop` breaks the chain iteratively and clears every slot, avoiding both
        // recursive destruction and deferred retention of arbitrary owned values.
        drop(old_queue);
    }

    /// Validates chunk links, cursor order, count equations, and DABA Lite region invariants.
    ///
    /// Validation invokes no monoid callback and takes O(c) time and space for `c` active chunks.
    /// Since incremental reversal overwrites original values, an arbitrary non-invertible monoid
    /// does not permit this method to reconstruct content; compare [`aggregate`](Self::aggregate)
    /// with an external FIFO model when validating content.
    pub fn validate_structure(&self) -> Result<DabaLiteStatistics, DabaLiteInvariantError> {
        self.validate_cursor_index(&self.f, "F")?;
        self.validate_cursor_index(&self.l, "L")?;
        self.validate_cursor_index(&self.r, "R")?;
        self.validate_cursor_index(&self.a, "A")?;
        self.validate_cursor_index(&self.b, "B")?;
        self.validate_cursor_index(&self.e, "E")?;

        let first = self.queue.first.clone();
        if first.borrow().previous.upgrade().is_some() {
            return Err(invariant_error(
                "the first DABA Lite chunk has a predecessor",
            ));
        }
        if !Rc::ptr_eq(&first, &self.f.block) {
            return Err(invariant_error(
                "the DABA Lite front cursor is not in the first active chunk",
            ));
        }

        let mut seen = HashSet::new();
        let mut previous: Option<BlockRef<T>> = None;
        let mut block = first;
        let mut block_base = 0_usize;
        let mut block_count = 0_usize;
        let mut positions = [None; 6];

        loop {
            let pointer = Rc::as_ptr(&block) as usize;
            if !seen.insert(pointer) {
                return Err(invariant_error(
                    "the DABA Lite chunk chain contains a cycle",
                ));
            }

            let borrowed = block.borrow();
            let linked_previous = borrowed.previous.upgrade();
            match (&previous, &linked_previous) {
                (None, None) => {}
                (Some(expected), Some(actual)) if Rc::ptr_eq(expected, actual) => {}
                _ => {
                    return Err(invariant_error(
                        "a DABA Lite chunk has an invalid predecessor link",
                    ));
                }
            }
            if let Some(previous) = previous.as_ref() {
                let successor = previous.borrow().next.clone();
                if !successor
                    .as_ref()
                    .is_some_and(|successor| Rc::ptr_eq(successor, &block))
                {
                    return Err(invariant_error(
                        "a DABA Lite chunk has an invalid successor link",
                    ));
                }
            }

            self.record_cursor_positions(&block, block_base, &mut positions)?;
            block_count = block_count
                .checked_add(1)
                .ok_or_else(|| invariant_error("the DABA Lite chunk count overflowed"))?;

            let is_end_block = Rc::ptr_eq(&block, &self.e.block);
            let next = borrowed.next.clone();
            drop(borrowed);
            if is_end_block {
                if next.is_some() {
                    return Err(invariant_error(
                        "the DABA Lite end cursor is not in the last active chunk",
                    ));
                }
                break;
            }

            previous = Some(block);
            block = next.ok_or_else(|| {
                invariant_error("the DABA Lite end cursor is not reachable from the front")
            })?;
            block_base = block_base
                .checked_add(BLOCK_CAPACITY)
                .ok_or_else(|| invariant_error("a DABA Lite cursor position overflowed"))?;
        }

        let [Some(f), Some(l), Some(r), Some(a), Some(b), Some(e)] = positions else {
            return Err(invariant_error(
                "a DABA Lite cursor is outside the active chunk chain",
            ));
        };
        if !(f <= l && l <= r && r <= a && a <= b && b <= e) {
            return Err(invariant_error(
                "DABA Lite cursors do not satisfy F <= L <= R <= A <= B <= E",
            ));
        }
        if e - f != self.len {
            return Err(invariant_error(
                "the DABA Lite length disagrees with the F-to-E cursor distance",
            ));
        }

        let front_len = b - f;
        let back_len = e - b;
        let left_len = r - l;
        let right_len = a - r;
        let accumulator_len = b - a;
        if self.len == 0 {
            if !(self.f.same(&self.l)
                && self.l.same(&self.r)
                && self.r.same(&self.a)
                && self.a.same(&self.b)
                && self.b.same(&self.e))
            {
                return Err(invariant_error(
                    "an empty DABA Lite window does not have coincident cursors",
                ));
            }
        } else {
            if left_len != right_len {
                return Err(invariant_error(
                    "the DABA Lite left and right work regions have different sizes",
                ));
            }
            let equation = left_len
                .checked_add(right_len)
                .and_then(|value| value.checked_add(accumulator_len))
                .and_then(|value| value.checked_add(1))
                .ok_or_else(|| invariant_error("a DABA Lite region equation overflowed"))?;
            let front_difference = front_len.checked_sub(back_len).ok_or_else(|| {
                invariant_error("the DABA Lite back region is longer than the front region")
            })?;
            if equation != front_difference {
                return Err(invariant_error(
                    "the DABA Lite incremental-reversal size equation is invalid",
                ));
            }
            let processed_prefix = back_len
                .checked_add(1)
                .ok_or_else(|| invariant_error("a DABA Lite region equation overflowed"))?;
            if l - f != processed_prefix {
                return Err(invariant_error(
                    "the DABA Lite processed-prefix distance is invalid",
                ));
            }
        }

        let expected_block_count = self
            .f
            .index
            .checked_add(self.len)
            .ok_or_else(|| invariant_error("the DABA Lite active span overflowed"))?
            / BLOCK_CAPACITY
            + 1;
        if block_count != expected_block_count {
            return Err(invariant_error(
                "the DABA Lite active chunk count is invalid",
            ));
        }
        let allocated_slot_capacity = block_count
            .checked_mul(BLOCK_CAPACITY)
            .ok_or_else(|| invariant_error("the DABA Lite slot capacity overflowed"))?;
        let slack_slot_count = allocated_slot_capacity
            .checked_sub(self.len)
            .ok_or_else(|| invariant_error("the DABA Lite slack count underflowed"))?;
        if !(1..=(2 * BLOCK_CAPACITY - 1)).contains(&slack_slot_count) {
            return Err(invariant_error(
                "the DABA Lite chunk slack exceeds its two-chunk bound",
            ));
        }

        Ok(DabaLiteStatistics {
            len: self.len,
            front_len,
            back_len,
            left_len,
            right_len,
            accumulator_len,
            block_count,
            allocated_slot_capacity,
            slack_slot_count,
        })
    }

    fn plan_fixup(
        &self,
        f: Cursor<T>,
        e: Cursor<T>,
        mut aggregate_b: Rc<T>,
        provisional_link: Option<&ProvisionalLink<T>>,
        overlay: Option<&Overlay<T>>,
    ) -> FixupPlan<T> {
        let mut l = self.l.clone();
        let mut r = self.r.clone();
        let mut a = self.a.clone();
        let mut b = self.b.clone();
        let mut aggregate_ra = self.aggregate_ra.clone();
        let mut reused_empty = None;

        if f.same(&b) {
            let empty = Rc::new(M::empty());
            l = e.clone();
            r = e.clone();
            a = e.clone();
            b = e;
            return FixupPlan::without_writes(l, r, a, b, empty.clone(), empty);
        }

        if l.same(&b) {
            let empty = Rc::new(M::empty());
            reused_empty = Some(empty.clone());
            l = f;
            a = e.clone();
            b = e;
            aggregate_ra = aggregate_b;
            aggregate_b = empty;
        }

        if l.same(&r) {
            let next_l = self.queue.advance(&l, provisional_link);
            let next_r = self.queue.advance(&r, provisional_link);
            let next_a = self.queue.advance(&a, provisional_link);
            let empty = reused_empty.unwrap_or_else(|| Rc::new(M::empty()));
            return FixupPlan::without_writes(next_l, next_r, next_a, b, empty, aggregate_b);
        }

        let next_left = self.queue.advance(&l, provisional_link);
        let before_a = self.queue.retreat(&a);
        let left_value = self.queue.read_with_overlay(&l, overlay);
        let new_left_value = Rc::new(M::combine(left_value.as_ref(), aggregate_ra.as_ref()));
        let product_a = if a.same(&b) {
            Rc::new(M::empty())
        } else {
            self.queue.read_with_overlay(&a, overlay)
        };
        let accumulator_value = self.queue.read_with_overlay(&before_a, overlay);
        let new_accumulator_value =
            Rc::new(M::combine(accumulator_value.as_ref(), product_a.as_ref()));

        FixupPlan {
            l: next_left,
            r,
            a: before_a.clone(),
            b,
            aggregate_ra,
            aggregate_b,
            first_write: Some(PlannedWrite {
                cursor: l,
                value: new_left_value,
            }),
            second_write: Some(PlannedWrite {
                cursor: before_a,
                value: new_accumulator_value,
            }),
        }
    }

    fn publish_fixup(&mut self, plan: FixupPlan<T>) {
        self.l = plan.l;
        self.r = plan.r;
        self.a = plan.a;
        self.b = plan.b;
        self.aggregate_ra = plan.aggregate_ra;
        self.aggregate_b = plan.aggregate_b;
    }

    fn validate_cursor_index(
        &self,
        cursor: &Cursor<T>,
        name: &'static str,
    ) -> Result<(), DabaLiteInvariantError> {
        if cursor.index >= BLOCK_CAPACITY {
            return Err(DabaLiteInvariantError {
                message: format!("the DABA Lite {name} cursor has an invalid chunk index"),
            });
        }
        Ok(())
    }

    fn record_cursor_positions(
        &self,
        block: &BlockRef<T>,
        block_base: usize,
        positions: &mut [Option<usize>; 6],
    ) -> Result<(), DabaLiteInvariantError> {
        for (position, cursor) in positions
            .iter_mut()
            .zip([&self.f, &self.l, &self.r, &self.a, &self.b, &self.e])
        {
            if Rc::ptr_eq(block, &cursor.block) {
                *position =
                    Some(block_base.checked_add(cursor.index).ok_or_else(|| {
                        invariant_error("a DABA Lite cursor position overflowed")
                    })?);
            }
        }
        Ok(())
    }
}

impl<T, M> Default for DabaLite<T, M>
where
    M: DabaMonoid<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

/// Statistics returned by [`DabaLite::validate_structure`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DabaLiteStatistics {
    /// Number of values in the FIFO window.
    pub len: usize,
    /// Length of the F-to-B front region.
    pub front_len: usize,
    /// Length of the B-to-E back region.
    pub back_len: usize,
    /// Length of the L-to-R work region.
    pub left_len: usize,
    /// Length of the R-to-A work region.
    pub right_len: usize,
    /// Length of the A-to-B accumulator region.
    pub accumulator_len: usize,
    /// Number of active fixed-size chunks.
    pub block_count: usize,
    /// Number of slots in active chunks.
    pub allocated_slot_capacity: usize,
    /// Number of allocated slots not occupied by logical window positions.
    pub slack_slot_count: usize,
}

/// A representation invariant failure reported by [`DabaLite::validate_structure`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DabaLiteInvariantError {
    message: String,
}

impl fmt::Display for DabaLiteInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for DabaLiteInvariantError {}

/// Error returned by [`DabaLite::evict`] for an empty window.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct EmptyDabaLiteError;

impl fmt::Display for EmptyDabaLiteError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("cannot evict from an empty DABA Lite window")
    }
}

impl std::error::Error for EmptyDabaLiteError {}

type BlockRef<T> = Rc<RefCell<Block<T>>>;

struct Block<T> {
    items: [Option<Rc<T>>; BLOCK_CAPACITY],
    previous: Weak<RefCell<Block<T>>>,
    next: Option<BlockRef<T>>,
}

impl<T> Block<T> {
    fn new(previous: Weak<RefCell<Block<T>>>) -> Self {
        Self {
            items: std::array::from_fn(|_| None),
            previous,
            next: None,
        }
    }
}

struct Cursor<T> {
    block: BlockRef<T>,
    index: usize,
}

impl<T> Cursor<T> {
    fn same(&self, other: &Self) -> bool {
        self.index == other.index && Rc::ptr_eq(&self.block, &other.block)
    }
}

impl<T> Clone for Cursor<T> {
    fn clone(&self) -> Self {
        Self {
            block: self.block.clone(),
            index: self.index,
        }
    }
}

struct ChunkedQueue<T> {
    first: BlockRef<T>,
}

impl<T> ChunkedQueue<T> {
    fn new() -> Self {
        Self {
            first: Rc::new(RefCell::new(Block::new(Weak::new()))),
        }
    }

    fn begin(&self) -> Cursor<T> {
        Cursor {
            block: self.first.clone(),
            index: 0,
        }
    }

    fn read(&self, cursor: &Cursor<T>) -> Rc<T> {
        cursor.block.borrow().items[cursor.index]
            .as_ref()
            .expect("DABA Lite attempted to read an uninitialized queue slot")
            .clone()
    }

    fn read_with_overlay(&self, cursor: &Cursor<T>, overlay: Option<&Overlay<T>>) -> Rc<T> {
        // The current one-step fixup never reads the just-inserted slot. Keep the overlay-aware
        // read at the planning boundary so a future batched fixup cannot accidentally observe an
        // unpublished queue slot while preserving the present side-effect-free plan/commit split.
        if let Some(overlay) = overlay
            && cursor.same(&overlay.cursor)
        {
            return overlay.value.clone();
        }
        self.read(cursor)
    }

    fn write(&self, cursor: &Cursor<T>, value: Rc<T>) {
        cursor.block.borrow_mut().items[cursor.index] = Some(value);
    }

    fn apply_write(&self, write: Option<&PlannedWrite<T>>) {
        if let Some(write) = write {
            self.write(&write.cursor, write.value.clone());
        }
    }

    fn clear_slot(&self, cursor: &Cursor<T>) {
        cursor.block.borrow_mut().items[cursor.index] = None;
    }

    fn advance(
        &self,
        cursor: &Cursor<T>,
        provisional_link: Option<&ProvisionalLink<T>>,
    ) -> Cursor<T> {
        if cursor.index + 1 < BLOCK_CAPACITY {
            return Cursor {
                block: cursor.block.clone(),
                index: cursor.index + 1,
            };
        }

        let next = cursor.block.borrow().next.clone().or_else(|| {
            provisional_link
                .filter(|link| Rc::ptr_eq(&link.predecessor, &cursor.block))
                .map(|link| link.successor.clone())
        });
        Cursor {
            block: next.expect("DABA Lite cursor advanced past the active chunk chain"),
            index: 0,
        }
    }

    fn plan_advance_end(&self, cursor: &Cursor<T>) -> (Cursor<T>, Option<ProvisionalLink<T>>) {
        if cursor.index + 1 < BLOCK_CAPACITY {
            return (
                Cursor {
                    block: cursor.block.clone(),
                    index: cursor.index + 1,
                },
                None,
            );
        }

        if let Some(next) = cursor.block.borrow().next.clone() {
            return (
                Cursor {
                    block: next,
                    index: 0,
                },
                None,
            );
        }

        let successor = Rc::new(RefCell::new(Block::new(Rc::downgrade(&cursor.block))));
        let link = ProvisionalLink {
            predecessor: cursor.block.clone(),
            successor: successor.clone(),
        };
        (
            Cursor {
                block: successor,
                index: 0,
            },
            Some(link),
        )
    }

    fn commit_link(&self, link: &ProvisionalLink<T>) {
        let mut predecessor = link.predecessor.borrow_mut();
        debug_assert!(predecessor.next.is_none());
        predecessor.next = Some(link.successor.clone());
    }

    fn retreat(&self, cursor: &Cursor<T>) -> Cursor<T> {
        if cursor.index != 0 {
            return Cursor {
                block: cursor.block.clone(),
                index: cursor.index - 1,
            };
        }

        let previous = cursor
            .block
            .borrow()
            .previous
            .upgrade()
            .expect("DABA Lite cursor retreated before the active chunk chain");
        Cursor {
            block: previous,
            index: BLOCK_CAPACITY - 1,
        }
    }

    fn trim_before(&mut self, front: &Cursor<T>) {
        if Rc::ptr_eq(&self.first, &front.block) {
            return;
        }

        let predecessor = front.block.borrow().previous.upgrade();
        self.first = front.block.clone();
        self.first.borrow_mut().previous = Weak::new();
        if let Some(predecessor) = predecessor {
            predecessor.borrow_mut().next = None;
        }
    }

    fn release_chain(first: BlockRef<T>) {
        let mut current = Some(first);
        while let Some(block) = current {
            let next = {
                let mut block = block.borrow_mut();
                for slot in &mut block.items {
                    *slot = None;
                }
                block.previous = Weak::new();
                block.next.take()
            };
            if let Some(next) = next.as_ref() {
                next.borrow_mut().previous = Weak::new();
            }
            current = next;
        }
    }
}

impl<T> Drop for ChunkedQueue<T> {
    fn drop(&mut self) {
        Self::release_chain(self.first.clone());
    }
}

struct ProvisionalLink<T> {
    predecessor: BlockRef<T>,
    successor: BlockRef<T>,
}

struct Overlay<T> {
    cursor: Cursor<T>,
    value: Rc<T>,
}

struct PlannedWrite<T> {
    cursor: Cursor<T>,
    value: Rc<T>,
}

struct FixupPlan<T> {
    l: Cursor<T>,
    r: Cursor<T>,
    a: Cursor<T>,
    b: Cursor<T>,
    aggregate_ra: Rc<T>,
    aggregate_b: Rc<T>,
    first_write: Option<PlannedWrite<T>>,
    second_write: Option<PlannedWrite<T>>,
}

impl<T> FixupPlan<T> {
    fn without_writes(
        l: Cursor<T>,
        r: Cursor<T>,
        a: Cursor<T>,
        b: Cursor<T>,
        aggregate_ra: Rc<T>,
        aggregate_b: Rc<T>,
    ) -> Self {
        Self {
            l,
            r,
            a,
            b,
            aggregate_ra,
            aggregate_b,
            first_write: None,
            second_write: None,
        }
    }
}

fn invariant_error(message: &'static str) -> DabaLiteInvariantError {
    DabaLiteInvariantError {
        message: message.to_owned(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::{Cell, RefCell};
    use std::collections::{HashSet, VecDeque};
    use std::panic::{AssertUnwindSafe, catch_unwind};

    const OFFSET_IDENTITY: i32 = 11;

    #[test]
    fn empty_evict_clear_and_builtin_sum_contracts() {
        let mut daba = DabaLite::<i64, SumMeasure<i64>>::new();
        assert!(daba.is_empty());
        assert_eq!(daba.len(), 0);
        assert_eq!(daba.aggregate(), 0);
        assert!(!daba.try_evict());
        assert_eq!(daba.evict(), Err(EmptyDabaLiteError));
        assert_eq!(
            daba.validate_structure().unwrap(),
            DabaLiteStatistics {
                len: 0,
                front_len: 0,
                back_len: 0,
                left_len: 0,
                right_len: 0,
                accumulator_len: 0,
                block_count: 1,
                allocated_slot_capacity: 64,
                slack_slot_count: 64,
            }
        );

        for value in 0..1_000 {
            daba.insert(value);
        }
        assert_eq!(daba.aggregate(), (0..1_000).sum());
        daba.clear();
        assert!(daba.is_empty());
        assert_eq!(daba.aggregate(), 0);
        assert_eq!(daba.validate_structure().unwrap().block_count, 1);
    }

    #[test]
    fn noncommutative_string_aggregation_preserves_fifo_order() {
        let mut daba = DabaLite::<String, StringMonoid>::new();
        let mut model = VecDeque::new();
        for index in 0..10_000 {
            let value = ((b'a' + (index % 26) as u8) as char).to_string();
            daba.insert(value.clone());
            model.push_back(value);
            if index % 3 == 0 {
                daba.evict().unwrap();
                model.pop_front();
            }
            assert_eq!(daba.aggregate(), model.iter().cloned().collect::<String>());
        }
    }

    #[test]
    fn randomized_history_matches_naive_sum_model() {
        let mut random = XorShift64::new(20_260_715);
        let mut daba = DabaLite::<i64, SumMeasure<i64>>::new();
        let mut model = VecDeque::new();
        for step in 0..100_000 {
            if model.is_empty() || random.next_u64() & 1 == 0 {
                let value = (random.next_u64() % 20_001) as i64 - 10_000;
                daba.insert(value);
                model.push_back(value);
            } else {
                assert!(daba.try_evict());
                model.pop_front();
            }

            assert_eq!(daba.aggregate(), model.iter().sum());
            assert_eq!(daba.len(), model.len());
            if step % 257 == 0 {
                assert_eq!(daba.validate_structure().unwrap().len, model.len());
            }
        }
    }

    #[test]
    fn core_operations_do_not_require_clone_values() {
        let mut daba = DabaLite::<NonClone, NonCloneSumMonoid>::new();
        daba.insert(NonClone(3));
        daba.insert(NonClone(5));
        daba.insert(NonClone(7));
        assert_eq!(daba.aggregate().0, 15);
        daba.evict().unwrap();
        assert_eq!(daba.aggregate().0, 12);
        assert_eq!(daba.validate_structure().unwrap().len, 2);
    }

    #[test]
    fn exhaustive_short_histories_match_noncommutative_model() {
        let left = Matrix::create(2);
        let right = Matrix::create(7);
        assert_ne!(Matrix::multiply(left, right), Matrix::multiply(right, left));

        const HISTORY_LEN: usize = 10;
        for mask in 0..1_usize << HISTORY_LEN {
            let mut daba = DabaLite::<Matrix, MatrixMonoid>::new();
            let mut model = VecDeque::new();
            for step in 0..HISTORY_LEN {
                if mask & (1 << step) != 0 {
                    let value = Matrix::create((mask * 17 + step + 1) as i64);
                    daba.insert(value);
                    model.push_back(value);
                } else if model.is_empty() {
                    assert!(!daba.try_evict());
                } else {
                    assert!(daba.try_evict());
                    model.pop_front();
                }
                assert_matrix_state(&daba, &model);
            }
        }
    }

    #[test]
    fn chunk_boundaries_and_steady_window_churn_remain_bounded() {
        for size in [63, 64, 65, 127, 128, 129] {
            let mut daba = DabaLite::<Matrix, MatrixMonoid>::new();
            let mut model = VecDeque::new();
            for index in 0..size {
                let value = Matrix::create(index as i64 + 1);
                daba.insert(value);
                model.push_back(value);
            }

            let initial = daba.validate_structure().unwrap();
            assert_eq!(initial.block_count, size / BLOCK_CAPACITY + 1);
            assert!((1..=127).contains(&initial.slack_slot_count));
            assert_matrix_state(&daba, &model);

            for index in 0..512 {
                daba.evict().unwrap();
                model.pop_front();
                let value = Matrix::create(10_000 + (size * 1_000 + index) as i64);
                daba.insert(value);
                model.push_back(value);
                let statistics = daba.validate_structure().unwrap();
                assert!((1..=127).contains(&statistics.slack_slot_count));
                assert!((1..=size / BLOCK_CAPACITY + 2).contains(&statistics.block_count));
                if index & 15 == 0 {
                    assert_eq!(daba.aggregate(), fold_matrices(&model));
                }
            }

            while !model.is_empty() {
                daba.evict().unwrap();
                model.pop_front();
                assert_matrix_state(&daba, &model);
            }
            let empty = daba.validate_structure().unwrap();
            assert_eq!(empty.block_count, 1);
            assert_eq!(empty.allocated_slot_capacity, BLOCK_CAPACITY);
        }
    }

    #[test]
    fn every_fixup_phase_and_callback_ceiling_is_observed() {
        callback_reset();
        let mut daba = DabaLite::<i32, CountingOffsetMonoid>::new();
        let mut model = VecDeque::new();
        let mut phases = HashSet::new();
        let mut maximum_insert = 0;
        let mut maximum_evict = 0;
        let mut maximum_query = 0;

        for cycle in 0..4 {
            for index in 0..130 {
                phases.insert(classify_next_fixup(
                    daba.validate_structure().unwrap(),
                    false,
                ));
                callback_reset();
                let value = cycle * 1_000 + index + 20;
                daba.insert(value);
                model.push_back(value);
                let counts = callback_counts();
                assert!((1..=3).contains(&counts.combine));
                maximum_insert = maximum_insert.max(counts.combine);
                assert_offset_state(&daba, &model, &mut maximum_query);
            }

            while !model.is_empty() {
                phases.insert(classify_next_fixup(
                    daba.validate_structure().unwrap(),
                    true,
                ));
                callback_reset();
                daba.evict().unwrap();
                model.pop_front();
                let counts = callback_counts();
                assert!(counts.combine <= 2);
                maximum_evict = maximum_evict.max(counts.combine);
                assert_offset_state(&daba, &model, &mut maximum_query);
            }
        }

        assert_eq!(
            phases,
            HashSet::from([
                FixupPhase::Singleton,
                FixupPhase::FlipAndShrink,
                FixupPhase::Shift,
                FixupPhase::Shrink,
            ])
        );
        assert_eq!(maximum_insert, 3);
        assert_eq!(maximum_evict, 2);
        assert_eq!(maximum_query, 1);
    }

    #[test]
    fn combine_panics_at_every_bounded_ordinal_leave_exact_state_unchanged() {
        for (inserting, ordinal) in [(true, 1), (true, 2), (true, 3), (false, 1), (false, 2)] {
            let history = find_callback_history::<ThrowingCombineMonoid>(
                inserting,
                ordinal,
                CallbackKind::Combine,
            );
            callback_reset();
            let (mut daba, mut model) = replay::<ThrowingCombineMonoid>(&history).unwrap();
            let before_statistics = daba.validate_structure().unwrap();
            let before_aggregate = fold_offset(&model);

            callback_throw_on(CallbackKind::Combine, ordinal);
            let failure = catch_unwind(AssertUnwindSafe(|| apply_candidate(&mut daba, inserting)));
            assert!(failure.is_err());
            assert_eq!(callback_counts().combine, ordinal);

            callback_reset();
            assert_eq!(daba.validate_structure().unwrap(), before_statistics);
            assert_eq!(daba.aggregate(), before_aggregate);
            retry_candidate_and_continue(&mut daba, &mut model, inserting);
        }
    }

    #[test]
    fn identity_panics_and_clear_identity_panic_are_atomic() {
        for inserting in [true, false] {
            let maximum =
                maximum_callback_count::<ThrowingEmptyMonoid>(inserting, CallbackKind::Empty);
            assert!((1..=2).contains(&maximum));
            for ordinal in 1..=maximum {
                let history = find_callback_history::<ThrowingEmptyMonoid>(
                    inserting,
                    ordinal,
                    CallbackKind::Empty,
                );
                callback_reset();
                let (mut daba, mut model) = replay::<ThrowingEmptyMonoid>(&history).unwrap();
                let before_statistics = daba.validate_structure().unwrap();
                let before_aggregate = fold_offset(&model);

                callback_throw_on(CallbackKind::Empty, ordinal);
                let failure =
                    catch_unwind(AssertUnwindSafe(|| apply_candidate(&mut daba, inserting)));
                assert!(failure.is_err());
                assert_eq!(callback_counts().empty, ordinal);

                callback_reset();
                assert_eq!(daba.validate_structure().unwrap(), before_statistics);
                assert_eq!(daba.aggregate(), before_aggregate);
                retry_candidate_and_continue(&mut daba, &mut model, inserting);
            }
        }

        callback_reset();
        let mut clear_target = DabaLite::<i32, ThrowingEmptyMonoid>::new();
        clear_target.insert(23);
        let before_statistics = clear_target.validate_structure().unwrap();
        let before_aggregate = clear_target.aggregate();
        callback_throw_on(CallbackKind::Empty, 1);
        let failure = catch_unwind(AssertUnwindSafe(|| clear_target.clear()));
        assert!(failure.is_err());

        callback_reset();
        assert_eq!(
            clear_target.validate_structure().unwrap(),
            before_statistics
        );
        assert_eq!(clear_target.aggregate(), before_aggregate);
        clear_target.clear();
        assert!(clear_target.is_empty());
    }

    #[test]
    fn failed_boundary_insert_does_not_publish_provisional_successor() {
        callback_reset();
        let mut daba = DabaLite::<i32, ThrowingCombineMonoid>::new();
        let mut model = VecDeque::new();
        for value in 20..83 {
            daba.insert(value);
            model.push_back(value);
        }
        let before_statistics = daba.validate_structure().unwrap();
        let before_aggregate = fold_offset(&model);
        assert_eq!(before_statistics.len, 63);
        assert_eq!(before_statistics.block_count, 1);
        assert!(daba.e.block.borrow().next.is_none());

        callback_throw_on(CallbackKind::Combine, 2);
        let failure = catch_unwind(AssertUnwindSafe(|| daba.insert(101)));
        assert!(failure.is_err());
        assert_eq!(callback_counts().combine, 2);

        callback_reset();
        assert!(daba.e.block.borrow().next.is_none());
        assert_eq!(daba.validate_structure().unwrap(), before_statistics);
        assert_eq!(daba.aggregate(), before_aggregate);
        daba.insert(101);
        model.push_back(101);
        assert_eq!(daba.validate_structure().unwrap().block_count, 2);
        assert_eq!(daba.aggregate(), fold_offset(&model));
    }

    #[test]
    fn eviction_and_clear_release_slots_and_chunks_promptly() {
        let mut block_daba = DabaLite::<i32, OffsetMonoid>::new();
        let retired_block = Rc::downgrade(&block_daba.queue.first);
        for value in 20..149 {
            block_daba.insert(value);
        }
        for _ in 0..64 {
            block_daba.evict().unwrap();
        }
        assert_eq!(block_daba.validate_structure().unwrap().block_count, 2);
        assert!(retired_block.upgrade().is_none());

        let mut reference_daba = DabaLite::<ReferenceAggregate, FirstReferenceMonoid>::new();
        let victim = Rc::new(Cell::new(1));
        let victim_weak = Rc::downgrade(&victim);
        reference_daba.insert(ReferenceAggregate::value(victim));
        reference_daba.insert(ReferenceAggregate::value(Rc::new(Cell::new(2))));
        reference_daba.evict().unwrap();
        assert!(victim_weak.upgrade().is_none());
        assert!(!reference_daba.aggregate().is_identity);

        let mut clear_daba = DabaLite::<ReferenceAggregate, FirstReferenceMonoid>::new();
        let mut weak_values = Vec::new();
        for value in 0..257 {
            let token = Rc::new(Cell::new(value));
            weak_values.push(Rc::downgrade(&token));
            clear_daba.insert(ReferenceAggregate::value(token));
        }
        assert!(weak_values.iter().all(|value| value.upgrade().is_some()));
        clear_daba.clear();
        assert!(weak_values.iter().all(|value| value.upgrade().is_none()));
        let statistics = clear_daba.validate_structure().unwrap();
        assert_eq!(statistics.block_count, 1);
        assert_eq!(statistics.allocated_slot_capacity, 64);
    }

    #[test]
    fn clear_and_validation_obey_callback_contracts_and_reuse_one_block() {
        callback_reset();
        let mut daba = DabaLite::<i32, CountingOffsetMonoid>::new();
        for value in 20..277 {
            daba.insert(value);
        }

        callback_reset();
        assert_eq!(daba.validate_structure().unwrap().len, 257);
        assert_eq!(callback_counts(), CallbackCounts::default());
        daba.clear();
        assert_eq!(callback_counts().combine, 0);
        assert_eq!(callback_counts().empty, 1);
        let statistics = daba.validate_structure().unwrap();
        assert_eq!(statistics.len, 0);
        assert_eq!(statistics.block_count, 1);
        assert_eq!(statistics.allocated_slot_capacity, 64);
        assert_eq!(statistics.slack_slot_count, 64);

        callback_reset();
        daba.clear();
        assert_eq!(callback_counts(), CallbackCounts::default());
        daba.insert(20);
        daba.insert(30);
        assert_eq!(daba.aggregate(), 39);
        assert_eq!(daba.validate_structure().unwrap().len, 2);
    }

    fn assert_matrix_state(daba: &DabaLite<Matrix, MatrixMonoid>, model: &VecDeque<Matrix>) {
        assert_eq!(daba.validate_structure().unwrap().len, model.len());
        assert_eq!(daba.aggregate(), fold_matrices(model));
    }

    fn fold_matrices(values: &VecDeque<Matrix>) -> Matrix {
        values
            .iter()
            .copied()
            .fold(Matrix::IDENTITY, Matrix::multiply)
    }

    fn assert_offset_state(
        daba: &DabaLite<i32, CountingOffsetMonoid>,
        model: &VecDeque<i32>,
        maximum_query: &mut usize,
    ) {
        assert_eq!(daba.validate_structure().unwrap().len, model.len());
        callback_reset();
        assert_eq!(daba.aggregate(), fold_offset(model));
        let query_count = callback_counts().combine;
        assert!(query_count <= 1);
        *maximum_query = (*maximum_query).max(query_count);
    }

    fn fold_offset(values: &VecDeque<i32>) -> i32 {
        values.iter().fold(OFFSET_IDENTITY, |aggregate, value| {
            aggregate + value - OFFSET_IDENTITY
        })
    }

    fn classify_next_fixup(statistics: DabaLiteStatistics, evicting: bool) -> FixupPhase {
        if (!evicting && statistics.len == 0) || (evicting && statistics.front_len == 1) {
            FixupPhase::Singleton
        } else if statistics.left_len + statistics.right_len + statistics.accumulator_len == 0 {
            FixupPhase::FlipAndShrink
        } else if statistics.left_len == 0 {
            FixupPhase::Shift
        } else {
            FixupPhase::Shrink
        }
    }

    fn find_callback_history<M>(
        inserting: bool,
        minimum_callbacks: usize,
        callback_kind: CallbackKind,
    ) -> Vec<bool>
    where
        M: DabaMonoid<i32>,
    {
        try_find_callback_history::<M>(inserting, minimum_callbacks, callback_kind)
            .expect("no short DABA history reaches the requested callback ordinal")
    }

    fn try_find_callback_history<M>(
        inserting: bool,
        minimum_callbacks: usize,
        callback_kind: CallbackKind,
    ) -> Option<Vec<bool>>
    where
        M: DabaMonoid<i32>,
    {
        for length in 0..=10 {
            for mask in 0..1_usize << length {
                let history = decode_history(mask, length);
                callback_reset();
                let Some((mut daba, model)) = replay::<M>(&history) else {
                    continue;
                };
                if !inserting && model.is_empty() {
                    continue;
                }
                callback_reset();
                apply_candidate(&mut daba, inserting);
                let counts = callback_counts();
                let observed = match callback_kind {
                    CallbackKind::Combine => counts.combine,
                    CallbackKind::Empty => counts.empty,
                };
                if observed >= minimum_callbacks {
                    callback_reset();
                    return Some(history);
                }
            }
        }
        None
    }

    fn maximum_callback_count<M>(inserting: bool, callback_kind: CallbackKind) -> usize
    where
        M: DabaMonoid<i32>,
    {
        let mut maximum = 0;
        for ordinal in 1..=3 {
            if try_find_callback_history::<M>(inserting, ordinal, callback_kind).is_none() {
                break;
            }
            maximum = ordinal;
        }
        maximum
    }

    fn decode_history(mask: usize, length: usize) -> Vec<bool> {
        (0..length).map(|index| mask & (1 << index) != 0).collect()
    }

    fn replay<M>(history: &[bool]) -> Option<(DabaLite<i32, M>, VecDeque<i32>)>
    where
        M: DabaMonoid<i32>,
    {
        let mut daba = DabaLite::<i32, M>::new();
        let mut model = VecDeque::new();
        let mut next = 20;
        for &inserting in history {
            if inserting {
                daba.insert(next);
                model.push_back(next);
                next += 1;
            } else if model.is_empty() {
                return None;
            } else {
                daba.evict().unwrap();
                model.pop_front();
            }
        }
        Some((daba, model))
    }

    fn apply_candidate<M>(daba: &mut DabaLite<i32, M>, inserting: bool)
    where
        M: DabaMonoid<i32>,
    {
        if inserting {
            daba.insert(101);
        } else {
            daba.evict().unwrap();
        }
    }

    fn retry_candidate_and_continue<M>(
        daba: &mut DabaLite<i32, M>,
        model: &mut VecDeque<i32>,
        inserting: bool,
    ) where
        M: DabaMonoid<i32>,
    {
        apply_candidate(daba, inserting);
        if inserting {
            model.push_back(101);
        } else {
            model.pop_front();
        }

        for index in 0..24 {
            daba.insert(200 + index);
            model.push_back(200 + index);
            if index & 1 == 0 {
                daba.evict().unwrap();
                model.pop_front();
            }
            assert_eq!(daba.validate_structure().unwrap().len, model.len());
            assert_eq!(daba.aggregate(), fold_offset(model));
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    struct Matrix(i64, i64, i64, i64);

    impl Matrix {
        const MODULUS: i64 = 1_000_003;
        const IDENTITY: Self = Self(1, 0, 0, 1);

        fn create(seed: i64) -> Self {
            Self(
                (seed * 17 + 3).unsigned_abs() as i64 % Self::MODULUS,
                (seed * 29 + 5).unsigned_abs() as i64 % Self::MODULUS,
                (seed * 43 + 7).unsigned_abs() as i64 % Self::MODULUS,
                (seed * 61 + 11).unsigned_abs() as i64 % Self::MODULUS,
            )
        }

        fn multiply(left: Self, right: Self) -> Self {
            Self(
                (left.0 * right.0 + left.1 * right.2) % Self::MODULUS,
                (left.0 * right.1 + left.1 * right.3) % Self::MODULUS,
                (left.2 * right.0 + left.3 * right.2) % Self::MODULUS,
                (left.2 * right.1 + left.3 * right.3) % Self::MODULUS,
            )
        }
    }

    struct StringMonoid;

    impl DabaMonoid<String> for StringMonoid {
        fn empty() -> String {
            String::new()
        }

        fn combine(left: &String, right: &String) -> String {
            let mut combined = String::with_capacity(left.len() + right.len());
            combined.push_str(left);
            combined.push_str(right);
            combined
        }
    }

    struct MatrixMonoid;

    impl DabaMonoid<Matrix> for MatrixMonoid {
        fn empty() -> Matrix {
            Matrix::IDENTITY
        }

        fn combine(left: &Matrix, right: &Matrix) -> Matrix {
            Matrix::multiply(*left, *right)
        }
    }

    struct OffsetMonoid;

    impl DabaMonoid<i32> for OffsetMonoid {
        fn empty() -> i32 {
            OFFSET_IDENTITY
        }

        fn combine(left: &i32, right: &i32) -> i32 {
            left + right - OFFSET_IDENTITY
        }
    }

    struct NonClone(i64);

    struct NonCloneSumMonoid;

    impl DabaMonoid<NonClone> for NonCloneSumMonoid {
        fn empty() -> NonClone {
            NonClone(0)
        }

        fn combine(left: &NonClone, right: &NonClone) -> NonClone {
            NonClone(left.0 + right.0)
        }
    }

    struct CountingOffsetMonoid;

    impl DabaMonoid<i32> for CountingOffsetMonoid {
        fn empty() -> i32 {
            callback_record(CallbackKind::Empty);
            OFFSET_IDENTITY
        }

        fn combine(left: &i32, right: &i32) -> i32 {
            callback_record(CallbackKind::Combine);
            left + right - OFFSET_IDENTITY
        }
    }

    struct ThrowingCombineMonoid;

    impl DabaMonoid<i32> for ThrowingCombineMonoid {
        fn empty() -> i32 {
            OFFSET_IDENTITY
        }

        fn combine(left: &i32, right: &i32) -> i32 {
            callback_record(CallbackKind::Combine);
            left + right - OFFSET_IDENTITY
        }
    }

    struct ThrowingEmptyMonoid;

    impl DabaMonoid<i32> for ThrowingEmptyMonoid {
        fn empty() -> i32 {
            callback_record(CallbackKind::Empty);
            OFFSET_IDENTITY
        }

        fn combine(left: &i32, right: &i32) -> i32 {
            left + right - OFFSET_IDENTITY
        }
    }

    #[derive(Clone)]
    struct ReferenceAggregate {
        _token: Option<Rc<Cell<i32>>>,
        is_identity: bool,
    }

    impl ReferenceAggregate {
        fn identity() -> Self {
            Self {
                _token: None,
                is_identity: true,
            }
        }

        fn value(token: Rc<Cell<i32>>) -> Self {
            Self {
                _token: Some(token),
                is_identity: false,
            }
        }
    }

    struct FirstReferenceMonoid;

    impl DabaMonoid<ReferenceAggregate> for FirstReferenceMonoid {
        fn empty() -> ReferenceAggregate {
            ReferenceAggregate::identity()
        }

        fn combine(left: &ReferenceAggregate, right: &ReferenceAggregate) -> ReferenceAggregate {
            if left.is_identity {
                right.clone()
            } else {
                left.clone()
            }
        }
    }

    #[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
    enum FixupPhase {
        Singleton,
        FlipAndShrink,
        Shift,
        Shrink,
    }

    #[derive(Clone, Copy)]
    enum CallbackKind {
        Combine,
        Empty,
    }

    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    struct CallbackCounts {
        combine: usize,
        empty: usize,
    }

    #[derive(Default)]
    struct CallbackState {
        counts: CallbackCounts,
        throw_combine_on: usize,
        throw_empty_on: usize,
    }

    thread_local! {
        static CALLBACK_STATE: RefCell<CallbackState> = RefCell::new(CallbackState::default());
    }

    fn callback_reset() {
        CALLBACK_STATE.with(|state| *state.borrow_mut() = CallbackState::default());
    }

    fn callback_counts() -> CallbackCounts {
        CALLBACK_STATE.with(|state| state.borrow().counts)
    }

    fn callback_throw_on(kind: CallbackKind, ordinal: usize) {
        CALLBACK_STATE.with(|state| {
            let mut state = state.borrow_mut();
            state.counts = CallbackCounts::default();
            match kind {
                CallbackKind::Combine => state.throw_combine_on = ordinal,
                CallbackKind::Empty => state.throw_empty_on = ordinal,
            }
        });
    }

    fn callback_record(kind: CallbackKind) {
        let should_panic = CALLBACK_STATE.with(|state| {
            let mut state = state.borrow_mut();
            match kind {
                CallbackKind::Combine => {
                    state.counts.combine += 1;
                    state.throw_combine_on == state.counts.combine
                }
                CallbackKind::Empty => {
                    state.counts.empty += 1;
                    state.throw_empty_on == state.counts.empty
                }
            }
        });
        assert!(!should_panic, "injected DABA monoid callback failure");
    }

    struct XorShift64(u64);

    impl XorShift64 {
        fn new(seed: u64) -> Self {
            Self(seed)
        }

        fn next_u64(&mut self) -> u64 {
            let mut value = self.0;
            value ^= value << 13;
            value ^= value >> 7;
            value ^= value << 17;
            self.0 = value;
            value
        }
    }
}
