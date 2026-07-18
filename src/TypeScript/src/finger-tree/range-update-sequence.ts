import type { RangeUpdateAlgebra } from "./range-update-algebra.js";
import {
    recordRangeUpdateElementApplyCallback,
    recordRangeUpdateElementMeasureCallback,
    recordRangeUpdateFacadeAllocation,
    recordRangeUpdateIdentityTestCallback,
    recordRangeUpdateMeasureApplyCallback,
    recordRangeUpdateMeasureCombineCallback,
    recordRangeUpdateNodeAllocation,
    recordRangeUpdateNodeVisit,
    recordRangeUpdatePush,
    recordRangeUpdateRotation,
    recordRangeUpdateSubtreeApplication,
    recordRangeUpdateTagComposeCallback,
} from "./range-update-sequence-internals.js";

const MAX_COUNT = 0x7fff_ffff;

interface RangeContext<Element, Measure, Tag> {
    readonly algebra: RangeUpdateAlgebra<Element, Measure, Tag>;
    readonly emptyMeasure: Measure;
    emptySequence: RangeUpdateSequence<Element, Measure, Tag> | undefined;
}

interface RangeNode<Element, Measure, Tag> {
    readonly value: Element;
    readonly left: RangeNode<Element, Measure, Tag> | undefined;
    readonly right: RangeNode<Element, Measure, Tag> | undefined;
    readonly height: number;
    readonly count: number;
    readonly measure: Measure;
    readonly hasPendingTag: boolean;
    readonly pendingTag: Tag | undefined;
}

interface NodeSplit<Element, Measure, Tag> {
    readonly left: RangeNode<Element, Measure, Tag> | undefined;
    readonly right: RangeNode<Element, Measure, Tag> | undefined;
}

interface RemovedMinimum<Element, Measure, Tag> {
    readonly minimum: Element;
    readonly remainder: RangeNode<Element, Measure, Tag> | undefined;
}

interface InheritedTag<Tag> {
    readonly hasTag: boolean;
    readonly tag: Tag | undefined;
}

interface TraversalFrame<Element, Measure, Tag> {
    readonly node: RangeNode<Element, Measure, Tag>;
    readonly hasInheritedTag: boolean;
    readonly inheritedTag: Tag | undefined;
    stage: 0 | 1 | 2 | 3;
    childTagComputed: boolean;
    childHasInheritedTag: boolean;
    childInheritedTag: Tag | undefined;
}

interface ValidationResult {
    readonly count: number;
    readonly height: number;
}

const contexts = new WeakMap<object, RangeContext<unknown, unknown, unknown>>();

function contextFor<Element, Measure, Tag>(
    algebra: RangeUpdateAlgebra<Element, Measure, Tag>,
): RangeContext<Element, Measure, Tag> {
    if ((typeof algebra !== "object" || algebra === null) && typeof algebra !== "function") {
        throw new TypeError("A range-update algebra object is required.");
    }

    const key = algebra as object;
    const existing = contexts.get(key) as RangeContext<Element, Measure, Tag> | undefined;
    if (existing !== undefined) return existing;

    const context: RangeContext<Element, Measure, Tag> = {
        algebra,
        emptyMeasure: algebra.identity,
        emptySequence: undefined,
    };
    contexts.set(key, context as unknown as RangeContext<unknown, unknown, unknown>);
    return context;
}

function heightOf<Element, Measure, Tag>(node: RangeNode<Element, Measure, Tag> | undefined): number {
    return node?.height ?? 0;
}

function countOf<Element, Measure, Tag>(node: RangeNode<Element, Measure, Tag> | undefined): number {
    return node?.count ?? 0;
}

function countOverflowError(): RangeError {
    return new RangeError("The operation would create a sequence with more than Int32.MaxValue elements.");
}

function checkedNodeCount(leftCount: number, rightCount: number): number {
    if (leftCount > MAX_COUNT - 1 - rightCount) throw countOverflowError();
    return leftCount + 1 + rightCount;
}

function measureElement<Element, Measure, Tag>(
    context: RangeContext<Element, Measure, Tag>,
    element: Element,
): Measure {
    recordRangeUpdateElementMeasureCallback();
    return context.algebra.measure(element);
}

function combineMeasures<Element, Measure, Tag>(
    context: RangeContext<Element, Measure, Tag>,
    left: Measure,
    right: Measure,
): Measure {
    recordRangeUpdateMeasureCombineCallback();
    return context.algebra.combine(left, right);
}

function isIdentityTag<Element, Measure, Tag>(
    context: RangeContext<Element, Measure, Tag>,
    tag: Tag,
): boolean {
    recordRangeUpdateIdentityTestCallback();
    return context.algebra.isIdentity(tag);
}

function composeTags<Element, Measure, Tag>(
    context: RangeContext<Element, Measure, Tag>,
    newer: Tag,
    older: Tag,
): Tag {
    recordRangeUpdateTagComposeCallback();
    return context.algebra.compose(newer, older);
}

function applyElementTag<Element, Measure, Tag>(
    context: RangeContext<Element, Measure, Tag>,
    tag: Tag,
    element: Element,
): Element {
    recordRangeUpdateElementApplyCallback();
    return context.algebra.applyElement(tag, element);
}

function applyMeasureTag<Element, Measure, Tag>(
    context: RangeContext<Element, Measure, Tag>,
    tag: Tag,
    measure: Measure,
    count: number,
): Measure {
    recordRangeUpdateMeasureApplyCallback();
    return context.algebra.applyMeasure(tag, measure, count);
}

function newRawNode<Element, Measure, Tag>(
    value: Element,
    left: RangeNode<Element, Measure, Tag> | undefined,
    right: RangeNode<Element, Measure, Tag> | undefined,
    height: number,
    count: number,
    measure: Measure,
    hasPendingTag: boolean,
    pendingTag: Tag | undefined,
): RangeNode<Element, Measure, Tag> {
    recordRangeUpdateNodeAllocation();
    return { value, left, right, height, count, measure, hasPendingTag, pendingTag };
}

function makeNode<Element, Measure, Tag>(
    value: Element,
    left: RangeNode<Element, Measure, Tag> | undefined,
    right: RangeNode<Element, Measure, Tag> | undefined,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    // Structural impossibility wins over every user-policy callback.
    const height = 1 + Math.max(heightOf(left), heightOf(right));
    const count = checkedNodeCount(countOf(left), countOf(right));

    let measure = measureElement(context, value);
    if (left !== undefined) measure = combineMeasures(context, left.measure, measure);
    if (right !== undefined) measure = combineMeasures(context, measure, right.measure);

    return newRawNode(value, left, right, height, count, measure, false, undefined);
}

function buildBalanced<Element, Measure, Tag>(
    values: readonly Element[],
    start: number,
    count: number,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> | undefined {
    if (count === 0) return undefined;
    const leftCount = Math.floor(count / 2);
    const middle = start + leftCount;
    const left = buildBalanced(values, start, leftCount, context);
    const right = buildBalanced(values, middle + 1, count - leftCount - 1, context);
    return makeNode(values[middle] as Element, left, right, context);
}

function applySubtree<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    newerTag: Tag,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    recordRangeUpdateNodeVisit();
    recordRangeUpdateSubtreeApplication();

    const value = applyElementTag(context, newerTag, node.value);
    const measure = applyMeasureTag(context, newerTag, node.measure, node.count);

    let hasPendingTag = true;
    let pendingTag: Tag | undefined = newerTag;
    if (node.hasPendingTag) {
        pendingTag = composeTags(context, newerTag, node.pendingTag as Tag);
        if (isIdentityTag(context, pendingTag)) {
            hasPendingTag = false;
            pendingTag = undefined;
        }
    }

    return newRawNode(
        value,
        node.left,
        node.right,
        node.height,
        node.count,
        measure,
        hasPendingTag,
        pendingTag,
    );
}

function push<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    if (!node.hasPendingTag) return node;

    recordRangeUpdatePush();
    const tag = node.pendingTag as Tag;
    const left = node.left === undefined ? undefined : applySubtree(node.left, tag, context);
    const right = node.right === undefined ? undefined : applySubtree(node.right, tag, context);
    return newRawNode(
        node.value,
        left,
        right,
        node.height,
        node.count,
        node.measure,
        false,
        undefined,
    );
}

function rotateLeft<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    recordRangeUpdateRotation();
    const pushedNode = push(node, context);
    const pivot = push(pushedNode.right as RangeNode<Element, Measure, Tag>, context);
    const lower = makeNode(pushedNode.value, pushedNode.left, pivot.left, context);
    return makeNode(pivot.value, lower, pivot.right, context);
}

function rotateRight<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    recordRangeUpdateRotation();
    const pushedNode = push(node, context);
    const pivot = push(pushedNode.left as RangeNode<Element, Measure, Tag>, context);
    const lower = makeNode(pushedNode.value, pivot.right, pushedNode.right, context);
    return makeNode(pivot.value, pivot.left, lower, context);
}

function balance<Element, Measure, Tag>(
    original: RangeNode<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    let node = original;
    const factor = heightOf(node.left) - heightOf(node.right);
    if (factor > 1) {
        const left = node.left as RangeNode<Element, Measure, Tag>;
        if (heightOf(left.left) < heightOf(left.right)) {
            node = push(node, context);
            const rotatedLeft = rotateLeft(node.left as RangeNode<Element, Measure, Tag>, context);
            node = makeNode(node.value, rotatedLeft, node.right, context);
        }
        return rotateRight(node, context);
    }

    if (factor < -1) {
        const right = node.right as RangeNode<Element, Measure, Tag>;
        if (heightOf(right.right) < heightOf(right.left)) {
            node = push(node, context);
            const rotatedRight = rotateRight(node.right as RangeNode<Element, Measure, Tag>, context);
            node = makeNode(node.value, node.left, rotatedRight, context);
        }
        return rotateLeft(node, context);
    }

    return node;
}

function insertCore<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag> | undefined,
    index: number,
    item: Element,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    if (node === undefined) return makeNode(item, undefined, undefined, context);

    recordRangeUpdateNodeVisit();
    const pushed = push(node, context);
    const leftCount = countOf(pushed.left);
    if (index <= leftCount) {
        const left = insertCore(pushed.left, index, item, context);
        return balance(makeNode(pushed.value, left, pushed.right, context), context);
    }

    const right = insertCore(pushed.right, index - leftCount - 1, item, context);
    return balance(makeNode(pushed.value, pushed.left, right, context), context);
}

function setCore<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    index: number,
    item: Element,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    recordRangeUpdateNodeVisit();
    const pushed = push(node, context);
    const leftCount = countOf(pushed.left);
    if (index < leftCount) {
        const left = setCore(pushed.left as RangeNode<Element, Measure, Tag>, index, item, context);
        return makeNode(pushed.value, left, pushed.right, context);
    }
    if (index > leftCount) {
        const right = setCore(
            pushed.right as RangeNode<Element, Measure, Tag>,
            index - leftCount - 1,
            item,
            context,
        );
        return makeNode(pushed.value, pushed.left, right, context);
    }

    return makeNode(item, pushed.left, pushed.right, context);
}

function extractMinimum<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): RemovedMinimum<Element, Measure, Tag> {
    recordRangeUpdateNodeVisit();
    const pushed = push(node, context);
    if (pushed.left === undefined) return { minimum: pushed.value, remainder: pushed.right };

    const removed = extractMinimum(pushed.left, context);
    return {
        minimum: removed.minimum,
        remainder: balance(makeNode(pushed.value, removed.remainder, pushed.right, context), context),
    };
}

function removeCore<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    index: number,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> | undefined {
    recordRangeUpdateNodeVisit();
    const pushed = push(node, context);
    const leftCount = countOf(pushed.left);
    if (index < leftCount) {
        const left = removeCore(pushed.left as RangeNode<Element, Measure, Tag>, index, context);
        return balance(makeNode(pushed.value, left, pushed.right, context), context);
    }
    if (index > leftCount) {
        const right = removeCore(
            pushed.right as RangeNode<Element, Measure, Tag>,
            index - leftCount - 1,
            context,
        );
        return balance(makeNode(pushed.value, pushed.left, right, context), context);
    }

    if (pushed.left === undefined) return pushed.right;
    if (pushed.right === undefined) return pushed.left;

    const removed = extractMinimum(pushed.right, context);
    return balance(makeNode(removed.minimum, pushed.left, removed.remainder, context), context);
}

function join<Element, Measure, Tag>(
    left: RangeNode<Element, Measure, Tag> | undefined,
    pivot: Element,
    right: RangeNode<Element, Measure, Tag> | undefined,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> {
    const leftHeight = heightOf(left);
    const rightHeight = heightOf(right);
    if (leftHeight <= rightHeight + 1 && rightHeight <= leftHeight + 1) {
        return makeNode(pivot, left, right, context);
    }

    recordRangeUpdateNodeVisit();
    if (leftHeight > rightHeight) {
        const pushedLeft = push(left as RangeNode<Element, Measure, Tag>, context);
        const boundary = join(pushedLeft.right, pivot, right, context);
        return balance(makeNode(pushedLeft.value, pushedLeft.left, boundary, context), context);
    }

    const pushedRight = push(right as RangeNode<Element, Measure, Tag>, context);
    const leading = join(left, pivot, pushedRight.left, context);
    return balance(makeNode(pushedRight.value, leading, pushedRight.right, context), context);
}

function joinConcatenated<Element, Measure, Tag>(
    left: RangeNode<Element, Measure, Tag> | undefined,
    right: RangeNode<Element, Measure, Tag> | undefined,
    context: RangeContext<Element, Measure, Tag>,
): RangeNode<Element, Measure, Tag> | undefined {
    if (left === undefined) return right;
    if (right === undefined) return left;
    const removed = extractMinimum(right, context);
    return join(left, removed.minimum, removed.remainder, context);
}

function splitNodes<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag> | undefined,
    index: number,
    context: RangeContext<Element, Measure, Tag>,
): NodeSplit<Element, Measure, Tag> {
    if (node === undefined) return { left: undefined, right: undefined };
    if (index === 0) return { left: undefined, right: node };
    if (index === node.count) return { left: node, right: undefined };

    recordRangeUpdateNodeVisit();
    const pushed = push(node, context);
    const leftCount = countOf(pushed.left);
    if (index <= leftCount) {
        const split = splitNodes(pushed.left, index, context);
        return {
            left: split.left,
            right: join(split.right, pushed.value, pushed.right, context),
        };
    }

    const split = splitNodes(pushed.right, index - leftCount - 1, context);
    return {
        left: join(pushed.left, pushed.value, split.left, context),
        right: split.right,
    };
}

function childInheritance<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    inherited: InheritedTag<Tag>,
    context: RangeContext<Element, Measure, Tag>,
): InheritedTag<Tag> {
    if (!node.hasPendingTag) return inherited;
    if (!inherited.hasTag) return { hasTag: true, tag: node.pendingTag };

    // Ancestor inheritance is newer than the node-local pending tag.
    const tag = composeTags(context, inherited.tag as Tag, node.pendingTag as Tag);
    return isIdentityTag(context, tag)
        ? { hasTag: false, tag: undefined }
        : { hasTag: true, tag };
}

function getElement<Element, Measure, Tag>(
    root: RangeNode<Element, Measure, Tag>,
    requestedIndex: number,
    context: RangeContext<Element, Measure, Tag>,
): Element {
    let node = root;
    let index = requestedIndex;
    let inherited: InheritedTag<Tag> = { hasTag: false, tag: undefined };

    for (;;) {
        recordRangeUpdateNodeVisit();
        const leftCount = countOf(node.left);
        if (index === leftCount) {
            return inherited.hasTag
                ? applyElementTag(context, inherited.tag as Tag, node.value)
                : node.value;
        }

        inherited = childInheritance(node, inherited, context);
        if (index < leftCount) {
            node = node.left as RangeNode<Element, Measure, Tag>;
        } else {
            index -= leftCount + 1;
            node = node.right as RangeNode<Element, Measure, Tag>;
        }
    }
}

function measureRangeCore<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    index: number,
    count: number,
    inherited: InheritedTag<Tag>,
    context: RangeContext<Element, Measure, Tag>,
): Measure {
    recordRangeUpdateNodeVisit();
    if (index === 0 && count === node.count) {
        return inherited.hasTag
            ? applyMeasureTag(context, inherited.tag as Tag, node.measure, node.count)
            : node.measure;
    }

    const end = index + count;
    const leftCount = countOf(node.left);
    let hasResult = false;
    let result = undefined as Measure;

    let childTagComputed = false;
    let childTag: InheritedTag<Tag> = { hasTag: false, tag: undefined };

    if (index < leftCount) {
        childTag = childInheritance(node, inherited, context);
        childTagComputed = true;
        const childCount = Math.min(count, leftCount - index);
        result = measureRangeCore(
            node.left as RangeNode<Element, Measure, Tag>,
            index,
            childCount,
            childTag,
            context,
        );
        hasResult = true;
    }

    if (index <= leftCount && end > leftCount) {
        let singleton = measureElement(context, node.value);
        if (inherited.hasTag) {
            singleton = applyMeasureTag(context, inherited.tag as Tag, singleton, 1);
        }
        result = hasResult ? combineMeasures(context, result, singleton) : singleton;
        hasResult = true;
    }

    const rightStartAtRoot = leftCount + 1;
    if (end > rightStartAtRoot) {
        if (!childTagComputed) childTag = childInheritance(node, inherited, context);
        const localStart = Math.max(0, index - rightStartAtRoot);
        const localEnd = end - rightStartAtRoot;
        const rightMeasure = measureRangeCore(
            node.right as RangeNode<Element, Measure, Tag>,
            localStart,
            localEnd - localStart,
            childTag,
            context,
        );
        result = hasResult ? combineMeasures(context, result, rightMeasure) : rightMeasure;
        hasResult = true;
    }

    if (!hasResult) throw new Error("A nonempty range produced no measure contribution.");
    return result;
}

function ensureChildTag<Element, Measure, Tag>(
    frame: TraversalFrame<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): void {
    if (frame.childTagComputed) return;
    const inherited = childInheritance(
        frame.node,
        { hasTag: frame.hasInheritedTag, tag: frame.inheritedTag },
        context,
    );
    frame.childHasInheritedTag = inherited.hasTag;
    frame.childInheritedTag = inherited.tag;
    frame.childTagComputed = true;
}

function createTraversalFrame<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    inherited: InheritedTag<Tag>,
): TraversalFrame<Element, Measure, Tag> {
    return {
        node,
        hasInheritedTag: inherited.hasTag,
        inheritedTag: inherited.tag,
        stage: 0,
        childTagComputed: false,
        childHasInheritedTag: false,
        childInheritedTag: undefined,
    };
}

function* iterateNodes<Element, Measure, Tag>(
    root: RangeNode<Element, Measure, Tag>,
    context: RangeContext<Element, Measure, Tag>,
): Generator<Element, void, unknown> {
    const stack: TraversalFrame<Element, Measure, Tag>[] = [
        createTraversalFrame(root, { hasTag: false, tag: undefined }),
    ];

    while (stack.length !== 0) {
        const frame = stack[stack.length - 1] as TraversalFrame<Element, Measure, Tag>;
        switch (frame.stage) {
            case 0:
                if (frame.node.left !== undefined) {
                    ensureChildTag(frame, context);
                    frame.stage = 1;
                    stack.push(createTraversalFrame(frame.node.left, {
                        hasTag: frame.childHasInheritedTag,
                        tag: frame.childInheritedTag,
                    }));
                } else {
                    frame.stage = 1;
                }
                break;

            case 1: {
                const value = frame.hasInheritedTag
                    ? applyElementTag(context, frame.inheritedTag as Tag, frame.node.value)
                    : frame.node.value;
                frame.stage = 2;
                yield value;
                break;
            }

            case 2:
                if (frame.node.right !== undefined) {
                    ensureChildTag(frame, context);
                    frame.stage = 3;
                    stack.push(createTraversalFrame(frame.node.right, {
                        hasTag: frame.childHasInheritedTag,
                        tag: frame.childInheritedTag,
                    }));
                } else {
                    frame.stage = 3;
                }
                break;

            case 3:
                stack.pop();
                break;
        }
    }
}

function structureStatistics<Element, Measure, Tag>(
    root: RangeNode<Element, Measure, Tag> | undefined,
): RangeUpdateSequenceStatistics {
    if (root === undefined) {
        return {
            count: 0,
            nodeCount: 0,
            height: 0,
            maximumAbsoluteBalanceFactor: 0,
            pendingTagNodeCount: 0,
            maximumPendingTagDepth: 0,
        };
    }

    let nodeCount = 0;
    let maximumAbsoluteBalanceFactor = 0;
    let pendingTagNodeCount = 0;
    let maximumPendingTagDepth = 0;
    const stack: { readonly node: RangeNode<Element, Measure, Tag>; readonly pendingDepth: number }[] = [
        { node: root, pendingDepth: 0 },
    ];

    while (stack.length !== 0) {
        const frame = stack.pop()!;
        const node = frame.node;
        nodeCount++;
        maximumAbsoluteBalanceFactor = Math.max(
            maximumAbsoluteBalanceFactor,
            Math.abs(heightOf(node.left) - heightOf(node.right)),
        );
        let pendingDepth = frame.pendingDepth;
        if (node.hasPendingTag) {
            pendingTagNodeCount++;
            pendingDepth++;
            maximumPendingTagDepth = Math.max(maximumPendingTagDepth, pendingDepth);
        }
        if (node.right !== undefined) stack.push({ node: node.right, pendingDepth });
        if (node.left !== undefined) stack.push({ node: node.left, pendingDepth });
    }

    return {
        count: root.count,
        nodeCount,
        height: root.height,
        maximumAbsoluteBalanceFactor,
        pendingTagNodeCount,
        maximumPendingTagDepth,
    };
}

function validateNode<Element, Measure, Tag>(
    node: RangeNode<Element, Measure, Tag>,
    measureEquals: (left: Measure, right: Measure) => boolean,
    context: RangeContext<Element, Measure, Tag>,
): ValidationResult {
    recordRangeUpdateNodeVisit();
    const left = node.left === undefined
        ? { count: 0, height: 0 }
        : validateNode(node.left, measureEquals, context);
    const right = node.right === undefined
        ? { count: 0, height: 0 }
        : validateNode(node.right, measureEquals, context);

    const expectedHeight = 1 + Math.max(left.height, right.height);
    const expectedCount = checkedNodeCount(left.count, right.count);
    const factor = left.height - right.height;
    if (Math.abs(factor) > 1) {
        throw new Error(`AVL balance factor ${factor} is outside [-1, 1] at a node of count ${node.count}.`);
    }
    if (node.height !== expectedHeight) {
        throw new Error(`Cached height ${node.height} disagrees with recomputed height ${expectedHeight}.`);
    }
    if (node.count !== expectedCount) {
        throw new Error(`Cached count ${node.count} disagrees with recomputed count ${expectedCount}.`);
    }
    if (node.hasPendingTag && isIdentityTag(context, node.pendingTag as Tag)) {
        throw new Error("A node retains a pending tag that the algebra recognizes as identity.");
    }

    let expectedMeasure = measureElement(context, node.value);
    if (node.left !== undefined) {
        const leftMeasure = node.hasPendingTag
            ? applyMeasureTag(context, node.pendingTag as Tag, node.left.measure, node.left.count)
            : node.left.measure;
        expectedMeasure = combineMeasures(context, leftMeasure, expectedMeasure);
    }
    if (node.right !== undefined) {
        const rightMeasure = node.hasPendingTag
            ? applyMeasureTag(context, node.pendingTag as Tag, node.right.measure, node.right.count)
            : node.right.measure;
        expectedMeasure = combineMeasures(context, expectedMeasure, rightMeasure);
    }
    if (!measureEquals(node.measure, expectedMeasure)) {
        throw new Error(`Cached logical measure disagrees with the recomputed measure at a node of count ${node.count}.`);
    }

    return { count: expectedCount, height: expectedHeight };
}

/** Result of splitting a range-update sequence at an element boundary. */
export interface RangeUpdateSequenceSplit<Element, Measure, Tag> {
    readonly left: RangeUpdateSequence<Element, Measure, Tag>;
    readonly right: RangeUpdateSequence<Element, Measure, Tag>;
}

/** Presence-safe neighboring value returned by a Range cursor. */
export interface RangeUpdateCursorPeek<Element> { readonly value: Element }

/** Structural diagnostics returned by `RangeUpdateSequence.validateStructure`. */
export interface RangeUpdateSequenceStatistics {
    readonly count: number;
    readonly nodeCount: number;
    readonly height: number;
    readonly maximumAbsoluteBalanceFactor: number;
    readonly pendingTagNodeCount: number;
    readonly maximumPendingTagDepth: number;
}

/**
 * Immutable measured sequence with lazily composed contiguous range updates.
 *
 * The representation is an implicit persistent AVL tree. A node's own value and cached
 * measure already reflect its pending tag while its children do not. Structural descent
 * pushes tags by path copying; reads carry inherited tags without allocating persistent nodes.
 */
export class RangeUpdateSequence<Element, Measure, Tag> implements Iterable<Element> {
    readonly #root: RangeNode<Element, Measure, Tag> | undefined;
    readonly #context: RangeContext<Element, Measure, Tag>;

    public readonly algebra: RangeUpdateAlgebra<Element, Measure, Tag>;

    private constructor(
        root: RangeNode<Element, Measure, Tag> | undefined,
        context: RangeContext<Element, Measure, Tag>,
        recordAllocation: boolean = true,
    ) {
        if (recordAllocation) recordRangeUpdateFacadeAllocation();
        this.#root = root;
        this.#context = context;
        this.algebra = context.algebra;
    }

    private static emptyFromContext<Element, Measure, Tag>(
        context: RangeContext<Element, Measure, Tag>,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        if (context.emptySequence !== undefined) return context.emptySequence;
        const empty = new RangeUpdateSequence<Element, Measure, Tag>(undefined, context, false);
        context.emptySequence = empty;
        return empty;
    }

    private wrap(
        root: RangeNode<Element, Measure, Tag> | undefined,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        return root === undefined
            ? RangeUpdateSequence.emptyFromContext(this.#context)
            : new RangeUpdateSequence(root, this.#context);
    }

    /** Returns the canonical empty sequence for an exact algebra object. */
    public static empty<Element, Measure, Tag>(
        algebra: RangeUpdateAlgebra<Element, Measure, Tag>,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        return RangeUpdateSequence.emptyFromContext(contextFor(algebra));
    }

    /** Creates a minimal-height sequence from a dense readonly array. */
    public static create<Element, Measure, Tag>(
        items: readonly Element[],
        algebra: RangeUpdateAlgebra<Element, Measure, Tag>,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        if (!Array.isArray(items)) throw new TypeError("A readonly array is required.");
        if (items.length > MAX_COUNT) throw countOverflowError();

        // Reject sparse arrays and clone before invoking any element-measure callback.
        const materialized: Element[] = [];
        for (let index = 0; index < items.length; index++) {
            if (!(index in items)) throw new TypeError("Sparse input arrays are not supported.");
            materialized.push(items[index] as Element);
        }

        const context = contextFor(algebra);
        if (materialized.length === 0) return RangeUpdateSequence.emptyFromContext(context);
        return new RangeUpdateSequence(
            buildBalanced(materialized, 0, materialized.length, context),
            context,
        );
    }

    /** Creates a sequence by eagerly enumerating a source exactly once. */
    public static createRange<Element, Measure, Tag>(
        items: Iterable<Element>,
        algebra: RangeUpdateAlgebra<Element, Measure, Tag>,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        if (items === null || items === undefined) throw new TypeError("The item source is required.");
        if (items instanceof RangeUpdateSequence && items.algebra === algebra) {
            return items as RangeUpdateSequence<Element, Measure, Tag>;
        }

        // Enumeration finishes before context initialization or element measurement. Enforce the
        // Int32 count boundary while consuming the source rather than relying on JavaScript's
        // larger maximum array length.
        const materialized: Element[] = [];
        for (const item of items) {
            if (materialized.length === MAX_COUNT) throw countOverflowError();
            materialized.push(item);
        }
        const context = contextFor(algebra);
        if (materialized.length === 0) return RangeUpdateSequence.emptyFromContext(context);
        return new RangeUpdateSequence(
            buildBalanced(materialized, 0, materialized.length, context),
            context,
        );
    }

    /** Idiomatic alias for `createRange`. */
    public static from<Element, Measure, Tag>(
        items: Iterable<Element>,
        algebra: RangeUpdateAlgebra<Element, Measure, Tag>,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        return RangeUpdateSequence.createRange(items, algebra);
    }

    /** Cached element count. */
    public get size(): number { return countOf(this.#root); }

    /** Cross-port alias for `size`. */
    public get count(): number { return this.size; }

    /** Creates an immutable positional and measured cursor at a gap in `0..size`. */
    public getCursor(position = 0): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        return new RangeUpdateSequenceCursor(this, position);
    }

    public get isEmpty(): boolean { return this.#root === undefined; }

    /** Cached ordered measure of the complete logical sequence. */
    public get measure(): Measure {
        return this.#root === undefined ? this.#context.emptyMeasure : this.#root.measure;
    }

    /** Returns the logical element at a valid zero-based index. */
    public get(index: number): Element {
        this.checkElementIndex(index);
        return getElement(this.#root as RangeNode<Element, Measure, Tag>, index, this.#context);
    }

    public prepend(item: Element): RangeUpdateSequence<Element, Measure, Tag> {
        return this.insert(0, item);
    }

    public append(item: Element): RangeUpdateSequence<Element, Measure, Tag> {
        return this.insert(this.size, item);
    }

    public insert(index: number, item: Element): RangeUpdateSequence<Element, Measure, Tag> {
        this.checkBoundaryIndex(index);
        this.checkRoomForOneMore();
        return this.wrap(insertCore(this.#root, index, item, this.#context));
    }

    /** Replaces an element without an equality-based identity shortcut. */
    public setItem(index: number, item: Element): RangeUpdateSequence<Element, Measure, Tag> {
        this.checkElementIndex(index);
        return this.wrap(setCore(
            this.#root as RangeNode<Element, Measure, Tag>,
            index,
            item,
            this.#context,
        ));
    }

    public removeAt(index: number): RangeUpdateSequence<Element, Measure, Tag> {
        this.checkElementIndex(index);
        return this.wrap(removeCore(
            this.#root as RangeNode<Element, Measure, Tag>,
            index,
            this.#context,
        ));
    }

    public concat(
        other: RangeUpdateSequence<Element, Measure, Tag>,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        if (!(other instanceof RangeUpdateSequence)) {
            throw new TypeError("Another range-update sequence is required.");
        }
        if (other.#context !== this.#context) {
            throw new TypeError("Sequences must retain the same range-update algebra object.");
        }
        if (other.size > MAX_COUNT - this.size) throw countOverflowError();
        if (this.isEmpty) return other;
        if (other.isEmpty) return this;

        const removed = extractMinimum(
            other.#root as RangeNode<Element, Measure, Tag>,
            this.#context,
        );
        return this.wrap(join(this.#root, removed.minimum, removed.remainder, this.#context));
    }

    public splitAt(index: number): RangeUpdateSequenceSplit<Element, Measure, Tag> {
        this.checkBoundaryIndex(index);
        if (index === 0) {
            return { left: RangeUpdateSequence.emptyFromContext(this.#context), right: this };
        }
        if (index === this.size) {
            return { left: this, right: RangeUpdateSequence.emptyFromContext(this.#context) };
        }

        const split = splitNodes(this.#root, index, this.#context);
        return { left: this.wrap(split.left), right: this.wrap(split.right) };
    }

    public getRange(index: number, count: number): RangeUpdateSequence<Element, Measure, Tag> {
        this.checkRange(index, count);
        if (count === 0) return RangeUpdateSequence.emptyFromContext(this.#context);
        if (count === this.size) return this;

        const tail = splitNodes(this.#root, index, this.#context).right;
        const middle = splitNodes(tail, count, this.#context).left;
        return this.wrap(middle);
    }

    public applyRange(
        index: number,
        count: number,
        tag: Tag,
    ): RangeUpdateSequence<Element, Measure, Tag> {
        this.checkRange(index, count);
        if (count === 0) return this;
        if (isIdentityTag(this.#context, tag)) return this;
        if (count === this.size) {
            return this.wrap(applySubtree(
                this.#root as RangeNode<Element, Measure, Tag>,
                tag,
                this.#context,
            ));
        }

        const first = splitNodes(this.#root, index, this.#context);
        const second = splitNodes(first.right, count, this.#context);
        const middle = applySubtree(
            second.left as RangeNode<Element, Measure, Tag>,
            tag,
            this.#context,
        );
        return this.wrap(joinConcatenated(
            joinConcatenated(first.left, middle, this.#context),
            second.right,
            this.#context,
        ));
    }

    public measureRange(index: number, count: number): Measure {
        this.checkRange(index, count);
        if (count === 0) return this.#context.emptyMeasure;
        if (count === this.size) return (this.#root as RangeNode<Element, Measure, Tag>).measure;
        return measureRangeCore(
            this.#root as RangeNode<Element, Measure, Tag>,
            index,
            count,
            { hasTag: false, tag: undefined },
            this.#context,
        );
    }

    /** Recomputes every structural and cached-measure invariant. */
    public validateStructure(
        measureEquals: (left: Measure, right: Measure) => boolean = Object.is,
    ): RangeUpdateSequenceStatistics {
        if (typeof measureEquals !== "function") throw new TypeError("A measure comparer is required.");
        if (this.#root === undefined) return structureStatistics(this.#root);

        const validated = validateNode(this.#root, measureEquals, this.#context);
        if (validated.count !== this.size) {
            throw new Error(`Root count ${this.size} disagrees with recomputed count ${validated.count}.`);
        }
        if (validated.height !== this.#root.height) {
            throw new Error(`Root height ${this.#root.height} disagrees with recomputed height ${validated.height}.`);
        }

        const statistics = structureStatistics(this.#root);
        if (statistics.nodeCount !== this.size) {
            throw new Error(
                `Reachable node count ${statistics.nodeCount} disagrees with root count ${this.size}.`,
            );
        }
        return statistics;
    }

    /** Counts node objects retained by both immutable versions. */
    public sharedNodeCount(other: RangeUpdateSequence<Element, Measure, Tag>): number {
        if (!(other instanceof RangeUpdateSequence)) {
            throw new TypeError("Another range-update sequence is required.");
        }
        if (other.#context !== this.#context) {
            throw new TypeError("Sequences must retain the same range-update algebra object.");
        }
        if (this.#root === undefined || other.#root === undefined) return 0;

        const mine = new Set<RangeNode<Element, Measure, Tag>>();
        const stack: RangeNode<Element, Measure, Tag>[] = [this.#root];
        while (stack.length !== 0) {
            const node = stack.pop()!;
            mine.add(node);
            if (node.left !== undefined) stack.push(node.left);
            if (node.right !== undefined) stack.push(node.right);
        }

        let shared = 0;
        const probe: RangeNode<Element, Measure, Tag>[] = [other.#root];
        while (probe.length !== 0) {
            const node = probe.pop()!;
            if (mine.has(node)) shared++;
            if (node.left !== undefined) probe.push(node.left);
            if (node.right !== undefined) probe.push(node.right);
        }
        return shared;
    }

    public toArray(): Element[] { return Array.from(this); }

    /** Returns an independent immutable-snapshot iterator over logical values. */
    public [Symbol.iterator](): IterableIterator<Element> {
        return this.#root === undefined
            ? ([] as Element[])[Symbol.iterator]()
            : iterateNodes(this.#root, this.#context);
    }

    private checkElementIndex(index: number): void {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) {
            throw new RangeError(`Index ${index} is outside the valid range of the sequence.`);
        }
    }

    private checkBoundaryIndex(index: number): void {
        if (!Number.isInteger(index) || index < 0 || index > this.size) {
            throw new RangeError(`Index ${index} is outside the valid boundary range of the sequence.`);
        }
    }

    private checkRange(index: number, count: number): void {
        if (!Number.isInteger(index) || index < 0) {
            throw new RangeError(`Index ${index} must be a nonnegative integer.`);
        }
        if (!Number.isInteger(count) || count < 0) {
            throw new RangeError(`Count ${count} must be a nonnegative integer.`);
        }
        if (index > this.size) {
            throw new RangeError(`Index ${index} is outside the valid boundary range of the sequence.`);
        }
        if (count > this.size - index) {
            throw new RangeError(`Count ${count} extends past the end of the sequence.`);
        }
    }

    private checkRoomForOneMore(): void {
        if (this.size === MAX_COUNT) throw countOverflowError();
    }
}

/** Immutable snapshot-plus-position cursor over a persistent lazy range-update sequence. */
export class RangeUpdateSequenceCursor<Element, Measure, Tag> {
    public constructor(
        public readonly sequence: RangeUpdateSequence<Element, Measure, Tag>,
        public readonly position = 0,
    ) {
        if (!Number.isInteger(position) || position < 0 || position > sequence.size) {
            throw new RangeError("Cursor position is outside the Range sequence.");
        }
    }
    public get size(): number { return this.sequence.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.size; }
    public get measureBefore(): Measure { return this.sequence.measureRange(0, this.position); }
    public get measureAfter(): Measure { return this.sequence.measureRange(this.position, this.size - this.position); }
    public peekPrevious(): RangeUpdateCursorPeek<Element> | undefined {
        return this.isAtStart ? undefined : { value: this.sequence.get(this.position - 1) };
    }
    public peekNext(): RangeUpdateCursorPeek<Element> | undefined {
        return this.isAtEnd ? undefined : { value: this.sequence.get(this.position) };
    }
    public movePrevious(): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new RangeUpdateSequenceCursor(this.sequence, this.position - 1);
    }
    public moveNext(): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new RangeUpdateSequenceCursor(this.sequence, this.position + 1);
    }
    public seek(position: number): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        return position === this.position ? this : new RangeUpdateSequenceCursor(this.sequence, position);
    }
    public insert(value: Element): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        return new RangeUpdateSequenceCursor(this.sequence.insert(this.position, value), this.position + 1);
    }
    public deletePrevious(): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        return new RangeUpdateSequenceCursor(this.sequence.removeAt(this.position - 1), this.position - 1);
    }
    public deleteNext(): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new RangeUpdateSequenceCursor(this.sequence.removeAt(this.position), this.position);
    }
    public replaceNext(value: Element): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new RangeUpdateSequenceCursor(this.sequence.setItem(this.position, value), this.position);
    }
    public measurePrevious(count: number): Measure {
        this.checkDirectionalCount(count, this.position);
        return this.sequence.measureRange(this.position - count, count);
    }
    public measureNext(count: number): Measure {
        this.checkDirectionalCount(count, this.size - this.position);
        return this.sequence.measureRange(this.position, count);
    }
    public applyPrevious(count: number, tag: Tag): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        this.checkDirectionalCount(count, this.position);
        const sequence = this.sequence.applyRange(this.position - count, count, tag);
        return sequence === this.sequence ? this : new RangeUpdateSequenceCursor(sequence, this.position);
    }
    public applyNext(count: number, tag: Tag): RangeUpdateSequenceCursor<Element, Measure, Tag> {
        this.checkDirectionalCount(count, this.size - this.position);
        const sequence = this.sequence.applyRange(this.position, count, tag);
        return sequence === this.sequence ? this : new RangeUpdateSequenceCursor(sequence, this.position);
    }
    public snapshot(): RangeUpdateSequence<Element, Measure, Tag> { return this.sequence; }
    private checkDirectionalCount(count: number, available: number): void {
        if (!Number.isInteger(count) || count < 0 || count > available) {
            throw new RangeError("Directional count is outside the cursor side.");
        }
    }
}
